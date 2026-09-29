#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace dniip::server {

// Component 9: in-process metrics registry rendered as Prometheus text
// exposition format by HttpMetricsServer. Deliberately minimal (counters +
// gauges, no histograms) — the metrics the spec calls for (agent
// availability, incident count, packet loss, API latency, node health
// score, connected agents) are all adequately served by counters/gauges.
class MetricsRegistry {
public:
    void inc_incident_count() { incident_count_.fetch_add(1, std::memory_order_relaxed); }
    void inc_connected_agents() { connected_agents_.fetch_add(1, std::memory_order_relaxed); }
    void dec_connected_agents() { connected_agents_.fetch_sub(1, std::memory_order_relaxed); }
    void inc_telemetry_samples() { telemetry_samples_total_.fetch_add(1, std::memory_order_relaxed); }

    void observe_processing_latency_ms(double ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        processing_latency_sum_ms_ += ms;
        processing_latency_count_ += 1;
    }

    void set_node_packet_loss(const std::string& node_id, double pct) {
        std::lock_guard<std::mutex> lock(mutex_);
        packet_loss_by_node_[node_id] = pct;
    }

    void set_node_health_score(const std::string& node_id, double score) {
        std::lock_guard<std::mutex> lock(mutex_);
        health_score_by_node_[node_id] = score;
    }

    void mark_node_seen(const std::string& node_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        known_nodes_.insert(node_id);
    }

    // Renders all metrics in Prometheus text exposition format
    // (https://prometheus.io/docs/instrumenting/exposition_formats/).
    std::string render() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;

        oss << "# HELP dniip_incident_count_total Total incidents generated since server start\n";
        oss << "# TYPE dniip_incident_count_total counter\n";
        oss << "dniip_incident_count_total " << incident_count_.load() << "\n";

        oss << "# HELP dniip_connected_agents Number of currently connected agent TCP sessions\n";
        oss << "# TYPE dniip_connected_agents gauge\n";
        oss << "dniip_connected_agents " << connected_agents_.load() << "\n";

        oss << "# HELP dniip_telemetry_samples_total Total telemetry samples processed\n";
        oss << "# TYPE dniip_telemetry_samples_total counter\n";
        oss << "dniip_telemetry_samples_total " << telemetry_samples_total_.load() << "\n";

        oss << "# HELP dniip_agent_availability_ratio Known nodes with an active connection\n";
        oss << "# TYPE dniip_agent_availability_ratio gauge\n";
        const double availability = known_nodes_.empty()
            ? 0.0
            : static_cast<double>(connected_agents_.load()) / static_cast<double>(known_nodes_.size());
        oss << "dniip_agent_availability_ratio " << availability << "\n";

        oss << "# HELP dniip_api_latency_ms_avg Average telemetry processing latency in milliseconds\n";
        oss << "# TYPE dniip_api_latency_ms_avg gauge\n";
        const double avg_latency = processing_latency_count_ > 0
            ? processing_latency_sum_ms_ / static_cast<double>(processing_latency_count_)
            : 0.0;
        oss << "dniip_api_latency_ms_avg " << avg_latency << "\n";

        oss << "# HELP dniip_node_packet_loss_percent Latest reported packet loss percent per node\n";
        oss << "# TYPE dniip_node_packet_loss_percent gauge\n";
        for (const auto& [node_id, pct] : packet_loss_by_node_) {
            oss << "dniip_node_packet_loss_percent{node_id=\"" << node_id << "\"} " << pct << "\n";
        }

        oss << "# HELP dniip_node_health_score Composite health score per node (0-100)\n";
        oss << "# TYPE dniip_node_health_score gauge\n";
        for (const auto& [node_id, score] : health_score_by_node_) {
            oss << "dniip_node_health_score{node_id=\"" << node_id << "\"} " << score << "\n";
        }

        return oss.str();
    }

private:
    std::atomic<uint64_t> incident_count_{0};
    std::atomic<int64_t> connected_agents_{0};
    std::atomic<uint64_t> telemetry_samples_total_{0};

    mutable std::mutex mutex_;
    double processing_latency_sum_ms_ = 0.0;
    uint64_t processing_latency_count_ = 0;
    std::unordered_map<std::string, double> packet_loss_by_node_;
    std::unordered_map<std::string, double> health_score_by_node_;
    std::unordered_set<std::string> known_nodes_;
};

} // namespace dniip::server
