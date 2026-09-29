-- DNIIP PostgreSQL schema (spec Component 7).
--
-- Kept byte-for-byte in sync with the apply_schema() literal embedded in
-- server/database/postgres_store.hpp (which the server actually runs); this
-- copy is for `psql -f schema.sql` / manual inspection.

CREATE TABLE IF NOT EXISTS nodes (
    node_id     TEXT PRIMARY KEY,
    first_seen  TIMESTAMPTZ NOT NULL,
    last_seen   TIMESTAMPTZ NOT NULL
);

CREATE TABLE IF NOT EXISTS telemetry (
    id                 BIGSERIAL PRIMARY KEY,
    node_id            TEXT NOT NULL REFERENCES nodes(node_id),
    "timestamp"        TIMESTAMPTZ NOT NULL,
    cpu_usage          DOUBLE PRECISION NOT NULL,
    memory_usage       DOUBLE PRECISION NOT NULL,
    packet_loss        DOUBLE PRECISION NOT NULL,
    rtt_ms             DOUBLE PRECISION NOT NULL,
    gateway_reachable  BOOLEAN NOT NULL,
    dns_resolvable     BOOLEAN NOT NULL,
    tcp_retransmits    INTEGER NOT NULL,
    interfaces_json    TEXT NOT NULL DEFAULT '[]',  -- JSON-encoded InterfaceStats[]
    created_at         TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_telemetry_node_ts ON telemetry(node_id, "timestamp" DESC);

CREATE TABLE IF NOT EXISTS events (
    id              BIGSERIAL PRIMARY KEY,
    event_type      TEXT NOT NULL,               -- e.g. "Network Congestion"
    affected_nodes  TEXT[] NOT NULL,
    symptoms        TEXT[] NOT NULL,
    confidence      DOUBLE PRECISION NOT NULL,
    detected_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_events_detected_at ON events(detected_at DESC);

CREATE TABLE IF NOT EXISTS incidents (
    incident_id     TEXT PRIMARY KEY,
    severity        TEXT NOT NULL CHECK (severity IN ('LOW','MEDIUM','HIGH','CRITICAL')),
    category        TEXT NOT NULL DEFAULT 'NETWORK',
    root_cause      TEXT NOT NULL,
    affected_nodes  TEXT[] NOT NULL,
    symptoms        TEXT[] NOT NULL DEFAULT '{}',
    confidence      DOUBLE PRECISION NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL,
    status          TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE','RESOLVED'))
);

CREATE INDEX IF NOT EXISTS idx_incidents_created_at ON incidents(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_incidents_severity ON incidents(severity);

CREATE TABLE IF NOT EXISTS reports (
    incident_id   TEXT PRIMARY KEY REFERENCES incidents(incident_id),
    report_text   TEXT NOT NULL,
    generated_at  TIMESTAMPTZ NOT NULL
);
