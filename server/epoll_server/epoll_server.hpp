#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <atomic>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "common/protocol.hpp"

namespace dniip::server {

// Component: epoll-based TCP collector (spec Component 2 server side).
//
// Single-threaded reactor built on epoll_wait/epoll_ctl/epoll_create1 that
// accepts connections from up to thousands of agents and multiplexes their
// I/O on one thread, handing decoded telemetry frames off to a callback
// (which in the real pipeline pushes onto a BlockingQueue for a worker
// pool — see server/collector). Kept level-triggered and single-threaded
// for clarity; the accept-and-hand-off-to-a-queue design is what lets this
// scale, not epoll edge-triggering tricks.
class EpollServer {
public:
    using FrameHandler = std::function<void(const std::string& peer, protocol::MessageType type,
                                             const nlohmann::json& payload)>;
    using ConnectionHandler = std::function<void(const std::string& peer)>;

    EpollServer(uint16_t port, FrameHandler handler,
                ConnectionHandler on_connect = nullptr, ConnectionHandler on_disconnect = nullptr);
    ~EpollServer();

    EpollServer(const EpollServer&) = delete;
    EpollServer& operator=(const EpollServer&) = delete;

    // Binds, listens, and runs the epoll_wait loop until stop() is called
    // from another thread (or a signal handler sets the shared flag passed
    // to run()). Blocking call — intended to run on its own thread or as
    // the daemon's main loop.
    void run();

    // Thread-safe; may be called from a signal handler-adjacent context
    // (it only sets an atomic flag which wakes epoll_wait via a timeout).
    void stop();

    // Sends a frame to a specific connected peer identified by the string
    // passed to FrameHandler (host:port). No-op if the peer disconnected.
    void send_to(const std::string& peer, protocol::MessageType type, const nlohmann::json& payload);

private:
    struct Connection {
        int fd = -1;
        std::string peer_name;
        std::string read_buf;
        std::string write_buf;
    };

    void accept_new_connections();
    void handle_readable(int fd);
    void handle_writable(int fd);
    void close_connection(int fd);
    void try_dispatch_frames(Connection& conn);

    uint16_t port_;
    FrameHandler handler_;
    ConnectionHandler on_connect_;
    ConnectionHandler on_disconnect_;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::atomic<bool> running_{false};
    std::unordered_map<int, Connection> connections_;
    std::unordered_map<std::string, int> peer_to_fd_;
};

} // namespace dniip::server
