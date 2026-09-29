#include <cstdlib>
#include <unistd.h>

#include <gtest/gtest.h>

#include "server/database/postgres_store.hpp"

using namespace dniip;
using namespace dniip::server;

namespace {

// These tests only run against a real PostgreSQL instance, pointed to via
// DNIIP_TEST_PG_CONNINFO (e.g. "host=localhost dbname=dniip_test"). They
// are skipped (not failed) when that's unset, so `ctest` stays green in
// environments without Postgres available (e.g. a bare CI runner).
std::string test_conninfo() {
    const char* v = std::getenv("DNIIP_TEST_PG_CONNINFO");
    return v ? v : "";
}

TelemetrySample sample_for(const std::string& node_id) {
    TelemetrySample s;
    s.node_id = node_id;
    s.timestamp = "2026-01-01T00:00:00Z";
    s.cpu_usage = 42.5;
    s.memory_usage = 60.0;
    s.packet_loss = 1.5;
    s.rtt_ms = 12.3;
    s.gateway_reachable = true;
    s.dns_resolvable = true;
    s.tcp_retransmits = 0;
    return s;
}

} // namespace

TEST(PostgresStore, SchemaAppliesAndRoundTripsData) {
    const std::string conninfo = test_conninfo();
    if (conninfo.empty()) GTEST_SKIP() << "DNIIP_TEST_PG_CONNINFO not set";

    PostgresStore store(conninfo); // applies schema on construction

    const std::string node_id = "test-node-" + std::to_string(::getpid());
    auto sample = sample_for(node_id);

    ASSERT_NO_THROW(store.upsert_node(node_id, sample.timestamp));
    ASSERT_NO_THROW(store.insert_telemetry(sample));

    CorrelatedEvent ev{EventType::kRoutingFailure, {node_id}, {"gateway unreachable"}, 0.85};
    ASSERT_NO_THROW(store.insert_event(ev));

    Incident inc;
    inc.incident_id = "INC-TEST-" + std::to_string(::getpid());
    inc.severity = Severity::kHigh;
    inc.root_cause = "Routing Failure";
    inc.affected_nodes = {node_id};
    inc.confidence = 0.85;
    inc.created_at = sample.timestamp;
    ASSERT_NO_THROW(store.insert_incident(inc));

    ASSERT_NO_THROW(store.insert_report(inc.incident_id, "INCIDENT REPORT\n...", sample.timestamp));
    // Re-inserting the same report_text must succeed via ON CONFLICT UPDATE.
    ASSERT_NO_THROW(store.insert_report(inc.incident_id, "INCIDENT REPORT (updated)\n...", sample.timestamp));
}
