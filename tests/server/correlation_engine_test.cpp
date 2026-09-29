#include <gtest/gtest.h>

#include "server/correlation_engine/correlation_engine.hpp"

using namespace dniip;
using namespace dniip::server;

namespace {

TelemetrySample healthy_sample(const std::string& node_id) {
    TelemetrySample s;
    s.node_id = node_id;
    s.timestamp = "2026-01-01T00:00:00Z";
    s.cpu_usage = 10;
    s.memory_usage = 20;
    s.packet_loss = 0;
    s.rtt_ms = 5;
    s.gateway_reachable = true;
    s.dns_resolvable = true;
    s.tcp_retransmits = 0;
    return s;
}

} // namespace

TEST(CorrelationEngine, HealthySampleTriggersNothing) {
    CorrelationEngine engine;
    auto events = engine.ingest(healthy_sample("node-a"));
    EXPECT_TRUE(events.empty());
}

TEST(CorrelationEngine, Rule1NetworkCongestion) {
    CorrelationEngine engine;
    auto s = healthy_sample("node-a");
    s.packet_loss = 35;
    s.rtt_ms = 300;
    s.tcp_retransmits = 20;

    auto events = engine.ingest(s);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::kNetworkCongestion);
    EXPECT_EQ(events[0].affected_nodes, std::vector<std::string>{"node-a"});
}

TEST(CorrelationEngine, Rule2RoutingFailureNeedsMultipleNodes) {
    CorrelationEngine engine;
    auto a = healthy_sample("node-a");
    a.gateway_reachable = false;

    // Single node with gateway down should NOT yet trigger routing failure.
    EXPECT_TRUE(engine.ingest(a).empty());

    auto b = healthy_sample("node-b");
    b.gateway_reachable = false;

    auto events = engine.ingest(b);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::kRoutingFailure);
    EXPECT_EQ(events[0].affected_nodes.size(), 2u);
}

TEST(CorrelationEngine, Rule3LinkFailureOnDownInterface) {
    CorrelationEngine engine;
    auto s = healthy_sample("node-c");
    InterfaceStats eth0;
    eth0.name = "eth0";
    eth0.up = false;
    s.interfaces.push_back(eth0);

    auto events = engine.ingest(s);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::kLinkFailure);
}

TEST(CorrelationEngine, Rule4DnsOutage) {
    CorrelationEngine engine;
    auto s = healthy_sample("node-d");
    s.dns_resolvable = false;

    auto events = engine.ingest(s);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::kDnsServiceOutage);
}
