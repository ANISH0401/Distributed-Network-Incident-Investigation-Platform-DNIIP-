# Sample data

All files here are **real captured output** from the actual binaries — generated
by running `scripts/seed_sample_data.cpp` against a fresh SQLite store and then
querying `dniip-api` over HTTP, not hand-written. Regenerate with:

```sh
cmake --build build --target dniip-seed-sample-data dniip-api
./build/scripts/dniip-seed-sample-data docs/samples/demo.sqlite
./build/server/dniip-api 8090 docs/samples/demo.sqlite &
curl -s http://localhost:8090/api/nodes | python3 -m json.tool > docs/samples/sample_nodes_response.json
# ...etc, see the commands in this file's git history for the full set
```

## Files

- **`telemetry_message.json`** — one telemetry sample exactly as an agent
  sends it as the JSON payload of a `kTelemetry` frame (see
  `docs/protocol.md`). Deliberately shows a *degraded* node (node-b, mid
  network-congestion incident) so every field is exercised meaningfully.
- **`sample_telemetry.json`** — `GET /api/nodes/node-a/telemetry?limit=5`
  response: telemetry history for a healthy node, newest first.
- **`sample_nodes_response.json`** — `GET /api/nodes` response: all six
  seeded nodes with their latest telemetry embedded.
- **`sample_incidents.json`** — `GET /api/incidents` response: all four
  seeded incidents (one per correlation rule), both active and resolved.
- **`sample_rca_report.json`** — `GET /api/incidents/INC-1002/report`
  response: full incident metadata plus the rendered report text.
- **`INC-100{1,2,3,4}_report.txt`** — the plain-text RCA report body for
  each seeded incident (Network Congestion, Routing Failure, Link Failure,
  DNS Service Outage respectively) — what `RcaEngine::render_text()`
  actually produces, and what the dashboard's report page displays.
- **`demo.sqlite`** — the seeded SQLite database itself. Point `dniip-api`
  or the dashboard at it directly for a quick demo with no agents running:
  `./build/server/dniip-api 8080 docs/samples/demo.sqlite`.
