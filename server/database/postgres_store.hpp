#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <libpq-fe.h>
#include <nlohmann/json.hpp>

#include "common/telemetry.hpp"
#include "server/common/models.hpp"
#include "server/database/store.hpp"

namespace dniip::server {

// PostgreSQL-backed persistence (spec Component 7). Schema lives in
// server/database/schema.sql and is applied automatically on construction
// (idempotent CREATE TABLE IF NOT EXISTS), so a fresh Postgres instance is
// ready to use with no separate migration step.
//
// Uses libpq's text-protocol PQexecParams (not the binary protocol) for
// simplicity; the query volume here (one write per telemetry sample every
// ~30s per node, incidents/reports far rarer, dashboard reads on-demand)
// does not justify the complexity of prepared statements with binary
// parameter encoding.
class PostgresStore : public ITelemetryStore {
public:
    // conninfo: libpq connection string, e.g.
    // "host=localhost port=5432 dbname=dniip user=dniip password=..."
    explicit PostgresStore(const std::string& conninfo) {
        conn_ = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
            throw std::runtime_error("PostgreSQL connection failed: " + err);
        }
        apply_schema();
    }

    ~PostgresStore() override {
        if (conn_) PQfinish(conn_);
    }

    PostgresStore(const PostgresStore&) = delete;
    PostgresStore& operator=(const PostgresStore&) = delete;

