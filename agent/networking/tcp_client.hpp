#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <cstring>
#include <cstdint>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common/protocol.hpp"

namespace dniip::agent {

// Component 2 (agent side): custom TCP client for the DNIIP telemetry
// protocol. Deliberately synchronous/blocking with short connect/IO
// timeouts — the agent is single-purpose (collect, buffer, send) and
// doesn't need its own event loop; epoll multiplexing lives server-side
// where thousands of these clients land on one socket set.
class TcpClient {
public:
    TcpClient(std::string host, uint16_t port) : host_(std::move(host)), port_(port) {}

    ~TcpClient() { disconnect(); }

    bool is_connected() const { return fd_ >= 0; }

    bool connect_to_server(int timeout_ms = 3000) {
        disconnect();

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0 || !res) {
            return false;
        }

        for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
            int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;

            timeval tv{};
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
                fd_ = fd;
                break;
            }
            close(fd);
        }
        freeaddrinfo(res);
        return fd_ >= 0;
    }

    void disconnect() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool send_frame(protocol::MessageType type, const nlohmann::json& payload) {
        if (fd_ < 0) return false;
        const std::string frame = protocol::encode_frame(type, payload);

        size_t sent_total = 0;
        while (sent_total < frame.size()) {
            ssize_t n = send(fd_, frame.data() + sent_total, frame.size() - sent_total, 0);
            if (n <= 0) {
                disconnect();
                return false;
            }
            sent_total += static_cast<size_t>(n);
        }
        return true;
    }

    // Blocks (up to the socket's SO_RCVTIMEO) for exactly one frame.
    std::optional<nlohmann::json> recv_frame(protocol::MessageType* out_type = nullptr) {
        if (fd_ < 0) return std::nullopt;

        std::string buf;
        buf.resize(protocol::kHeaderSize);
        if (!recv_exact(buf.data(), buf.size())) return std::nullopt;

        auto hdr = protocol::try_parse_header(buf);
        if (!hdr || hdr->magic != protocol::kMagic) return std::nullopt;
        if (hdr->payload_len > protocol::kMaxPayloadSize) return std::nullopt;

        std::string payload;
        payload.resize(hdr->payload_len);
        if (!recv_exact(payload.data(), payload.size())) return std::nullopt;

        if (out_type) *out_type = hdr->type;
        try {
            return nlohmann::json::parse(payload);
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    bool recv_exact(char* dst, size_t len) {
        size_t got = 0;
        while (got < len) {
            ssize_t n = recv(fd_, dst + got, len - got, 0);
            if (n <= 0) {
                disconnect();
                return false;
            }
            got += static_cast<size_t>(n);
        }
        return true;
    }

    std::string host_;
    uint16_t port_;
    int fd_ = -1;
};

} // namespace dniip::agent
