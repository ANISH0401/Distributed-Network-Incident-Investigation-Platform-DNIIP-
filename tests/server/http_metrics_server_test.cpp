#include <chrono>
#include <thread>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "server/metrics/http_metrics_server.hpp"

using namespace dniip::server;

namespace {

std::string http_get(uint16_t port, const char* path) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // The server thread may still be mid-bind() right after start(); a
    // handful of short retries covers that without a fixed, flaky sleep.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    std::string req = std::string("GET ") + path + " HTTP/1.0\r\n\r\n";
    send(fd, req.data(), req.size(), 0);

    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        response.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return response;
}

} // namespace

TEST(HttpMetricsServer, ServesMetricsOverHttp) {
    MetricsRegistry registry;
    registry.inc_incident_count();
    registry.set_node_health_score("node-a", 95.0);

    HttpMetricsServer server(19100, registry);
    server.start();

    std::string response = http_get(19100, "/metrics");
    server.stop();

    ASSERT_NE(response.find("200 OK"), std::string::npos);
    ASSERT_NE(response.find("dniip_incident_count_total 1"), std::string::npos);
    ASSERT_NE(response.find("dniip_node_health_score{node_id=\"node-a\"} 95"), std::string::npos);
}

TEST(HttpMetricsServer, Returns404ForUnknownPath) {
    MetricsRegistry registry;
    HttpMetricsServer server(19101, registry);
    server.start();

    std::string response = http_get(19101, "/nope");
    server.stop();

    ASSERT_NE(response.find("404 Not Found"), std::string::npos);
}