    void upsert_node(const std::string& node_id, const std::string& ts) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO nodes(node_id, first_seen, last_seen) VALUES ($1, $2, $2) "
            "ON CONFLICT (node_id) DO UPDATE SET last_seen = EXCLUDED.last_seen;";
        const char* params[2] = {node_id.c_str(), ts.c_str()};
        exec_params(sql, 2, params);
    }

    void insert_telemetry(const TelemetrySample& t) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO telemetry(node_id, \"timestamp\", cpu_usage, memory_usage, packet_loss, "
            "rtt_ms, gateway_reachable, dns_resolvable, tcp_retransmits, interfaces_json) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10);";

        std::string cpu = std::to_string(t.cpu_usage);
        std::string mem = std::to_string(t.memory_usage);
        std::string loss = std::to_string(t.packet_loss);
        std::string rtt = std::to_string(t.rtt_ms);
        std::string gw = t.gateway_reachable ? "t" : "f";
        std::string dns = t.dns_resolvable ? "t" : "f";
        std::string retrans = std::to_string(t.tcp_retransmits);
        std::string interfaces_json = nlohmann::json(t.interfaces).dump();

        const char* params[10] = {
            t.node_id.c_str(), t.timestamp.c_str(), cpu.c_str(), mem.c_str(), loss.c_str(),
            rtt.c_str(), gw.c_str(), dns.c_str(), retrans.c_str(), interfaces_json.c_str(),
        };
        exec_params(sql, 10, params);
    }

    // Records a correlated event (distinct from the incident it may roll
    // up into) in the events table. Not part of ITelemetryStore since
    // SqliteStore's interim schema predates the events/incidents split;
    // callers that want event-level history call this directly on a
    // PostgresStore instance.
    void insert_event(const CorrelatedEvent& ev) {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO events(event_type, affected_nodes, symptoms, confidence) "
            "VALUES ($1, $2, $3, $4);";
        std::string nodes_arr = to_pg_text_array(ev.affected_nodes);
        std::string symptoms_arr = to_pg_text_array(ev.symptoms);
        std::string confidence = std::to_string(ev.confidence);
        const char* type_str = to_string(ev.type);
        const char* params[4] = {type_str, nodes_arr.c_str(), symptoms_arr.c_str(), confidence.c_str()};
        exec_params(sql, 4, params);
    }

    void insert_incident(const Incident& inc) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO incidents(incident_id, severity, category, root_cause, affected_nodes, "
            "symptoms, confidence, created_at, status) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9);";
        std::string nodes_arr = to_pg_text_array(inc.affected_nodes);
        std::string symptoms_arr = to_pg_text_array(inc.symptoms);
        std::string confidence = std::to_string(inc.confidence);
        const char* severity_str = to_string(inc.severity);
        const char* status_str = to_string(inc.status);
        const char* params[9] = {
            inc.incident_id.c_str(), severity_str, inc.category.c_str(), inc.root_cause.c_str(),
            nodes_arr.c_str(), symptoms_arr.c_str(), confidence.c_str(), inc.created_at.c_str(), status_str,
        };
        exec_params(sql, 9, params);
    }

    void insert_report(const std::string& incident_id, const std::string& report_text,
                        const std::string& generated_at) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO reports(incident_id, report_text, generated_at) VALUES ($1,$2,$3) "
            "ON CONFLICT (incident_id) DO UPDATE SET report_text = EXCLUDED.report_text, "
            "generated_at = EXCLUDED.generated_at;";
        const char* params[3] = {incident_id.c_str(), report_text.c_str(), generated_at.c_str()};
        exec_params(sql, 3, params);
    }

    void set_incident_status(const std::string& incident_id, IncidentStatus status) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "UPDATE incidents SET status = $1 WHERE incident_id = $2;";
        const char* status_str = to_string(status);
        const char* params[2] = {status_str, incident_id.c_str()};
        exec_params(sql, 2, params);
    }

    std::vector<NodeSummary> list_nodes() override {
        std::lock_guard<std::mutex> lock(mutex_);
        PGresult* res = PQexecParams(conn_, "SELECT node_id, first_seen, last_seen FROM nodes ORDER BY node_id;",
                                      0, nullptr, nullptr, nullptr, nullptr, 0);
        check_tuples(res);
        std::vector<NodeSummary> out;
        for (int i = 0; i < PQntuples(res); ++i) {
            out.push_back(NodeSummary{PQgetvalue(res, i, 0), PQgetvalue(res, i, 1), PQgetvalue(res, i, 2)});
        }
        PQclear(res);
        return out;
    }

    std::optional<TelemetrySample> latest_telemetry(const std::string& node_id) override {
        auto history = telemetry_history(node_id, 1);
        if (history.empty()) return std::nullopt;
        return history.front();
    }

    std::vector<TelemetrySample> telemetry_history(const std::string& node_id, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "SELECT node_id, \"timestamp\", cpu_usage, memory_usage, packet_loss, rtt_ms, "
            "gateway_reachable, dns_resolvable, tcp_retransmits, interfaces_json "
            "FROM telemetry WHERE node_id = $1 ORDER BY id DESC LIMIT $2;";
        std::string limit_str = std::to_string(limit);
        const char* params[2] = {node_id.c_str(), limit_str.c_str()};
        PGresult* res = PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0);
        check_tuples(res);

        std::vector<TelemetrySample> out;
        for (int i = 0; i < PQntuples(res); ++i) {
            TelemetrySample t;
            t.node_id = PQgetvalue(res, i, 0);
            t.timestamp = PQgetvalue(res, i, 1);
            t.cpu_usage = std::stod(PQgetvalue(res, i, 2));
            t.memory_usage = std::stod(PQgetvalue(res, i, 3));
            t.packet_loss = std::stod(PQgetvalue(res, i, 4));
            t.rtt_ms = std::stod(PQgetvalue(res, i, 5));
            t.gateway_reachable = std::string(PQgetvalue(res, i, 6)) == "t";
            t.dns_resolvable = std::string(PQgetvalue(res, i, 7)) == "t";
            t.tcp_retransmits = std::stoi(PQgetvalue(res, i, 8));
            try {
                t.interfaces = nlohmann::json::parse(PQgetvalue(res, i, 9)).get<std::vector<InterfaceStats>>();
            } catch (...) {}
            out.push_back(std::move(t));
        }
        PQclear(res);
        return out;
    }

    std::vector<Incident> list_incidents(std::optional<IncidentStatus> status_filter, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        PGresult* res;
        std::string limit_str = std::to_string(limit);
        if (status_filter) {
            const char* sql =
                "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
                "confidence, created_at, status FROM incidents WHERE status = $1 "
                "ORDER BY created_at DESC LIMIT $2;";
            const char* status_str = to_string(*status_filter);
            const char* params[2] = {status_str, limit_str.c_str()};
            res = PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0);
        } else {
            const char* sql =
                "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
                "confidence, created_at, status FROM incidents ORDER BY created_at DESC LIMIT $1;";
            const char* params[1] = {limit_str.c_str()};
            res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
        }
        check_tuples(res);

        std::vector<Incident> out;
        for (int i = 0; i < PQntuples(res); ++i) {
            Incident inc;
            inc.incident_id = PQgetvalue(res, i, 0);
            std::string severity_str = PQgetvalue(res, i, 1);
            inc.severity = severity_str == "CRITICAL" ? Severity::kCritical
                         : severity_str == "HIGH" ? Severity::kHigh
                         : severity_str == "MEDIUM" ? Severity::kMedium
                         : Severity::kLow;
            inc.category = PQgetvalue(res, i, 2);
            inc.root_cause = PQgetvalue(res, i, 3);
            inc.affected_nodes = from_pg_text_array(PQgetvalue(res, i, 4));
            inc.symptoms = from_pg_text_array(PQgetvalue(res, i, 5));
            inc.confidence = std::stod(PQgetvalue(res, i, 6));
            inc.created_at = PQgetvalue(res, i, 7);
            inc.status = incident_status_from_string(PQgetvalue(res, i, 8));
            out.push_back(std::move(inc));
        }
        PQclear(res);
        return out;
    }

    std::optional<Incident> get_incident(const std::string& incident_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
            "confidence, created_at, status FROM incidents WHERE incident_id = $1;";
        const char* params[1] = {incident_id.c_str()};
        PGresult* res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
        check_tuples(res);

        std::optional<Incident> out;
        if (PQntuples(res) > 0) {
            Incident inc;
            inc.incident_id = PQgetvalue(res, 0, 0);
            std::string severity_str = PQgetvalue(res, 0, 1);
            inc.severity = severity_str == "CRITICAL" ? Severity::kCritical
                         : severity_str == "HIGH" ? Severity::kHigh
                         : severity_str == "MEDIUM" ? Severity::kMedium
                         : Severity::kLow;
            inc.category = PQgetvalue(res, 0, 2);
            inc.root_cause = PQgetvalue(res, 0, 3);
            inc.affected_nodes = from_pg_text_array(PQgetvalue(res, 0, 4));
            inc.symptoms = from_pg_text_array(PQgetvalue(res, 0, 5));
            inc.confidence = std::stod(PQgetvalue(res, 0, 6));
            inc.created_at = PQgetvalue(res, 0, 7);
            inc.status = incident_status_from_string(PQgetvalue(res, 0, 8));
            out = std::move(inc);
        }
        PQclear(res);
        return out;
    }

    std::optional<StoredReport> get_report(const std::string& incident_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "SELECT incident_id, report_text, generated_at FROM reports WHERE incident_id = $1;";
        const char* params[1] = {incident_id.c_str()};
        PGresult* res = PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0);
        check_tuples(res);

        std::optional<StoredReport> out;
        if (PQntuples(res) > 0) {
            out = StoredReport{PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1), PQgetvalue(res, 0, 2)};
        }
        PQclear(res);
        return out;
    }

