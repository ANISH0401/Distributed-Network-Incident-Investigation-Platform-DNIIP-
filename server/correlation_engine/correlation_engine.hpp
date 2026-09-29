#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

#include "common/telemetry.hpp"
#include "server/common/models.hpp"
#include <algorithm>
namespace dniip::server {

// Thresholds tuned for the sample telemetry cadence (30s). Exposed as a
// struct (rather than magic numbers) so they can be overridden per
// deployment/tests without touching rule logic.
struct CorrelationThresholds {
    double packet_loss_pct = 10.0;
    double high_rtt_ms = 150.0;
    int retransmit_count = 5;
    int min_nodes_for_routing_failure = 2;
};

// Rule-based correlation engine (spec Component 4).
//
// Maintains a small rolling window of the latest sample per node and
// evaluates four independent rules against the current snapshot of all
// known nodes on every telemetry update. This is intentionally a simple,
// explainable expert-system style engine (not statistical/ML) so its
// output can be directly justified in an RCA report.
class CorrelationEngine {
public:
    explicit CorrelationEngine(CorrelationThresholds thresholds = {})
        : thresholds_(thresholds) {}

    // Feeds one telemetry sample in and returns any events newly detected
    // as a result (usually 0 or 1, but a single update can in principle
    // trigger more than one rule).
    std::vector<CorrelatedEvent> ingest(const TelemetrySample& sample) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_by_node_[sample.node_id] = sample;
        return evaluate_locked();
    }

    size_t known_node_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_by_node_.size();
    }

private:
    std::vector<CorrelatedEvent> evaluate_locked() {
        std::vector<CorrelatedEvent> events;
        if (auto e = rule_network_congestion_locked()) events.push_back(std::move(*e));
        if (auto e = rule_routing_failure_locked()) events.push_back(std::move(*e));
        if (auto e = rule_link_failure_locked()) events.push_back(std::move(*e));
        if (auto e = rule_dns_outage_locked()) events.push_back(std::move(*e));
        return events;
    }

    // Rule 1: packet loss + high RTT + TCP retransmissions -> Network Congestion.
    std::optional<CorrelatedEvent> rule_network_congestion_locked() const {
        CorrelatedEvent ev{EventType::kNetworkCongestion, {}, {}, 0.0};
        for (const auto& [node_id, s] : latest_by_node_) {
            const bool loss = s.packet_loss >= thresholds_.packet_loss_pct;
            const bool rtt = s.rtt_ms >= thresholds_.high_rtt_ms;
            const bool retrans = s.tcp_retransmits >= thresholds_.retransmit_count;
            if (loss && rtt && retrans) {
                ev.affected_nodes.push_back(node_id);
                ev.symptoms.push_back(node_id + ": packet loss " + std::to_string(s.packet_loss) +
                                       "%, RTT " + std::to_string(s.rtt_ms) + "ms, " +
                                       std::to_string(s.tcp_retransmits) + " retransmits");
            }
        }
        if (ev.affected_nodes.empty()) return std::nullopt;
        ev.confidence = 0.75;
        return ev;
    }

    // Rule 2: gateway unreachable on multiple nodes -> Routing Failure.
    std::optional<CorrelatedEvent> rule_routing_failure_locked() const {
        CorrelatedEvent ev{EventType::kRoutingFailure, {}, {}, 0.0};
        for (const auto& [node_id, s] : latest_by_node_) {
            if (!s.gateway_reachable) {
                ev.affected_nodes.push_back(node_id);
                ev.symptoms.push_back(node_id + ": gateway unreachable");
            }
        }
        if (static_cast<int>(ev.affected_nodes.size()) < thresholds_.min_nodes_for_routing_failure) {
            return std::nullopt;
        }
        ev.confidence = 0.85;
        return ev;
    }

    // Rule 3: any interface reporting operationally down -> Link Failure.
    std::optional<CorrelatedEvent> rule_link_failure_locked()
{
    CorrelatedEvent ev{
        EventType::kLinkFailure,
        {},
        {},
        0.0
    };

    for (const auto& [node_id, sample] : latest_by_node_)
    {
        for (const auto& iface : sample.interfaces)
        {
            // Ignore virtual interfaces
            if (iface.name == "lo" ||
                iface.name.rfind("docker", 0) == 0 ||
                iface.name.rfind("veth", 0) == 0 ||
                iface.name.rfind("br-", 0) == 0)
            {
                continue;
            }

            const std::string key =
                node_id + ":" + iface.name;

            const bool current_state = iface.up;

            auto it =
                previous_interface_state_.find(key);

            // First observation
            if (it == previous_interface_state_.end())
            {
                previous_interface_state_[key] =
                    current_state;
                continue;
            }

            const bool previous_state =
                it->second;

            previous_interface_state_[key] =
                current_state;

            // Trigger only on UP -> DOWN
            if (previous_state && !current_state)
            {
                ev.affected_nodes.push_back(node_id);

                ev.symptoms.push_back(
                    node_id +
                    ": interface " +
                    iface.name +
                    " transitioned UP -> DOWN");
            }
        }
    }

    if (ev.affected_nodes.empty())
    {
        return std::nullopt;
    }

    std::sort(
        ev.affected_nodes.begin(),
        ev.affected_nodes.end());

    ev.affected_nodes.erase(
        std::unique(
            ev.affected_nodes.begin(),
            ev.affected_nodes.end()),
        ev.affected_nodes.end());

    ev.confidence = 0.95;

    return ev;
}
    }

    // Rule 4: DNS resolution failing -> DNS Service Outage.
    std::optional<CorrelatedEvent> rule_dns_outage_locked() const {
        CorrelatedEvent ev{EventType::kDnsServiceOutage, {}, {}, 0.0};
        for (const auto& [node_id, s] : latest_by_node_) {
            if (!s.dns_resolvable) {
                ev.affected_nodes.push_back(node_id);
                ev.symptoms.push_back(node_id + ": DNS resolution failed");
            }
        }
        if (ev.affected_nodes.empty()) return std::nullopt;
        ev.confidence = 0.8;
        return ev;
    }

mutable std::mutex mutex_;

std::unordered_map<
    std::string,
    TelemetrySample
> latest_by_node_;

std::unordered_map<
    std::string,
    bool
> previous_interface_state_;

CorrelationThresholds thresholds_;
};

} // namespace dniip::server
// TEST LINE
