#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace dniip::server {

enum class Severity { kLow, kMedium, kHigh, kCritical };

inline const char* to_string(Severity s) {
    switch (s) {
        case Severity::kLow: return "LOW";
        case Severity::kMedium: return "MEDIUM";
        case Severity::kHigh: return "HIGH";
        case Severity::kCritical: return "CRITICAL";
    }
    return "UNKNOWN";
}

enum class IncidentStatus { kActive, kResolved };

inline const char* to_string(IncidentStatus s) {
    return s == IncidentStatus::kActive ? "ACTIVE" : "RESOLVED";
}

inline IncidentStatus incident_status_from_string(const std::string& s) {
    return s == "RESOLVED" ? IncidentStatus::kResolved : IncidentStatus::kActive;
}

enum class EventType {
    kNetworkCongestion,
    kRoutingFailure,
    kLinkFailure,
    kDnsServiceOutage,
};

inline const char* to_string(EventType e) {
    switch (e) {
        case EventType::kNetworkCongestion: return "Network Congestion";
        case EventType::kRoutingFailure: return "Routing Failure";
        case EventType::kLinkFailure: return "Link Failure";
        case EventType::kDnsServiceOutage: return "DNS Service Outage";
    }
    return "Unknown";
}

// A correlated event derived from one or more nodes' telemetry over a
// short sliding window. Multiple events can later be grouped into a
// single incident by the incident engine.
struct CorrelatedEvent {
    EventType type;
    std::vector<std::string> affected_nodes;
    std::vector<std::string> symptoms;   // human-readable evidence lines
    double confidence = 0.0;             // 0..1
};

struct Incident
{
    std::string incident_id;

    Severity severity;

    std::string category;

    std::string root_cause;

    std::vector<std::string> affected_nodes;

    std::vector<std::string> symptoms;

    double confidence;

    std::string created_at;

    std::string first_seen;

    std::string last_seen;

    uint64_t occurrence_count = 1;

    IncidentStatus status =
        IncidentStatus::kActive;
};

struct NodeSummary {
    std::string node_id;
    std::string first_seen;
    std::string last_seen;
};

struct RcaReport {
    std::string incident_id;
    std::string root_cause;
    double confidence = 0.0;
    std::vector<std::string> symptoms;
    std::vector<std::string> recommendations;
    std::string generated_at;
};

} // namespace dniip::server