private:
    // Kept as a raw string literal (rather than reading server/database/schema.sql
    // off disk) so the store has no runtime dependency on the source tree's
    // layout post-install. The two are kept byte-for-byte identical; schema.sql
    // is the copy meant for `psql -f` / manual inspection.
    void apply_schema() {
        exec(
            "CREATE TABLE IF NOT EXISTS nodes ("
            "    node_id     TEXT PRIMARY KEY,"
            "    first_seen  TIMESTAMPTZ NOT NULL,"
            "    last_seen   TIMESTAMPTZ NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "    id                 BIGSERIAL PRIMARY KEY,"
            "    node_id            TEXT NOT NULL REFERENCES nodes(node_id),"
            "    \"timestamp\"        TIMESTAMPTZ NOT NULL,"
            "    cpu_usage          DOUBLE PRECISION NOT NULL,"
            "    memory_usage       DOUBLE PRECISION NOT NULL,"
            "    packet_loss        DOUBLE PRECISION NOT NULL,"
            "    rtt_ms             DOUBLE PRECISION NOT NULL,"
            "    gateway_reachable  BOOLEAN NOT NULL,"
            "    dns_resolvable     BOOLEAN NOT NULL,"
            "    tcp_retransmits    INTEGER NOT NULL,"
            "    interfaces_json    TEXT NOT NULL DEFAULT '[]',"
            "    created_at         TIMESTAMPTZ NOT NULL DEFAULT now()"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_telemetry_node_ts ON telemetry(node_id, \"timestamp\" DESC);"
            "CREATE TABLE IF NOT EXISTS events ("
            "    id              BIGSERIAL PRIMARY KEY,"
            "    event_type      TEXT NOT NULL,"
            "    affected_nodes  TEXT[] NOT NULL,"
            "    symptoms        TEXT[] NOT NULL,"
            "    confidence      DOUBLE PRECISION NOT NULL,"
            "    detected_at     TIMESTAMPTZ NOT NULL DEFAULT now()"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_events_detected_at ON events(detected_at DESC);"
            "CREATE TABLE IF NOT EXISTS incidents ("
            "    incident_id     TEXT PRIMARY KEY,"
            "    severity        TEXT NOT NULL CHECK (severity IN ('LOW','MEDIUM','HIGH','CRITICAL')),"
            "    category        TEXT NOT NULL DEFAULT 'NETWORK',"
            "    root_cause      TEXT NOT NULL,"
            "    affected_nodes  TEXT[] NOT NULL,"
            "    symptoms        TEXT[] NOT NULL DEFAULT '{}',"
            "    confidence      DOUBLE PRECISION NOT NULL,"
            "    created_at      TIMESTAMPTZ NOT NULL,"
            "    status          TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE','RESOLVED'))"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_incidents_created_at ON incidents(created_at DESC);"
            "CREATE INDEX IF NOT EXISTS idx_incidents_severity ON incidents(severity);"
            "CREATE TABLE IF NOT EXISTS reports ("
            "    incident_id   TEXT PRIMARY KEY REFERENCES incidents(incident_id),"
            "    report_text   TEXT NOT NULL,"
            "    generated_at  TIMESTAMPTZ NOT NULL"
            ");"
        );
    }

    static std::string to_pg_text_array(const std::vector<std::string>& items) {
        // Builds a Postgres array literal, e.g. {"node-a","node-b"}.
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) oss << ",";
            oss << "\"";
            for (char c : items[i]) {
                if (c == '"' || c == '\\') oss << '\\';
                oss << c;
            }
            oss << "\"";
        }
        oss << "}";
        return oss.str();
    }

    // Parses a Postgres TEXT[] literal as returned in text-protocol results,
    // e.g. {"node-a","node-b"} or {} for empty. Does not attempt to handle
    // every edge case of Postgres array literal escaping (nested braces,
    // embedded commas requiring quotes) beyond escaped quotes/backslashes,
    // which is all to_pg_text_array ever produces.
    static std::vector<std::string> from_pg_text_array(const std::string& literal) {
        std::vector<std::string> out;
        if (literal.size() < 2 || literal.front() != '{' || literal.back() != '}') return out;
        const std::string inner = literal.substr(1, literal.size() - 2);
        if (inner.empty()) return out;

        std::string current;
        bool in_quotes = false;
        for (size_t i = 0; i < inner.size(); ++i) {
            char c = inner[i];
            if (c == '\\' && i + 1 < inner.size()) {
                current += inner[++i];
            } else if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                out.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        out.push_back(current);
        return out;
    }

    void exec_params(const char* sql, int n_params, const char* const* params) {
        PGresult* res = PQexecParams(conn_, sql, n_params, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL query failed: " + err);
        }
        PQclear(res);
    }

    void check_tuples(PGresult* res) {
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL query failed: " + err);
        }
    }

    void exec(const std::string& sql) {
        PGresult* res = PQexec(conn_, sql.c_str());
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("PostgreSQL schema apply failed: " + err);
        }
        PQclear(res);
    }

    PGconn* conn_ = nullptr;
    std::mutex mutex_;
};

} // namespace dniip::server
