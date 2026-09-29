# dniip-api reference

Read-only JSON REST API served by the `dniip-api` binary (`server/api/http_api_server.hpp`),
backing the dashboard. All responses are `application/json`; `Access-Control-Allow-Origin: *`
is set on every response so the dashboard can be served from a different
origin (e.g. the Vite dev server) than the API.

Base URL defaults to `http://localhost:8080` (configurable — see the
top-level README's "Running natively" section, or `DNIIP_METRICS_PORT`/
`VITE_API_BASE` for the compose deployment).

Only `GET` is supported; any other method returns `405`. An unrecognized
path returns `404` with `{"error": "not found"}`.

## `GET /api/nodes`

All known nodes with their latest telemetry sample embedded.

```json
[
  {
    "node_id": "node-a",
    "first_seen": "2026-09-27T12:50:20Z",
    "last_seen": "2026-09-27T13:50:20Z",
    "latest_telemetry": { "...": "TelemetrySample, see protocol.md, or null if none yet" }
  }
]
```

See `docs/samples/sample_nodes_response.json` for a full real example.

## `GET /api/nodes/{node_id}/telemetry?limit=N`

Telemetry history for one node, newest first. `limit` defaults to 50.

```json
[ { "...": "TelemetrySample" }, "..." ]
```

See `docs/samples/sample_telemetry.json`.

## `GET /api/incidents?status=active|resolved&limit=N`

Incidents, newest first. `status` is optional (omit for all); `limit`
defaults to 100.

```json
[
  {
    "incident_id": "INC-1001",
    "severity": "MEDIUM",
    "category": "NETWORK",
    "root_cause": "Network Congestion",
    "affected_nodes": ["node-b"],
    "symptoms": ["node-b: packet loss 35%, RTT 320ms, 22 retransmits"],
    "confidence": 0.75,
    "created_at": "2026-09-27T13:50:20Z",
    "status": "ACTIVE"
  }
]
```

`severity` is one of `LOW`/`MEDIUM`/`HIGH`/`CRITICAL`; `status` is
`ACTIVE`/`RESOLVED`. See `docs/samples/sample_incidents.json`.

## `GET /api/incidents/{incident_id}/report`

Full incident metadata (same shape as one entry above) plus the rendered
RCA report:

```json
{
  "incident_id": "INC-1002",
  "severity": "HIGH",
  "...": "(all Incident fields)",
  "report_text": "INCIDENT REPORT\nIncident ID:\n...",
  "generated_at": "2026-09-27T14:03:03Z"
}
```

If the incident has no report yet, `report_text` and `generated_at` are
empty strings rather than the field being omitted. If the incident itself
doesn't exist, the response body is `{"error": "incident not found"}`
(still HTTP 200 — the route matched, the lookup didn't; see
`HttpApiServer::route`'s doc comment for that distinction).

See `docs/samples/sample_rca_report.json` and `docs/samples/INC-*_report.txt`
for the plain-text report bodies each of the four correlation rules
produces.

## Errors

| Status | When |
|---|---|
| 404 | Path doesn't match any route |
| 405 | Method other than GET |
| 500 | Unhandled exception while building the response (e.g. a database error); body is `{"error": "<exception message>"}` |
