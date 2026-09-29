# DNIIP Architecture

## Overview

```
+------------------+     +------------------+     +------------------+
|  dniip-agent      |     |  dniip-agent      |     |  dniip-agent      |
|  (Linux daemon)   | ... |  (Linux daemon)   | ... |  (Linux daemon)   |
|  node_id=hostname |     |  node_id=hostname |     |  node_id=hostname |
+--------+---------+     +--------+---------+     +--------+---------+
         |  custom TCP protocol (length-prefixed JSON, see protocol.md)
         v
+----------------------------------------------------------------+
|                          dniip-server                          |
|  epoll TCP collector (server/epoll_server/)                    |
|    -> BlockingQueue<TelemetrySample> (producer/consumer)        |
|    -> worker thread pool                                        |
|         -> CorrelationEngine  (4 rule-based detectors)          |
|         -> IncidentEngine     (severity, incident_id)           |
|         -> RcaEngine          (recommendations, report text)    |
|         -> ITelemetryStore    (writes)                          |
|  HttpMetricsServer :9100  ---> Prometheus                        |
+----------------------------------------------------------------+
         |
         v  ITelemetryStore (SqliteStore | PostgresStore)
+----------------------------------------------------------------+
|                    PostgreSQL  (or SQLite, interim)             |
|  nodes | telemetry | events | incidents | reports               |
+----------------------------------------------------------------+
         ^
         |  ITelemetryStore (reads)
+----------------------------------------------------------------+
|                          dniip-api                              |
|  HttpApiServer :8080 - read-only JSON REST API                  |
+----------------------------------------------------------------+
         ^
         |  fetch()
+----------------------------------------------------------------+
|              React + TypeScript + MUI + Chart.js dashboard      |
|  Home | Nodes | Incidents | RCA Reports                         |
+----------------------------------------------------------------+
```

Prometheus additionally scrapes `dniip-server:9100/metrics`, and Grafana
renders a provisioned dashboard from that data (see `monitoring/`).

## Why two server binaries, not one

`dniip-server` (telemetry ingestion) and `dniip-api` (read-only REST API)
are deliberately separate processes sharing one database, not two halves of
a single binary:

- `dniip-server` needs `epoll`, raw sockets, and is Linux-only. `dniip-api`
  has no such dependency — it's plain blocking sockets over `ITelemetryStore`
  — so it builds and runs on any platform (verified natively on macOS during
  development, see the top-level README).
- They scale independently: telemetry ingestion load (proportional to
  agent count) and dashboard read load (proportional to viewers) have
  unrelated growth curves in a real deployment.
- It keeps the ingestion server's hot path (epoll loop, correlation
  pipeline) free of anything that could block on a slow dashboard client.

Both implement the same `ITelemetryStore` interface (`server/database/store.hpp`)
against the same database, so this split has no effect on data consistency.

## Component-to-directory map

| Spec component | Directory | Notes |
|---|---|---|
| 1. Linux Monitoring Agent | `agent/` | collectors (`/proc`, `/sys/class/net`), diagnostics (raw ICMP, `getaddrinfo`), SQLite store-and-forward, TCP client, daemon loop |
| 2. Custom TCP Transport | `common/protocol.hpp`, `server/epoll_server/`, `agent/networking/` | length-prefixed JSON framing, see `protocol.md` |
| 3. Diagnostics Engine | `agent/diagnostics/` | packet loss/RTT/DNS/routing/interface checks, folded into `TelemetrySample` |
| 4. Event Correlation Engine | `server/correlation_engine/` | 4 rule-based detectors |
| 5. Incident Engine | `server/incident_engine/` | severity scoring, incident IDs |
| 6. Root Cause Analysis Engine | `server/rca_engine/` | recommendation lookup table, report rendering |
| 7. PostgreSQL Database | `server/database/postgres_store.hpp`, `schema.sql` | also `sqlite_store.hpp` for local dev, both behind `ITelemetryStore` |
| 8. Web Dashboard | `dashboard/`, `server/api/` | React frontend + its `dniip-api` backend |
| 9. Monitoring | `server/metrics/`, `monitoring/` | Prometheus text exposition + Grafana provisioning |
| 10. Advanced Linux Features | throughout | systemd units in `scripts/systemd/`, `std::thread`/mutex/condvar in the blocking queue, signal handling in both `main.cpp`s, spdlog everywhere |

## Data flow: one telemetry sample's journey

1. `agent/service/main.cpp`'s 30s loop calls the collectors, assembles a
   `TelemetrySample`, and writes it to the local SQLite buffer
   (`agent/storage/sqlite_buffer.hpp`) — always, before attempting network
   I/O, so a connectivity outage never loses a sample.
2. `TcpClient::send_frame` encodes it via `protocol::encode_frame` and sends
   it over the persistent TCP connection to `dniip-server`. On success, the
   local row is marked synced; on failure it stays pending for the next
   cycle's retry pass.
3. `EpollServer`'s reactor thread reads bytes off the socket, reassembles
   frames (`try_parse_header` / length check), parses the JSON payload, and
   hands it to `TelemetryCollector::on_frame`, which pushes it onto a
   `BlockingQueue<TelemetrySample>`.
4. A worker thread pops the sample, writes it to the store
   (`upsert_node` + `insert_telemetry`), updates Prometheus gauges
   (per-node packet loss, health score), and feeds it to
   `CorrelationEngine::ingest`.
5. If any of the 4 rules fire, `IncidentEngine::create_incident` builds an
   `Incident` (severity derived from affected-node count), it's persisted,
   `RcaEngine::generate` + `render_text` produce the report, and it's
   persisted too. `MetricsRegistry::inc_incident_count` fires.
6. The dashboard, polling `dniip-api` every 15s, picks up the new incident
   and telemetry on its next poll — no push/websocket layer; simple polling
   was sufficient at this data rate and kept the API stateless.

## Correlation rules (server/correlation_engine/correlation_engine.hpp)

All four evaluate against the latest known sample per node on every
telemetry update (not a time-windowed batch job) — see the class-level
comment there for the rationale.

| Rule | Condition | Output |
|---|---|---|
| 1 | packet_loss ≥ 10% AND rtt ≥ 150ms AND retransmits ≥ 5, single node | Network Congestion |
| 2 | gateway unreachable on ≥ 2 nodes | Routing Failure |
| 3 | any interface reporting down | Link Failure |
| 4 | DNS resolution failing | DNS Service Outage |

Thresholds are a `CorrelationThresholds` struct, not magic numbers, so they
can be tuned per deployment without touching rule logic.
