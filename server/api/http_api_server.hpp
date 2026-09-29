#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/telemetry.hpp"
#include "server/common/models.hpp"
#include "server/database/store.hpp"

namespace dniip::server {

// Read-only JSON REST API for the React dashboard (spec Component 8), built
// on ITelemetryStore so it works against either backend. Deliberately a
// second, independent process/binary from dniip-server (the epoll
// telemetry-ingestion server, Linux-only): this API has no epoll/`/proc`
// dependency, so it builds and runs on any platform and can scale
// separately from ingestion — both just point at the same database.
//
// Same one-thread-per-request blocking model as HttpMetricsServer; request
// volume here is dashboard polling (seconds-scale), not agent telemetry.
class HttpApiServer {
public:
    HttpApiServer(uint16_t port, ITelemetryStore& store) : port_(port), store_(store) {}

    ~HttpApiServer() { stop(); }

    void start() { thread_ = std::thread([this] { run(); }); }

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
    struct Request {
        std::string method;
        std::string path;
        std::unordered_map<std::string, std::string> query;
    };

    void run() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            spdlog::error("HttpApiServer: socket() failed");
            return;
        }
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            spdlog::error("HttpApiServer: bind() failed: {}", strerror(errno));
            return;
        }
        if (listen(listen_fd_, 64) < 0) {
            spdlog::error("HttpApiServer: listen() failed: {}", strerror(errno));
            return;
        }

        spdlog::info("HttpApiServer listening on port {}", port_);
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
        std::string buf;
        buf.resize(8192);
        ssize_t n = recv(fd, buf.data(), buf.size(), 0);
        if (n <= 0) return;
        buf.resize(static_cast<size_t>(n));

        Request req = parse_request(buf);
        std::string body;
        int status = 200;

        try {
            if (req.method != "GET") {
                status = 405;
                body = R"({"error":"method not allowed"})";
            } else if (!route(req, body)) {
                status = 404;
                body = R"({"error":"not found"})";
            }
        } catch (const std::exception& e) {
            status = 500;
            body = nlohmann::json{{"error", e.what()}}.dump();
        }

        send_response(fd, status, body);
    }

    static Request parse_request(const std::string& raw) {
        Request req;
        std::istringstream iss(raw);
        std::string request_line;
        std::getline(iss, request_line);

        std::istringstream line_iss(request_line);
        std::string full_path;
        line_iss >> req.method >> full_path;

        auto qpos = full_path.find('?');
        req.path = qpos == std::string::npos ? full_path : full_path.substr(0, qpos);
        if (qpos != std::string::npos) {
            std::string qs = full_path.substr(qpos + 1);
            std::istringstream qss(qs);
            std::string pair;
            while (std::getline(qss, pair, '&')) {
                auto eq = pair.find('=');
                if (eq == std::string::npos) continue;
                req.query[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
        }
        return req;
    }

    static std::vector<std::string> split_path(const std::string& path) {
        std::vector<std::string> segments;
        std::istringstream iss(path);
        std::string seg;
        while (std::getline(iss, seg, '/')) {
            if (!seg.empty()) segments.push_back(seg);
        }
        return segments;
    }

    // Returns false only for "no route matched" (-> caller sends 404).
    // A matched route that hits an application-level "not found" (e.g.
    // unknown node id) still returns true with an appropriate JSON body.
    bool route(const Request& req, std::string& out_body) {
        auto segments = split_path(req.path);

        // GET /api/nodes
        if (segments.size() == 2 && segments[0] == "api" && segments[1] == "nodes") {
            out_body = handle_list_nodes();
            return true;
        }
        // GET /api/nodes/{id}/telemetry?limit=N
        if (segments.size() == 4 && segments[0] == "api" && segments[1] == "nodes" && segments[3] == "telemetry") {
            int limit = 50;
            auto it = req.query.find("limit");
            if (it != req.query.end()) limit = std::atoi(it->second.c_str());
            out_body = handle_node_telemetry(segments[2], limit);
            return true;
        }
        // GET /api/incidents?status=active|resolved&limit=N
        if (segments.size() == 2 && segments[0] == "api" && segments[1] == "incidents") {
            std::optional<IncidentStatus> status_filter;
            auto it = req.query.find("status");
            if (it != req.query.end()) {
                if (it->second == "active") status_filter = IncidentStatus::kActive;
                else if (it->second == "resolved") status_filter = IncidentStatus::kResolved;
            }
            int limit = 100;
            auto lit = req.query.find("limit");
            if (lit != req.query.end()) limit = std::atoi(lit->second.c_str());
            out_body = handle_list_incidents(status_filter, limit);
            return true;
        }
        // GET /api/incidents/{id}/report
        if (segments.size() == 4 && segments[0] == "api" && segments[1] == "incidents" && segments[3] == "report") {
            out_body = handle_incident_report(segments[2]);
            return true;
        }
        return false;
    }

    std::string handle_list_nodes() {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : store_.list_nodes()) {
            nlohmann::json entry{{"node_id", n.node_id}, {"first_seen", n.first_seen}, {"last_seen", n.last_seen}};
            auto latest = store_.latest_telemetry(n.node_id);
            entry["latest_telemetry"] = latest ? nlohmann::json(*latest) : nlohmann::json(nullptr);
            arr.push_back(std::move(entry));
        }
        return arr.dump();
    }

    std::string handle_node_telemetry(const std::string& node_id, int limit) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : store_.telemetry_history(node_id, limit)) arr.push_back(t);
        return arr.dump();
    }

    std::string handle_list_incidents(std::optional<IncidentStatus> status_filter, int limit) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& inc : store_.list_incidents(status_filter, limit)) {
            arr.push_back(incident_to_json(inc));
        }
        return arr.dump();
    }

    std::string handle_incident_report(const std::string& incident_id) {
        auto incident = store_.get_incident(incident_id);
        if (!incident) return nlohmann::json{{"error", "incident not found"}}.dump();

        nlohmann::json result = incident_to_json(*incident);
        auto report = store_.get_report(incident_id);
        result["report_text"] = report ? report->report_text : "";
        result["generated_at"] = report ? report->generated_at : "";
        return result.dump();
    }

    static nlohmann::json incident_to_json(const Incident& inc) {
        return nlohmann::json{
            {"incident_id", inc.incident_id},
            {"severity", to_string(inc.severity)},
            {"category", inc.category},
            {"root_cause", inc.root_cause},
            {"affected_nodes", inc.affected_nodes},
            {"symptoms", inc.symptoms},
            {"confidence", inc.confidence},
            {"created_at", inc.created_at},
            {"status", to_string(inc.status)},
        };
    }

    void send_response(int fd, int status, const std::string& body) {
        const char* status_text = status == 200 ? "OK" : status == 404 ? "Not Found"
                                 : status == 405 ? "Method Not Allowed" : "Internal Server Error";
        std::string response =
            "HTTP/1.0 " + std::to_string(status) + " " + status_text + "\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
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
    ITelemetryStore& store_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace dniip::server
