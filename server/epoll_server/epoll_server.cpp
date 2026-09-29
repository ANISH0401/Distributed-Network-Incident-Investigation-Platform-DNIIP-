#include "epoll_server.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <vector>

#include <spdlog/spdlog.h>

namespace dniip::server {

namespace {

constexpr int kMaxEvents = 256;
constexpr int kBacklog = 1024;
constexpr int kEpollWaitTimeoutMs = 500; // wakes periodically to check running_

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) throw std::runtime_error("fcntl(F_GETFL) failed");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl(F_SETFL, O_NONBLOCK) failed");
    }
}

std::string format_peer(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace

EpollServer::EpollServer(uint16_t port, FrameHandler handler,
                          ConnectionHandler on_connect, ConnectionHandler on_disconnect)
    : port_(port), handler_(std::move(handler)),
      on_connect_(std::move(on_connect)), on_disconnect_(std::move(on_disconnect)) {}

EpollServer::~EpollServer() {
    stop();
    if (listen_fd_ >= 0) close(listen_fd_);
    if (epoll_fd_ >= 0) close(epoll_fd_);
    for (auto& [fd, conn] : connections_) close(fd);
}

void EpollServer::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));
    }
    if (listen(listen_fd_, kBacklog) < 0) {
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));
    }
    set_nonblocking(listen_fd_);

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1() failed");

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        throw std::runtime_error("epoll_ctl(ADD listen_fd) failed");
    }

    spdlog::info("EpollServer listening on port {}", port_);
    running_ = true;

    std::vector<epoll_event> events(kMaxEvents);
    while (running_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epoll_fd_, events.data(), kMaxEvents, kEpollWaitTimeoutMs);
        if (n < 0) {
            if (errno == EINTR) continue;
            spdlog::error("epoll_wait failed: {}", strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                accept_new_connections();
                continue;
            }
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                close_connection(fd);
                continue;
            }
            if (events[i].events & EPOLLIN) handle_readable(fd);
            if (events[i].events & EPOLLOUT) handle_writable(fd);
        }
    }
}

void EpollServer::stop() { running_ = false; }

void EpollServer::accept_new_connections() {
    for (;;) {
        sockaddr_in peer_addr{};
        socklen_t len = sizeof(peer_addr);
        int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer_addr), &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                spdlog::warn("accept() failed: {}", strerror(errno));
            }
            return;
        }
        set_nonblocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        Connection conn;
        conn.fd = fd;
        conn.peer_name = format_peer(peer_addr);

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            spdlog::error("epoll_ctl(ADD, {}) failed: {}", fd, strerror(errno));
            close(fd);
            continue;
        }

        peer_to_fd_[conn.peer_name] = fd;
        const std::string peer_name = conn.peer_name;
        connections_.emplace(fd, std::move(conn));
        spdlog::info("Accepted connection from {}", peer_name);
        if (on_connect_) on_connect_(peer_name);
    }
}

void EpollServer::handle_readable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    char buf[65536];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn.read_buf.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            close_connection(fd);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        spdlog::warn("recv() error on {}: {}", conn.peer_name, strerror(errno));
        close_connection(fd);
        return;
    }
    try_dispatch_frames(conn);
}

void EpollServer::try_dispatch_frames(Connection& conn) {
    for (;;) {
        auto hdr = protocol::try_parse_header(conn.read_buf);
        if (!hdr) return; // need more bytes for header
        if (hdr->magic != protocol::kMagic) {
            spdlog::warn("Bad magic from {}, dropping connection", conn.peer_name);
            close_connection(conn.fd);
            return;
        }
        if (hdr->payload_len > protocol::kMaxPayloadSize) {
            spdlog::warn("Oversized frame from {} ({} bytes), dropping connection",
                         conn.peer_name, hdr->payload_len);
            close_connection(conn.fd);
            return;
        }
        const size_t total = protocol::kHeaderSize + hdr->payload_len;
        if (conn.read_buf.size() < total) return; // need more bytes for payload

        std::string payload_str = conn.read_buf.substr(protocol::kHeaderSize, hdr->payload_len);
        conn.read_buf.erase(0, total);

        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(payload_str);
        } catch (const std::exception& e) {
            spdlog::warn("Malformed JSON payload from {}: {}", conn.peer_name, e.what());
            continue;
        }

        if (handler_) handler_(conn.peer_name, hdr->type, payload);
    }
}

void EpollServer::handle_writable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    while (!conn.write_buf.empty()) {
        ssize_t n = send(fd, conn.write_buf.data(), conn.write_buf.size(), 0);
        if (n > 0) {
            conn.write_buf.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n < 0 && errno == EINTR) continue;
        close_connection(fd);
        return;
    }

    if (conn.write_buf.empty()) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void EpollServer::send_to(const std::string& peer, protocol::MessageType type,
                           const nlohmann::json& payload) {
    auto pit = peer_to_fd_.find(peer);
    if (pit == peer_to_fd_.end()) return;
    auto it = connections_.find(pit->second);
    if (it == connections_.end()) return;

    Connection& conn = it->second;
    const bool was_empty = conn.write_buf.empty();
    conn.write_buf += protocol::encode_frame(type, payload);

    if (was_empty) {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.fd = conn.fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &ev);
    }
}

void EpollServer::close_connection(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    const std::string peer_name = it->second.peer_name;
    spdlog::info("Closing connection from {}", peer_name);
    peer_to_fd_.erase(peer_name);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(it);
    if (on_disconnect_) on_disconnect_(peer_name);
}

} // namespace dniip::server
