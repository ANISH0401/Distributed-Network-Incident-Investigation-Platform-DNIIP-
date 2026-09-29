#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <cstdint>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace dniip::agent {

struct ConnectivityResult {
    bool gateway_reachable = false;
    double rtt_ms = 0.0;
    double packet_loss_pct = 0.0;
    bool dns_resolvable = true;
};

// Component: connectivity diagnostics (ping/gateway reachability/DNS),
// implemented directly on top of socket()/getaddrinfo() rather than
// shelling out, per spec Component 1.
class ConnectivityDiagnostics {
public:
    // Reads the default gateway address from /proc/net/route (spec:
    // "Routing Information"). Returns empty string if none found.
    static std::string default_gateway() {
        std::ifstream f("/proc/net/route");
        if (!f) return "";

        std::string line;
        std::getline(f, line); // header
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string iface, destination, gateway_hex;
            iss >> iface >> destination >> gateway_hex;
            if (destination == "00000000" && gateway_hex != "00000000") {
                // /proc/net/route stores the gateway as a little-endian hex u32.
                uint32_t gw = std::stoul(gateway_hex, nullptr, 16);
                in_addr addr{};
                addr.s_addr = gw;
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr, buf, sizeof(buf));
                return std::string(buf);
            }
        }
        return "";
    }

    // Sends a single ICMP echo request to `host` and measures RTT.
    // Requires CAP_NET_RAW (or running as root) to open a raw socket,
    // consistent with how ping(8) itself operates. Returns pings_sent=1,
    // and 0% or 100% packet loss for this single probe (agents call this
    // repeatedly across collection cycles to build a loss percentage
    // trend server-side).
    static ConnectivityResult ping(const std::string& host, int timeout_ms = 1000) {
        ConnectivityResult result;

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_RAW;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            result.dns_resolvable = false;
            result.packet_loss_pct = 100.0;
            return result;
        }

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP); // unprivileged ICMP (Linux ping_group_range)
        if (sock < 0) sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            freeaddrinfo(res);
            result.packet_loss_pct = 100.0;
            return result;
        }

        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        icmphdr icmp_hdr{};
        icmp_hdr.type = ICMP_ECHO;
        icmp_hdr.code = 0;
        icmp_hdr.un.echo.id = static_cast<uint16_t>(getpid() & 0xFFFF);
        icmp_hdr.un.echo.sequence = 1;
        icmp_hdr.checksum = 0;
        icmp_hdr.checksum = checksum(&icmp_hdr, sizeof(icmp_hdr));

        const auto start = std::chrono::steady_clock::now();
        ssize_t sent = sendto(sock, &icmp_hdr, sizeof(icmp_hdr), 0, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);

        if (sent < 0) {
            close(sock);
            result.packet_loss_pct = 100.0;
            return result;
        }

        char recv_buf[512];
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        ssize_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                              reinterpret_cast<sockaddr*>(&from), &from_len);
        close(sock);

        if (n > 0) {
            const auto end = std::chrono::steady_clock::now();
            result.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
            result.gateway_reachable = true;
            result.packet_loss_pct = 0.0;
        } else {
            result.packet_loss_pct = 100.0;
        }
        return result;
    }

    // Resolves `hostname` via getaddrinfo(); true if at least one address
    // was returned.
    static bool resolve_dns(const std::string& hostname) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        const bool ok = (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) == 0);
        if (res) freeaddrinfo(res);
        return ok;
    }

private:
    static uint16_t checksum(void* data, size_t len) {
        uint32_t sum = 0;
        auto* buf = reinterpret_cast<uint16_t*>(data);
        while (len > 1) {
            sum += *buf++;
            len -= 2;
        }
        if (len == 1) sum += *reinterpret_cast<uint8_t*>(buf);
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<uint16_t>(~sum);
    }
};

} // namespace dniip::agent
