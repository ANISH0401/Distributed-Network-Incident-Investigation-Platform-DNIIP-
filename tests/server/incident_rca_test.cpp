#include <algorithm>

#include <gtest/gtest.h>

#include "server/incident_engine/incident_engine.hpp"
#include "server/rca_engine/rca_engine.hpp"

using namespace dniip::server;

TEST(IncidentEngine, CreatesIncidentWithIncrementingIds) {
    IncidentEngine engine;
    CorrelatedEvent ev{EventType::kRoutingFailure,
                        {"node-a", "node-b", "node-c", "node-d", "node-e"},
                        {"gateway unreachable"}, 0.85};

    Incident i1 = engine.create_incident(ev);
    Incident i2 = engine.create_incident(ev);

    EXPECT_NE(i1.incident_id, i2.incident_id);
    EXPECT_EQ(i1.severity, Severity::kCritical); // 5 nodes >= threshold
    EXPECT_EQ(i1.root_cause, "Routing Failure");
    EXPECT_EQ(i1.affected_nodes.size(), 5u);
}

TEST(IncidentEngine, LowNodeCountRoutingFailureIsHighNotCritical) {
    IncidentEngine engine;
    CorrelatedEvent ev{EventType::kRoutingFailure, {"node-a", "node-b"}, {"x"}, 0.85};
    Incident inc = engine.create_incident(ev);
    EXPECT_EQ(inc.severity, Severity::kHigh);
}

TEST(RcaEngine, GeneratesRoutingFailureRecommendations) {
    IncidentEngine incident_engine;
    RcaEngine rca_engine;

    CorrelatedEvent ev{EventType::kRoutingFailure,
                        {"node-a", "node-b"},
                        {"node-a: gateway unreachable", "node-b: gateway unreachable"}, 0.85};
    Incident inc = incident_engine.create_incident(ev);
    RcaReport report = rca_engine.generate(inc);

    EXPECT_EQ(report.incident_id, inc.incident_id);
    EXPECT_EQ(report.root_cause, "Routing Failure");
    ASSERT_FALSE(report.recommendations.empty());
    EXPECT_NE(std::find(report.recommendations.begin(), report.recommendations.end(),
                        "Verify default gateway"),
              report.recommendations.end());

    std::string text = rca_engine.render_text(report);
    EXPECT_NE(text.find("INCIDENT REPORT"), std::string::npos);
    EXPECT_NE(text.find(inc.incident_id), std::string::npos);
}
