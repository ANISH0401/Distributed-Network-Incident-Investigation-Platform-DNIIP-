#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "common/telemetry.hpp"
#include "server/common/models.hpp"
#include "server/database/store.hpp"

namespace dniip::server {

// Interim / local-dev persistence backend. Same shape of data as
// PostgresStore (nodes/telemetry/incidents/reports) via the shared
// ITelemetryStore interface, so either can be selected at startup.
class SqliteStore : public ITelemetryStore {
public:
    explicit SqliteStore(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw std::runtime_error("Failed to open server SQLite DB: " + std::string(sqlite3_errmsg(db_)));
        }
        exec(
            "CREATE TABLE IF NOT EXISTS nodes ("
            "  node_id TEXT PRIMARY KEY,"
            "  first_seen TEXT NOT NULL,"
            "  last_seen TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS telemetry ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  node_id TEXT NOT NULL,"
            "  timestamp TEXT NOT NULL,"
            "  cpu_usage REAL, memory_usage REAL, packet_loss REAL,"
            "  rtt_ms REAL, gateway_reachable INTEGER, dns_resolvable INTEGER,"
            "  tcp_retransmits INTEGER,"
            "  interfaces_json TEXT NOT NULL DEFAULT '[]',"
            "  FOREIGN KEY(node_id) REFERENCES nodes(node_id)"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_telemetry_node_ts ON telemetry(node_id, timestamp);"
            "CREATE TABLE IF NOT EXISTS incidents ("
            "  incident_id TEXT PRIMARY KEY,"
            "  severity TEXT NOT NULL,"
            "  category TEXT NOT NULL,"
            "  root_cause TEXT NOT NULL,"
            "  affected_nodes TEXT NOT NULL,"
            "  symptoms TEXT NOT NULL DEFAULT '[]',"
            "  confidence REAL NOT NULL,"
            "  created_at TEXT NOT NULL,"
            "  status TEXT NOT NULL DEFAULT 'ACTIVE'"
            ");"
            "CREATE TABLE IF NOT EXISTS reports ("
            "  incident_id TEXT PRIMARY KEY,"
            "  report_text TEXT NOT NULL,"
            "  generated_at TEXT NOT NULL,"
            "  FOREIGN KEY(incident_id) REFERENCES incidents(incident_id)"
            ");"
        );
    }

    ~SqliteStore() override { if (db_) sqlite3_close(db_); }

