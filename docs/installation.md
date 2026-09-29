# Installation guide

Three ways to run DNIIP, from quickest to most production-like.

## 1. Docker Compose (recommended, full stack)

Requires Docker + Docker Compose. `dniip-server`/`dniip-agent` need Linux
(`epoll`, `/proc`), which is why this is the path of least resistance even
on macOS/Windows dev machines — the compose file builds Linux containers
regardless of host OS.

On macOS without Docker Desktop, [Colima](https://github.com/abiosoft/colima)
works just as well (this is how the whole stack was verified during
development):

```sh
brew install docker docker-compose colima
colima start --cpu 4 --memory 8 --disk 40
mkdir -p ~/.docker && cat > ~/.docker/config.json <<'EOF'
{ "cliPluginsExtraDirs": ["/opt/homebrew/lib/docker/cli-plugins"] }
EOF
```

```sh
git clone <this-repo>
cd distributed-network-incident-platform/docker
docker compose up --build
```

Services and ports:

| Service | Port(s) | Purpose |
|---|---|---|
| `postgres` | (internal) | backing store |
| `dniip-server` | 9000 (telemetry TCP), 9100 (`/metrics`) | ingestion + correlation/incident/RCA pipeline |
| `dniip-agent-a`, `dniip-agent-b` | — | sample agents, hostnames pinned to `agent-a`/`agent-b` |
| `dniip-api` | 8080 | REST API |
| `dashboard` | 3002 | React dashboard (nginx) |
| `prometheus` | 9090 | scrapes `dniip-server:9100` |
| `grafana` | 3000 | anonymous viewer access enabled, DNIIP Overview dashboard pre-provisioned |

Open `http://localhost:3002` for the dashboard, `http://localhost:3000` for
Grafana. Tear down with `docker compose down -v` (the `-v` also drops the
named volumes — omit it to keep Postgres/Grafana state across restarts).

To point agents at a real fleet instead of the two sample containers,
deploy `dniip-agent` (see systemd section below) on each target host with
`server_host` set to wherever `dniip-server` is reachable, and remove
`dniip-agent-a`/`-b` from `docker-compose.yml`.

## 2. Native build (portable pieces only — no agent/dniip-server)

Useful for iterating on the correlation/incident/RCA engines, the REST API,
or the dashboard without needing Linux or Docker. See the top-level
`README.md`'s "Building natively" and "Running the dashboard locally
without Docker" sections for the exact commands — summarized:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<brew prefixes for nlohmann-json, spdlog, sqlite, googletest, libpq>"
cmake --build build -j
ctest --test-dir build --output-on-failure   # 18/18 on a portable host

./build/scripts/dniip-seed-sample-data demo.sqlite
./build/server/dniip-api 8080 demo.sqlite &
cd dashboard && npm install && npm run dev
```

Required packages (Homebrew names shown; Debian/Ubuntu package names are
in `docker/Dockerfile.server`): `cmake`, `nlohmann-json`, `spdlog`,
`sqlite`, `libpq`, `googletest`, `node`.

## 3. Bare-metal Linux with systemd (production agent/server deployment)

For deploying `dniip-agent` on real Linux hosts and/or `dniip-server`/
`dniip-api` on a real Linux server outside of containers.

### Build

```sh
sudo apt-get install -y build-essential cmake pkg-config \
    nlohmann-json3-dev libspdlog-dev libsqlite3-dev libpq-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build   # installs dniip-agent, dniip-server, dniip-api to /usr/local/bin
```

### Provision the service user and state directory

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin dniip
sudo mkdir -p /var/lib/dniip
sudo chown dniip:dniip /var/lib/dniip
```

### Install and enable the systemd units

Unit files are in `scripts/systemd/`. On an agent host:

```sh
sudo cp scripts/systemd/dniip-agent.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now dniip-agent
```

Edit the `ExecStart` line first to point at your actual `dniip-server`
host/port. On the server host (both units, or split across two hosts):

```sh
sudo cp scripts/systemd/dniip-server.service scripts/systemd/dniip-api.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now dniip-server dniip-api
```

To use PostgreSQL instead of the interim SQLite store, uncomment and fill
in the `DNIIP_PG_CONNINFO` line in whichever unit(s) need it and apply
`server/database/schema.sql` to that database first (or just let
`dniip-server`/`dniip-api` apply it automatically on first connect — see
`PostgresStore`'s constructor).

Both units run as an unprivileged `dniip` user with `ProtectSystem=strict`
and `NoNewPrivileges=true`; `dniip-agent.service` additionally grants
`CAP_NET_RAW` via `AmbientCapabilities` (needed for the raw ICMP socket in
`ConnectivityDiagnostics::ping`) rather than running as root.

### Verify

```sh
sudo systemctl status dniip-agent    # or dniip-server / dniip-api
journalctl -u dniip-server -f        # spdlog output goes to the journal
curl http://localhost:9100/metrics   # on the server host
curl http://localhost:8080/api/nodes # on the API host
```

## Running the integration test

Requires Docker. Brings up the full compose stack and asserts real
end-to-end behavior (agents connecting, telemetry reaching Postgres,
Prometheus scraping, dashboard serving):

```sh
./tests/integration/run_integration_test.sh
```

Set `KEEP_UP=1` to leave the stack running after the test for manual
inspection.
