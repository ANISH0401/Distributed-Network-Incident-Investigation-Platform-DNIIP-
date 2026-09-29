// Dev/demo utility: seeds a SqliteStore with a handful of nodes, telemetry
// history, and incidents/reports covering all four correlation rules, so
// dashboard.hpp/dniip-api and the React dashboard have something to render
// without needing a live agent fleet running. Not part of the production
// binaries (see server/CMakeLists.txt / scripts/CMakeLists.txt).
#include <chrono>
#include <cstdio>
#include <string>

#include "server/database/sqlite_store.hpp"
#include "server/incident_engine/incident_engine.hpp"
#include "server/rca_engine/rca_engine.hpp"

using namespace dniip;
using namespace dniip::server;

namespace {

std::string ts_minutes_ago(int minutes) {
    auto t = std::chrono::system_clock::now() - std::chrono::minutes(minutes);
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

TelemetrySample make_sample(const std::string& node_id, int minutes_ago, double cpu, double mem,
                             double loss, double rtt, bool gw_ok, bool dns_ok, int retrans,
                             bool eth0_up = true) {
    TelemetrySample s;
    s.node_id = node_id;
    s.timestamp = ts_minutes_ago(minutes_ago);
    s.cpu_usage = cpu;
    s.memory_usage = mem;
    s.packet_loss = loss;
    s.rtt_ms = rtt;
    s.gateway_reachable = gw_ok;
    s.dns_resolvable = dns_ok;
    s.tcp_retransmits = retrans;
    InterfaceStats eth0;
    eth0.name = "eth0";
    eth0.up = eth0_up;
    eth0.rx_bytes = 123456789;
    eth0.tx_bytes = 98765432;
    s.interfaces = {eth0};
    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::string db_path = argc > 1 ? argv[1] : "dniip_server.sqlite";
    SqliteStore store(db_path);
    IncidentEngine incident_engine;
    RcaEngine rca_engine;

    // node-a: healthy over the last hour.
    for (int m = 60; m >= 0; m -= 10) {
        auto s = make_sample("node-a", m, 12.0 + (m % 20), 30.0, 0.0, 8.0, true, true, 0);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);
    }

    // node-b: currently degraded (Rule 1: network congestion).
    for (int m = 60; m >= 10; m -= 10) {
        auto s = make_sample("node-b", m, 20.0, 40.0, 1.0, 15.0, true, true, 0);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);
    }
    {
        auto s = make_sample("node-b", 0, 78.0, 82.0, 35.0, 320.0, true, true, 22);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);

        CorrelatedEvent ev{EventType::kNetworkCongestion, {"node-b"},
                           {"node-b: packet loss 35%, RTT 320ms, 22 retransmits"}, 0.75};
        Incident inc = incident_engine.create_incident(ev);
        store.insert_incident(inc);
        RcaReport report = rca_engine.generate(inc);
        store.insert_report(report.incident_id, rca_engine.render_text(report), report.generated_at);
        std::printf("Seeded active incident %s (Network Congestion) on node-b\n", inc.incident_id.c_str());
    }

    // node-c, node-d: gateway unreachable (Rule 2: routing failure), now resolved.
    for (const auto& node : {"node-c", "node-d"}) {
        for (int m = 45; m >= 20; m -= 10) {
            auto s = make_sample(node, m, 15.0, 35.0, 0.0, 9.0, true, true, 0);
            store.upsert_node(s.node_id, s.timestamp);
            store.insert_telemetry(s);
        }
        auto s = make_sample(node, 15, 15.0, 35.0, 0.0, 0.0, false, true, 0);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);
    }
    {
        CorrelatedEvent ev{EventType::kRoutingFailure, {"node-c", "node-d"},
                           {"node-c: gateway unreachable", "node-d: gateway unreachable"}, 0.85};
        Incident inc = incident_engine.create_incident(ev);
        store.insert_incident(inc);
        RcaReport report = rca_engine.generate(inc);
        store.insert_report(report.incident_id, rca_engine.render_text(report), report.generated_at);
        store.set_incident_status(inc.incident_id, IncidentStatus::kResolved);
        std::printf("Seeded resolved incident %s (Routing Failure) on node-c/node-d\n", inc.incident_id.c_str());
    }

    // node-e: interface down (Rule 3: link failure).
    for (int m = 30; m >= 10; m -= 10) {
        auto s = make_sample("node-e", m, 10.0, 25.0, 0.0, 7.0, true, true, 0, /*eth0_up=*/true);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);
    }
    {
        auto s = make_sample("node-e", 0, 10.0, 25.0, 0.0, 0.0, true, true, 0, /*eth0_up=*/false);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);

        CorrelatedEvent ev{EventType::kLinkFailure, {"node-e"}, {"node-e: interface eth0 is down"}, 0.95};
        Incident inc = incident_engine.create_incident(ev);
        store.insert_incident(inc);
        RcaReport report = rca_engine.generate(inc);
        store.insert_report(report.incident_id, rca_engine.render_text(report), report.generated_at);
        std::printf("Seeded active incident %s (Link Failure) on node-e\n", inc.incident_id.c_str());
    }

    // node-f: DNS outage (Rule 4).
    for (int m = 30; m >= 10; m -= 10) {
        auto s = make_sample("node-f", m, 18.0, 45.0, 0.0, 11.0, true, true, 0);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);
    }
    {
        auto s = make_sample("node-f", 0, 18.0, 45.0, 0.0, 11.0, true, false, 0);
        store.upsert_node(s.node_id, s.timestamp);
        store.insert_telemetry(s);

        CorrelatedEvent ev{EventType::kDnsServiceOutage, {"node-f"}, {"node-f: DNS resolution failed"}, 0.80};
        Incident inc = incident_engine.create_incident(ev);
        store.insert_incident(inc);
        RcaReport report = rca_engine.generate(inc);
        store.insert_report(report.incident_id, rca_engine.render_text(report), report.generated_at);
        std::printf("Seeded active incident %s (DNS Service Outage) on node-f\n", inc.incident_id.c_str());
    }

    std::printf("Sample data seeded into %s\n", db_path.c_str());
    return 0;
}
