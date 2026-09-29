#pragma once

#include <atomic>
#include <cstdint>
#include <cerrno>
#include <string>
#include <thread>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "server/metrics/metrics_registry.hpp"

namespace dniip::server {

// Minimal single-threaded HTTP/1.0 server that only ever answers
// "GET /metrics" for Prometheus scraping. Deliberately not built on the
// epoll reactor: scrape traffic is low-frequency (Prometheus default scrape
// interval is 15s+) and one blocking accept-per-request loop on its own
// thread is simpler and entirely sufficient here.
class HttpMetricsServer {
public:
    HttpMetricsServer(uint16_t port, const MetricsRegistry& registry)
        : port_(port), registry_(registry) {}

    ~HttpMetricsServer() { stop(); }

    void start() {
        thread_ = std::thread([this] { run(); });
    }

    void stop() {
        running_ = false;
        if (listen_fd_ >= 0) {
            shutdown(listen_fd_, SHUT_RDWR);
            close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            spdlog::error("HttpMetricsServer: socket() failed");
            return;
        }
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            spdlog::error("HttpMetricsServer: bind() failed: {}", strerror(errno));
            return;
        }
        if (listen(listen_fd_, 16) < 0) {
            spdlog::error("HttpMetricsServer: listen() failed: {}", strerror(errno));
            return;
        }

        spdlog::info("HttpMetricsServer listening on port {} (GET /metrics)", port_);
        running_ = true;

        while (running_.load()) {
            int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd < 0) {
                if (!running_.load()) break;
                continue;
            }
            handle_client(fd);
            close(fd);
        }
    }

    void handle_client(int fd) {
        char buf[2048];
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return;
        buf[n] = '\0';

        // We only need to distinguish "/metrics" from everything else; a
        // full HTTP request-line parse is unnecessary for a scrape-only
        // endpoint with a single hardcoded path.
        const bool is_metrics = std::strstr(buf, "GET /metrics") != nullptr;

        std::string body = is_metrics ? registry_.render() : "not found\n";
        std::string status = is_metrics ? "200 OK" : "404 Not Found";

        std::string response =
            "HTTP/1.0 " + status + "\r\n"
            "Content-Type: text/plain; version=0.0.4\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + body;

        size_t sent = 0;
        while (sent < response.size()) {
            ssize_t s = send(fd, response.data() + sent, response.size() - sent, 0);
            if (s <= 0) break;
            sent += static_cast<size_t>(s);
        }
    }

    uint16_t port_;
    const MetricsRegistry& registry_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace dniip::server
