#pragma once

#include <thread>
#include <vector>
#include <chrono>

#include <spdlog/spdlog.h>

#include "common/telemetry.hpp"
#include "common/protocol.hpp"
#include "server/common/blocking_queue.hpp"
#include "server/common/models.hpp"
#include "server/correlation_engine/correlation_engine.hpp"
#include "server/incident_engine/incident_engine.hpp"
#include "server/rca_engine/rca_engine.hpp"
#include "server/database/store.hpp"
#include "server/epoll_server/epoll_server.hpp"
#include "server/metrics/metrics_registry.hpp"

namespace dniip::server {

namespace detail {
// Simple, explainable composite score — not a statistical model — so it
// stays consistent with the RCA engine's "deterministic, justifiable
// output" design goal (see rca_engine.hpp).
inline double compute_health_score(const TelemetrySample& s) {
    double score = 100.0;
    score -= s.packet_loss;                 // 1 point per % packet loss
    if (!s.gateway_reachable) score -= 30.0;
    if (!s.dns_resolvable) score -= 15.0;
    for (const auto& iface : s.interfaces) {
        if (!iface.up) score -= 20.0;
    }
    if (score < 0.0) score = 0.0;
    return score;
}
} // namespace detail

// Wires the epoll TCP server to the telemetry processing pipeline:
//   epoll I/O thread -> BlockingQueue<TelemetrySample> -> worker thread(s)
//     -> CorrelationEngine -> IncidentEngine -> RcaEngine -> ITelemetryStore
//
// This is the producer/consumer pipeline called for in spec Component 10.
// Backend-agnostic over `store` (SqliteStore or PostgresStore) via
// ITelemetryStore, and reports operational metrics into `metrics` for
// Prometheus scraping (see server/metrics/).
class TelemetryCollector {
public:
    TelemetryCollector(uint16_t port, ITelemetryStore& store, MetricsRegistry& metrics,
                        int worker_threads = 2)
        : store_(store), metrics_(metrics), num_workers_(worker_threads),
          epoll_server_(
              port,
              [this](const std::string& peer, protocol::MessageType type, const nlohmann::json& payload) {
                  on_frame(peer, type, payload);
              },
              [this](const std::string&) { metrics_.inc_connected_agents(); },
              [this](const std::string&) { metrics_.dec_connected_agents(); }) {}

    void start() {
        for (int i = 0; i < num_workers_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
        epoll_server_.run(); // blocks until stop()
    }

    void stop() {
        epoll_server_.stop();
        queue_.shutdown();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

private:
    void on_frame(const std::string& peer, protocol::MessageType type, const nlohmann::json& payload) {
        if (type != protocol::MessageType::kTelemetry) return;
        try {
            TelemetrySample sample = payload.get<TelemetrySample>();
            queue_.push(std::move(sample));
        } catch (const std::exception& e) {
            spdlog::warn("Failed to decode telemetry from {}: {}", peer, e.what());
        }
    }

    void worker_loop() {
        while (auto sample = queue_.pop()) {
            const auto start = std::chrono::steady_clock::now();

            store_.upsert_node(sample->node_id, sample->timestamp);
            store_.insert_telemetry(*sample);

            metrics_.inc_telemetry_samples();
            metrics_.mark_node_seen(sample->node_id);
            metrics_.set_node_packet_loss(sample->node_id, sample->packet_loss);
            metrics_.set_node_health_score(sample->node_id, detail::compute_health_score(*sample));

            for (const auto& event : correlation_.ingest(*sample)) {
                Incident incident = incident_engine_.create_incident(event);
                store_.insert_incident(incident);
                metrics_.inc_incident_count();

                RcaReport report = rca_engine_.generate(incident);
                store_.insert_report(report.incident_id, rca_engine_.render_text(report), report.generated_at);

                spdlog::warn("Incident {} [{}] root_cause={} nodes={} confidence={:.0f}%",
                             incident.incident_id, to_string(incident.severity), incident.root_cause,
                             incident.affected_nodes.size(), incident.confidence * 100);
            }

            const auto elapsed_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            metrics_.observe_processing_latency_ms(elapsed_ms);
        }
    }

    ITelemetryStore& store_;
    MetricsRegistry& metrics_;
    int num_workers_;
    BlockingQueue<TelemetrySample> queue_;
    CorrelationEngine correlation_;
    IncidentEngine incident_engine_;
    RcaEngine rca_engine_;
    EpollServer epoll_server_;
    std::vector<std::thread> workers_;
};

} // namespace dniip::server
