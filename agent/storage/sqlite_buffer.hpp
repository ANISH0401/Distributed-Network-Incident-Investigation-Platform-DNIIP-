#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "common/telemetry.hpp"

namespace dniip::agent {

// Component: SQLite store-and-forward buffer (spec Component 1).
//
// The agent always writes a telemetry sample here first, then attempts
// live transmission. On success the record is marked synchronized; on
// failure (or agent restart with connectivity down) unsynced records are
// retried on the next successful connection, oldest first.
class SqliteBuffer {
public:
    explicit SqliteBuffer(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw std::runtime_error("Failed to open agent SQLite DB: " + std::string(sqlite3_errmsg(db_)));
        }
        exec(
            "CREATE TABLE IF NOT EXISTS telemetry_buffer ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  payload TEXT NOT NULL,"
            "  created_at TEXT NOT NULL,"
            "  synced INTEGER NOT NULL DEFAULT 0"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_buffer_synced ON telemetry_buffer(synced);"
        );
    }

    ~SqliteBuffer() { if (db_) sqlite3_close(db_); }

    // Buffers a sample locally; returns the row id for later mark_synced().
    int64_t enqueue(const TelemetrySample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json payload = sample;
        const std::string body = payload.dump();

        sqlite3_stmt* stmt = prepare(
            "INSERT INTO telemetry_buffer(payload, created_at, synced) VALUES (?, ?, 0);");
        sqlite3_bind_text(stmt, 1, body.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, sample.timestamp.c_str(), -1, SQLITE_TRANSIENT);
        step_and_finalize(stmt);
        return sqlite3_last_insert_rowid(db_);
    }

    struct PendingRecord {
        int64_t id;
        TelemetrySample sample;
    };

    // Returns up to `limit` unsynced records, oldest first.
    std::vector<PendingRecord> pending(int limit = 100) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PendingRecord> out;

        sqlite3_stmt* stmt = prepare(
            "SELECT id, payload FROM telemetry_buffer WHERE synced = 0 ORDER BY id ASC LIMIT ?;");
        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(stmt, 0);
            const unsigned char* text = sqlite3_column_text(stmt, 1);
            std::string payload(reinterpret_cast<const char*>(text));
            try {
                PendingRecord rec;
                rec.id = id;
                rec.sample = nlohmann::json::parse(payload).get<TelemetrySample>();
                out.push_back(std::move(rec));
            } catch (...) {
                // Skip malformed rows rather than blocking the whole retry queue.
            }
        }
        sqlite3_finalize(stmt);
        return out;
    }

    void mark_synced(int64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = prepare("UPDATE telemetry_buffer SET synced = 1 WHERE id = ?;");
        sqlite3_bind_int64(stmt, 1, id);
        step_and_finalize(stmt);
    }

    // Deletes synced rows older than everything currently pending, to keep
    // the local DB from growing unbounded. Call periodically, not per-sample.
    void vacuum_synced(size_t keep_last_n = 1000) {
        std::lock_guard<std::mutex> lock(mutex_);
        sqlite3_stmt* stmt = prepare(
            "DELETE FROM telemetry_buffer WHERE synced = 1 AND id NOT IN ("
            "  SELECT id FROM telemetry_buffer WHERE synced = 1 ORDER BY id DESC LIMIT ?"
            ");");
        sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(keep_last_n));
        step_and_finalize(stmt);
    }

private:
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

} // namespace dniip::agent
