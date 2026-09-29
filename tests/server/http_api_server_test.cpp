#include <chrono>
#include <thread>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "server/api/http_api_server.hpp"
#include "server/database/sqlite_store.hpp"

using namespace dniip;
using namespace dniip::server;

namespace {

std::string http_get(uint16_t port, const std::string& path) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    std::string req = "GET " + path + " HTTP/1.0\r\n\r\n";
    send(fd, req.data(), req.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) response.append(buf, static_cast<size_t>(n));
    close(fd);
    return response;
}

std::string body_of(const std::string& http_response) {
    auto pos = http_response.find("\r\n\r\n");
    return pos == std::string::npos ? "" : http_response.substr(pos + 4);
}

class HttpApiServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "test_api_" + std::to_string(::getpid()) + ".sqlite";
        store_ = std::make_unique<SqliteStore>(db_path_);

        TelemetrySample t;
        t.node_id = "node-a";
        t.timestamp = "2026-01-01T00:00:00Z";
        t.cpu_usage = 55.5;
        t.memory_usage = 40.0;
        t.packet_loss = 2.0;
        t.rtt_ms = 10.0;
        t.gateway_reachable = true;
        t.dns_resolvable = true;
        InterfaceStats eth0;
        eth0.name = "eth0";
        eth0.up = true;
        t.interfaces = {eth0};

        store_->upsert_node(t.node_id, t.timestamp);
        store_->insert_telemetry(t);

        Incident inc;
        inc.incident_id = "INC-API-TEST-1";
        inc.severity = Severity::kHigh;
        inc.root_cause = "Routing Failure";
        inc.affected_nodes = {"node-a"};
        inc.symptoms = {"node-a: gateway unreachable"};
        inc.confidence = 0.85;
        inc.created_at = t.timestamp;
        store_->insert_incident(inc);
        store_->insert_report(inc.incident_id, "INCIDENT REPORT\n...", t.timestamp);
    }

    void TearDown() override {
        store_.reset();
        std::remove(db_path_.c_str());
    }

    std::string db_path_;
    std::unique_ptr<SqliteStore> store_;
};

} // namespace

TEST_F(HttpApiServerTest, ListNodesIncludesLatestTelemetry) {
    HttpApiServer api(19200, *store_);
    api.start();
    std::string resp = http_get(19200, "/api/nodes");
    api.stop();

    auto json = nlohmann::json::parse(body_of(resp));
    ASSERT_EQ(json.size(), 1u);
    EXPECT_EQ(json[0]["node_id"], "node-a");
    EXPECT_EQ(json[0]["latest_telemetry"]["cpu_usage"], 55.5);
    EXPECT_EQ(json[0]["latest_telemetry"]["interfaces"][0]["name"], "eth0");
}

TEST_F(HttpApiServerTest, NodeTelemetryHistory) {
    HttpApiServer api(19201, *store_);
    api.start();
    std::string resp = http_get(19201, "/api/nodes/node-a/telemetry?limit=10");
    api.stop();

    auto json = nlohmann::json::parse(body_of(resp));
    ASSERT_EQ(json.size(), 1u);
    EXPECT_EQ(json[0]["node_id"], "node-a");
}

TEST_F(HttpApiServerTest, ListIncidentsFiltersByStatus) {
    HttpApiServer api(19202, *store_);
    api.start();
    std::string active_resp = http_get(19202, "/api/incidents?status=active");
    std::string resolved_resp = http_get(19202, "/api/incidents?status=resolved");
    api.stop();

    auto active = nlohmann::json::parse(body_of(active_resp));
    auto resolved = nlohmann::json::parse(body_of(resolved_resp));
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0]["incident_id"], "INC-API-TEST-1");
    EXPECT_EQ(resolved.size(), 0u);
}

TEST_F(HttpApiServerTest, IncidentReportIncludesTextAndMetadata) {
    HttpApiServer api(19203, *store_);
    api.start();
    std::string resp = http_get(19203, "/api/incidents/INC-API-TEST-1/report");
    api.stop();

    auto json = nlohmann::json::parse(body_of(resp));
    EXPECT_EQ(json["root_cause"], "Routing Failure");
    EXPECT_NE(json["report_text"].get<std::string>().find("INCIDENT REPORT"), std::string::npos);
}

TEST_F(HttpApiServerTest, UnknownRouteReturns404) {
    HttpApiServer api(19204, *store_);
    api.start();
    std::string resp = http_get(19204, "/api/nope");
    api.stop();
    EXPECT_NE(resp.find("404"), std::string::npos);
}
