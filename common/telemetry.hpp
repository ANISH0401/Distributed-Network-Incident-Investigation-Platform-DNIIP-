#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace dniip {

struct InterfaceStats {
    std::string name;
    bool up = true;             // from /sys/class/net/<if>/operstate
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    uint64_t rx_errors = 0;
    uint64_t tx_errors = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_dropped = 0;
};

struct TelemetrySample {
    std::string node_id;
    std::string timestamp;      // ISO-8601 UTC
    double cpu_usage = 0.0;     // percent, 0-100
    double memory_usage = 0.0;  // percent, 0-100
    double packet_loss = 0.0;   // percent, 0-100 (from ping diagnostics)
    double rtt_ms = 0.0;        // average RTT to gateway/target
    bool gateway_reachable = false;
    bool dns_resolvable = true;
    int tcp_retransmits = 0;
    std::vector<InterfaceStats> interfaces;
};

inline void to_json(nlohmann::json& j, const InterfaceStats& s) {
    j = nlohmann::json{
        {"name", s.name},
        {"up", s.up},
        {"rx_bytes", s.rx_bytes}, {"tx_bytes", s.tx_bytes},
        {"rx_errors", s.rx_errors}, {"tx_errors", s.tx_errors},
        {"rx_dropped", s.rx_dropped}, {"tx_dropped", s.tx_dropped},
    };
}

inline void from_json(const nlohmann::json& j, InterfaceStats& s) {
    j.at("name").get_to(s.name);
    if (j.contains("up")) j.at("up").get_to(s.up);
    j.at("rx_bytes").get_to(s.rx_bytes);
    j.at("tx_bytes").get_to(s.tx_bytes);
    j.at("rx_errors").get_to(s.rx_errors);
    j.at("tx_errors").get_to(s.tx_errors);
    j.at("rx_dropped").get_to(s.rx_dropped);
    j.at("tx_dropped").get_to(s.tx_dropped);
}

inline void to_json(nlohmann::json& j, const TelemetrySample& t) {
    j = nlohmann::json{
        {"node_id", t.node_id},
        {"timestamp", t.timestamp},
        {"cpu_usage", t.cpu_usage},
        {"memory_usage", t.memory_usage},
        {"packet_loss", t.packet_loss},
        {"rtt_ms", t.rtt_ms},
        {"gateway_reachable", t.gateway_reachable},
        {"dns_resolvable", t.dns_resolvable},
        {"tcp_retransmits", t.tcp_retransmits},
        {"interfaces", t.interfaces},
    };
}

inline void from_json(const nlohmann::json& j, TelemetrySample& t) {
    j.at("node_id").get_to(t.node_id);
    j.at("timestamp").get_to(t.timestamp);
    j.at("cpu_usage").get_to(t.cpu_usage);
    j.at("memory_usage").get_to(t.memory_usage);
    j.at("packet_loss").get_to(t.packet_loss);
    j.at("rtt_ms").get_to(t.rtt_ms);
    j.at("gateway_reachable").get_to(t.gateway_reachable);
    if (j.contains("dns_resolvable")) j.at("dns_resolvable").get_to(t.dns_resolvable);
    if (j.contains("tcp_retransmits")) j.at("tcp_retransmits").get_to(t.tcp_retransmits);
    if (j.contains("interfaces")) j.at("interfaces").get_to(t.interfaces);
}

} // namespace dniip
