#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>
#include <unistd.h>

#include "common/telemetry.hpp"
#include "common/protocol.hpp"
#include "agent/collectors/cpu_collector.hpp"
#include "agent/collectors/memory_collector.hpp"
#include "agent/collectors/network_collector.hpp"
#include "agent/collectors/tcp_retransmit_collector.hpp"
#include "agent/diagnostics/connectivity_diagnostics.hpp"
#include "agent/networking/tcp_client.hpp"
#include "agent/storage/sqlite_buffer.hpp"

namespace {

std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }

std::string iso8601_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string node_id() {
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname));
    return hostname[0] ? hostname : "unknown-node";
}

} // namespace

int main(int argc, char** argv) {
    std::string server_host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t server_port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : 9000;
    std::string db_path = argc > 3 ? argv[3] : "dniip_agent.sqlite";
    const int collection_interval_s = 30;

    const std::string this_node = node_id();
    spdlog::info("dniip-agent starting: node_id={} server={}:{}", this_node, server_host, server_port);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    dniip::agent::SqliteBuffer buffer(db_path);
    dniip::agent::TcpClient client(server_host, server_port);
    dniip::agent::CpuCollector cpu_collector;
    dniip::agent::MemoryCollector mem_collector;
    dniip::agent::NetworkCollector net_collector;
    dniip::agent::TcpRetransmitCollector tcp_retransmit_collector;

    const std::string gateway = dniip::agent::ConnectivityDiagnostics::default_gateway();
    spdlog::info("Detected default gateway: {}", gateway.empty() ? "(none)" : gateway);

    while (g_running.load()) {
        const auto cycle_start = std::chrono::steady_clock::now();

        dniip::TelemetrySample sample;
        sample.node_id = this_node;
        sample.timestamp = iso8601_now();
        sample.cpu_usage = cpu_collector.sample();
        sample.memory_usage = mem_collector.sample();
        sample.interfaces = net_collector.sample();
        sample.tcp_retransmits = tcp_retransmit_collector.sample();

        if (!gateway.empty()) {
            auto conn = dniip::agent::ConnectivityDiagnostics::ping(gateway);
            sample.gateway_reachable = conn.gateway_reachable;
            sample.rtt_ms = conn.rtt_ms;
            sample.packet_loss = conn.packet_loss_pct;
        }
        sample.dns_resolvable = dniip::agent::ConnectivityDiagnostics::resolve_dns("www.google.com");

        // Store-and-forward: always buffer locally first.
        const int64_t row_id = buffer.enqueue(sample);
        spdlog::debug("Buffered telemetry sample row_id={}", row_id);

        // Try live transmission; on any failure, leave it for the next
        // cycle's retry-of-pending pass rather than blocking here.
        if (!client.is_connected()) client.connect_to_server();
        if (client.is_connected()) {
            nlohmann::json payload = sample;
            if (client.send_frame(dniip::protocol::MessageType::kTelemetry, payload)) {
                buffer.mark_synced(row_id);
            }
        } else {
            spdlog::warn("Server unreachable; sample row_id={} remains buffered", row_id);
        }

        // Retry a batch of previously-unsynced records (covers the case
        // where connectivity just came back after an outage).
        if (client.is_connected()) {
            for (const auto& pending : buffer.pending(50)) {
                if (pending.id == row_id) continue; // already handled above
                nlohmann::json payload = pending.sample;
                if (client.send_frame(dniip::protocol::MessageType::kTelemetry, payload)) {
                    buffer.mark_synced(pending.id);
                } else {
                    break; // connection dropped mid-batch; stop and retry next cycle
                }
            }
        }

        buffer.vacuum_synced();

        const auto elapsed = std::chrono::steady_clock::now() - cycle_start;
        const auto sleep_for = std::chrono::seconds(collection_interval_s) - elapsed;
        if (sleep_for > std::chrono::seconds(0)) {
            std::this_thread::sleep_for(sleep_for);
        }
    }

    spdlog::info("dniip-agent shut down cleanly");
    return 0;
}
