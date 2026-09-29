#!/usr/bin/env bash
# Multi-container integration test: brings up the full docker-compose stack
# (postgres, dniip-server, 2 agents, dniip-api, dashboard, prometheus,
# grafana) and asserts the real, end-to-end behavior the unit tests can't
# reach on their own: agents actually connecting over TCP to the epoll
# server, telemetry landing in Postgres, and the REST API/metrics/dashboard
# all serving real data derived from it.
#
# Requires Docker + Docker Compose. Not run by `ctest` (which only exercises
# the portable, single-process unit tests) — invoke directly:
#   ./tests/integration/run_integration_test.sh
#
# Exit code 0 = all assertions passed. Non-zero = failure (message printed).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_DIR="${REPO_ROOT}/docker"
KEEP_UP="${KEEP_UP:-0}"  # set KEEP_UP=1 to leave the stack running after the test for manual poking

FAILURES=0
fail() { echo "FAIL: $1" >&2; FAILURES=$((FAILURES + 1)); }
pass() { echo "PASS: $1"; }

cleanup() {
    if [[ "${KEEP_UP}" != "1" ]]; then
        echo "Tearing down stack..."
        (cd "${COMPOSE_DIR}" && docker compose down -v) >/dev/null 2>&1
    else
        echo "KEEP_UP=1: leaving the stack running (docker compose down -v in ${COMPOSE_DIR} when done)"
    fi
}
trap cleanup EXIT

wait_for_http() {
    local url="$1" label="$2" timeout_s="${3:-60}"
    local waited=0
    until curl -sf "${url}" >/dev/null 2>&1; do
        sleep 2
        waited=$((waited + 2))
        if (( waited >= timeout_s )); then
            fail "${label} did not become reachable at ${url} within ${timeout_s}s"
            return 1
        fi
    done
    pass "${label} reachable at ${url} (after ~${waited}s)"
}

echo "== Building and starting the stack =="
(cd "${COMPOSE_DIR}" && docker compose up -d --build) || { fail "docker compose up failed"; exit 1; }

echo "== Waiting for core services =="
wait_for_http "http://localhost:9100/metrics" "dniip-server metrics" 90
wait_for_http "http://localhost:8080/api/nodes" "dniip-api" 90
wait_for_http "http://localhost:3002/" "dashboard (nginx)" 60
wait_for_http "http://localhost:9090/-/ready" "prometheus" 60
wait_for_http "http://localhost:3000/api/health" "grafana" 90

echo "== Waiting for at least one full agent telemetry cycle (agents collect every 30s) =="
sleep 40

echo "== Asserting agents registered as nodes =="
NODES_JSON="$(curl -sf http://localhost:8080/api/nodes)"
for node in agent-a agent-b; do
    if echo "${NODES_JSON}" | grep -q "\"node_id\":\"${node}\""; then
        pass "node ${node} present in /api/nodes"
    else
        fail "node ${node} missing from /api/nodes response: ${NODES_JSON}"
    fi
done

echo "== Asserting dniip-server sees connected agents (Prometheus metrics) =="
METRICS="$(curl -sf http://localhost:9100/metrics)"
CONNECTED="$(echo "${METRICS}" | grep '^dniip_connected_agents ' | awk '{print $2}')"
if [[ -n "${CONNECTED}" ]] && (( $(echo "${CONNECTED} >= 1" | bc -l 2>/dev/null || echo 0) )); then
    pass "dniip_connected_agents = ${CONNECTED} (>= 1)"
else
    fail "dniip_connected_agents metric missing or zero (got: '${CONNECTED}')"
fi

SAMPLES_TOTAL="$(echo "${METRICS}" | grep '^dniip_telemetry_samples_total ' | awk '{print $2}')"
if [[ -n "${SAMPLES_TOTAL}" ]] && (( SAMPLES_TOTAL > 0 )); then
    pass "dniip_telemetry_samples_total = ${SAMPLES_TOTAL} (> 0, data reached PostgreSQL via the pipeline)"
else
    fail "dniip_telemetry_samples_total is zero/missing — no telemetry made it through the pipeline"
fi

echo "== Asserting dashboard serves the built SPA =="
if curl -sf http://localhost:3002/ | grep -qi "<title>"; then
    pass "dashboard index.html served"
else
    fail "dashboard did not return an HTML page"
fi

echo "== Asserting Prometheus is actually scraping dniip-server =="
TARGETS="$(curl -sf http://localhost:9090/api/v1/targets)"
if echo "${TARGETS}" | grep -q '"health":"up"'; then
    pass "Prometheus has at least one healthy scrape target"
else
    fail "Prometheus has no healthy scrape targets: ${TARGETS}"
fi

echo
if (( FAILURES == 0 )); then
    echo "ALL INTEGRATION ASSERTIONS PASSED"
    exit 0
else
    echo "${FAILURES} INTEGRATION ASSERTION(S) FAILED"
    exit 1
fi
