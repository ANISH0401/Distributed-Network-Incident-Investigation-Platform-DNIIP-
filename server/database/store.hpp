#pragma once

#include <string>
#include <vector>
#include <optional>

#include "common/telemetry.hpp"
#include "server/common/models.hpp"

namespace dniip::server {

struct StoredReport {
    std::string incident_id;
    std::string report_text;
    std::string generated_at;
};

// Persistence backend abstraction. SqliteStore (interim/local dev) and
// PostgresStore (production, spec Component 7) both implement this so
// TelemetryCollector, the REST API (server/api/), and other call sites are
// backend-agnostic.
class ITelemetryStore {
public:
    virtual ~ITelemetryStore() = default;

    // Writes (used by the telemetry ingestion pipeline).
    virtual void upsert_node(const std::string& node_id, const std::string& ts) = 0;
    virtual void insert_telemetry(const TelemetrySample& t) = 0;
    virtual void insert_incident(const Incident& inc) = 0;
    virtual void insert_report(const std::string& incident_id, const std::string& report_text,
                                const std::string& generated_at) = 0;
    virtual void set_incident_status(const std::string& incident_id, IncidentStatus status) = 0;

    // Reads (used by the REST API / dashboard).
    virtual std::vector<NodeSummary> list_nodes() = 0;
    virtual std::optional<TelemetrySample> latest_telemetry(const std::string& node_id) = 0;
    virtual std::vector<TelemetrySample> telemetry_history(const std::string& node_id, int limit) = 0;
    virtual std::vector<Incident> list_incidents(std::optional<IncidentStatus> status_filter, int limit) = 0;
    virtual std::optional<Incident> get_incident(const std::string& incident_id) = 0;
    virtual std::optional<StoredReport> get_report(const std::string& incident_id) = 0;
};

} // namespace dniip::server
