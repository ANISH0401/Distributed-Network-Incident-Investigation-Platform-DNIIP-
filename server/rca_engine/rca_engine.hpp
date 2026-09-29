#pragma once

#include <vector>
#include <string>
#include <sstream>

#include "server/common/models.hpp"
#include "server/incident_engine/incident_engine.hpp"

namespace dniip::server {

// Component 6: expands an incident's root cause into a human-readable
// investigation report with actionable recommendations. Recommendations
// are keyed off root cause category — this is deliberately a lookup table
// (not inference) so reports stay deterministic and explainable.
class RcaEngine {
public:
    RcaReport generate(const Incident& incident) const {
        RcaReport report;
        report.incident_id = incident.incident_id;
        report.root_cause = incident.root_cause;
        report.confidence = incident.confidence;
        report.symptoms = incident.symptoms;
        report.recommendations = recommendations_for(incident.root_cause);
        report.generated_at = iso8601_now();
        return report;
    }

    std::string render_text(const RcaReport& r) const {
        std::ostringstream oss;
        oss << "INCIDENT REPORT\n";
        oss << "Incident ID:\n" << r.incident_id << "\n\n";
        oss << "Symptoms:\n";
        for (const auto& s : r.symptoms) oss << "  - " << s << "\n";
        oss << "\nRoot Cause:\n" << r.root_cause << "\n\n";
        oss << "Confidence:\n" << static_cast<int>(r.confidence * 100) << "%\n\n";
        oss << "Recommendations:\n";
        for (const auto& rec : r.recommendations) oss << "  - " << rec << "\n";
        return oss.str();
    }

private:
    static std::vector<std::string> recommendations_for(const std::string& root_cause) {
        if (root_cause == to_string(EventType::kRoutingFailure)) {
            return {
                "Verify default gateway",
                "Check static routes",
                "Validate BGP advertisements",
                "Inspect router logs",
            };
        }
        if (root_cause == to_string(EventType::kNetworkCongestion)) {
            return {
                "Check interface utilization and QoS policies",
                "Look for TCP retransmit/duplicate-ACK storms",
                "Review recent traffic pattern or DDoS indicators",
                "Consider link capacity upgrade or traffic shaping",
            };
        }
        if (root_cause == to_string(EventType::kLinkFailure)) {
            return {
                "Physically inspect cabling/SFP on affected interface",
                "Check switch port status and error counters",
                "Verify NIC driver/firmware health",
                "Fail over to redundant link if available",
            };
        }
        if (root_cause == to_string(EventType::kDnsServiceOutage)) {
            return {
                "Verify DNS server process health and reachability",
                "Check /etc/resolv.conf and resolver configuration",
                "Test resolution against a secondary DNS server",
                "Review DNS server logs for query failures",
            };
        }
        return {"Escalate to on-call network engineer for manual investigation"};
    }
};

} // namespace dniip::server
