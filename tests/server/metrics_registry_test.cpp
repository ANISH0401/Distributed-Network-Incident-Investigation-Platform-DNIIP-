#include <gtest/gtest.h>

#include "server/metrics/metrics_registry.hpp"

using namespace dniip::server;

TEST(MetricsRegistry, RendersCountersAndGauges) {
    MetricsRegistry m;
    m.inc_incident_count();
    m.inc_incident_count();
    m.inc_connected_agents();
    m.mark_node_seen("node-a");
    m.set_node_packet_loss("node-a", 12.5);
    m.set_node_health_score("node-a", 80.0);
    m.observe_processing_latency_ms(10.0);
    m.observe_processing_latency_ms(20.0);

    const std::string text = m.render();

    EXPECT_NE(text.find("dniip_incident_count_total 2"), std::string::npos);
    EXPECT_NE(text.find("dniip_connected_agents 1"), std::string::npos);
    EXPECT_NE(text.find("dniip_node_packet_loss_percent{node_id=\"node-a\"} 12.5"), std::string::npos);
    EXPECT_NE(text.find("dniip_node_health_score{node_id=\"node-a\"} 80"), std::string::npos);
    EXPECT_NE(text.find("dniip_api_latency_ms_avg 15"), std::string::npos);
}

TEST(MetricsRegistry, ConnectedAgentsDecrementsOnDisconnect) {
    MetricsRegistry m;
    m.inc_connected_agents();
    m.inc_connected_agents();
    m.dec_connected_agents();
    EXPECT_NE(m.render().find("dniip_connected_agents 1"), std::string::npos);
}