    void upsert_node(const std::string& node_id, const std::string& ts) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO nodes(node_id, first_seen, last_seen) VALUES (?, ?, ?) "
            "ON CONFLICT(node_id) DO UPDATE SET last_seen=excluded.last_seen;";
        sqlite3_stmt* stmt = prepare(sql);
        sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, ts.c_str(), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
    }

    void insert_telemetry(const TelemetrySample& t) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO telemetry(node_id, timestamp, cpu_usage, memory_usage, packet_loss, "
            "rtt_ms, gateway_reachable, dns_resolvable, tcp_retransmits, interfaces_json) "
            "VALUES (?,?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = prepare(sql);
        sqlite3_bind_text(stmt, 1, t.node_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, t.timestamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, t.cpu_usage);
        sqlite3_bind_double(stmt, 4, t.memory_usage);
        sqlite3_bind_double(stmt, 5, t.packet_loss);
        sqlite3_bind_double(stmt, 6, t.rtt_ms);
        sqlite3_bind_int(stmt, 7, t.gateway_reachable ? 1 : 0);
        sqlite3_bind_int(stmt, 8, t.dns_resolvable ? 1 : 0);
        sqlite3_bind_int(stmt, 9, t.tcp_retransmits);
        std::string interfaces_json = nlohmann::json(t.interfaces).dump();
        sqlite3_bind_text(stmt, 10, interfaces_json.c_str(), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
    }

    void insert_incident(const Incident& inc) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string nodes_csv = join_csv(inc.affected_nodes);
        std::string symptoms_json = nlohmann::json(inc.symptoms).dump();
        const char* sql =
            "INSERT INTO incidents(incident_id, severity, category, root_cause, affected_nodes, "
            "symptoms, confidence, created_at, status) VALUES (?,?,?,?,?,?,?,?,?);";
        sqlite3_stmt* stmt = prepare(sql);
        sqlite3_bind_text(stmt, 1, inc.incident_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, to_string(inc.severity), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, inc.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, inc.root_cause.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, nodes_csv.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, symptoms_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 7, inc.confidence);
        sqlite3_bind_text(stmt, 8, inc.created_at.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, to_string(inc.status), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
    }

    void insert_report(const std::string& incident_id, const std::string& report_text,
                        const std::string& generated_at) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql =
            "INSERT INTO reports(incident_id, report_text, generated_at) VALUES (?,?,?) "
            "ON CONFLICT(incident_id) DO UPDATE SET report_text=excluded.report_text, "
            "generated_at=excluded.generated_at;";
        sqlite3_stmt* stmt = prepare(sql);
        sqlite3_bind_text(stmt, 1, incident_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, report_text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, generated_at.c_str(), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
    }

    void set_incident_status(const std::string& incident_id, IncidentStatus status) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* sql = "UPDATE incidents SET status = ? WHERE incident_id = ?;";
        sqlite3_stmt* stmt = prepare(sql);
        sqlite3_bind_text(stmt, 1, to_string(status), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, incident_id.c_str(), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
    }

    std::vector<NodeSummary> list_nodes() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NodeSummary> out;
        sqlite3_stmt* stmt = prepare("SELECT node_id, first_seen, last_seen FROM nodes ORDER BY node_id;");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            NodeSummary n;
            n.node_id = text_col(stmt, 0);
            n.first_seen = text_col(stmt, 1);
            n.last_seen = text_col(stmt, 2);
            out.push_back(std::move(n));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    std::optional<TelemetrySample> latest_telemetry(const std::string& node_id) override {
        auto history = telemetry_history(node_id, 1);
        if (history.empty()) return std::nullopt;
        return history.front();
    }

    std::vector<TelemetrySample> telemetry_history(const std::string& node_id, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TelemetrySample> out;
        sqlite3_stmt* stmt = prepare(
            "SELECT node_id, timestamp, cpu_usage, memory_usage, packet_loss, rtt_ms, "
            "gateway_reachable, dns_resolvable, tcp_retransmits, interfaces_json "
            "FROM telemetry WHERE node_id = ? ORDER BY id DESC LIMIT ?;");
        sqlite3_bind_text(stmt, 1, node_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            TelemetrySample t;
            t.node_id = text_col(stmt, 0);
            t.timestamp = text_col(stmt, 1);
            t.cpu_usage = sqlite3_column_double(stmt, 2);
            t.memory_usage = sqlite3_column_double(stmt, 3);
            t.packet_loss = sqlite3_column_double(stmt, 4);
            t.rtt_ms = sqlite3_column_double(stmt, 5);
            t.gateway_reachable = sqlite3_column_int(stmt, 6) != 0;
            t.dns_resolvable = sqlite3_column_int(stmt, 7) != 0;
            t.tcp_retransmits = sqlite3_column_int(stmt, 8);
            try {
                t.interfaces = nlohmann::json::parse(text_col(stmt, 9)).get<std::vector<InterfaceStats>>();
            } catch (...) { /* leave empty on malformed/legacy rows */ }
            out.push_back(std::move(t));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    std::vector<Incident> list_incidents(std::optional<IncidentStatus> status_filter, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Incident> out;
        sqlite3_stmt* stmt;
        if (status_filter) {
            stmt = prepare(
                "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
                "confidence, created_at, status FROM incidents WHERE status = ? "
                "ORDER BY created_at DESC LIMIT ?;");
            sqlite3_bind_text(stmt, 1, to_string(*status_filter), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limit);
        } else {
            stmt = prepare(
                "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
                "confidence, created_at, status FROM incidents ORDER BY created_at DESC LIMIT ?;");
            sqlite3_bind_int(stmt, 1, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            out.push_back(incident_from_row(stmt));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    std::optional<Incident> get_incident(const std::string& incident_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = prepare(
            "SELECT incident_id, severity, category, root_cause, affected_nodes, symptoms, "
            "confidence, created_at, status FROM incidents WHERE incident_id = ?;");
        sqlite3_bind_text(stmt, 1, incident_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<Incident> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = incident_from_row(stmt);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    std::optional<StoredReport> get_report(const std::string& incident_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = prepare(
            "SELECT incident_id, report_text, generated_at FROM reports WHERE incident_id = ?;");
        sqlite3_bind_text(stmt, 1, incident_id.c_str(), -1, SQLITE_TRANSIENT);
        std::optional<StoredReport> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            StoredReport r;
            r.incident_id = text_col(stmt, 0);
            r.report_text = text_col(stmt, 1);
            r.generated_at = text_col(stmt, 2);
            result = std::move(r);
        }
        sqlite3_finalize(stmt);
        return result;
    }

private:
    static std::string join_csv(const std::vector<std::string>& items) {
        std::string out;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += ",";
            out += items[i];
        }
        return out;
    }

    static std::vector<std::string> split_csv(const std::string& csv) {
        std::vector<std::string> out;
        if (csv.empty()) return out;
        size_t start = 0;
        while (start <= csv.size()) {
            size_t comma = csv.find(',', start);
            if (comma == std::string::npos) {
                out.push_back(csv.substr(start));
                break;
            }
            out.push_back(csv.substr(start, comma - start));
            start = comma + 1;
        }
        return out;
    }

    static std::string text_col(sqlite3_stmt* stmt, int col) {
        const unsigned char* text = sqlite3_column_text(stmt, col);
        return text ? reinterpret_cast<const char*>(text) : "";
    }

    Incident incident_from_row(sqlite3_stmt* stmt) {
        Incident inc;
        inc.incident_id = text_col(stmt, 0);
        std::string severity_str = text_col(stmt, 1);
        inc.severity = severity_str == "CRITICAL" ? Severity::kCritical
                     : severity_str == "HIGH" ? Severity::kHigh
                     : severity_str == "MEDIUM" ? Severity::kMedium
                     : Severity::kLow;
        inc.category = text_col(stmt, 2);
        inc.root_cause = text_col(stmt, 3);
        inc.affected_nodes = split_csv(text_col(stmt, 4));
        try {
            inc.symptoms = nlohmann::json::parse(text_col(stmt, 5)).get<std::vector<std::string>>();
        } catch (...) {}
        inc.confidence = sqlite3_column_double(stmt, 6);
        inc.created_at = text_col(stmt, 7);
        inc.status = incident_status_from_string(text_col(stmt, 8));
        return inc;
    }

    sqlite3_stmt* prepare(const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sqlite3_prepare_v2 failed: " + std::string(sqlite3_errmsg(db_)));
        }
        return stmt;
    }

    void step_and_finalize(sqlite3_stmt* stmt) {
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            throw std::runtime_error("sqlite3_step failed: " + std::string(sqlite3_errmsg(db_)));
        }
    }

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("sqlite3_exec failed: " + msg);
        }
    }

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

} // namespace dniip::server
