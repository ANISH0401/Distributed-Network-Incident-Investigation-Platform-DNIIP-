#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>

#include "common/telemetry.hpp"

namespace dniip::agent {

// Reads per-interface counters from /proc/net/dev and link state from
// /sys/class/net/<if>/operstate.
class NetworkCollector {
public:
    std::vector<InterfaceStats> sample() const {
        std::vector<InterfaceStats> result;
        std::ifstream f("/proc/net/dev");
        if (!f) return result;

        std::string line;
        std::getline(f, line); // header line 1
        std::getline(f, line); // header line 2 (column names)

        while (std::getline(f, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string name = line.substr(0, colon);
            name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
            if (name == "lo") continue; // skip loopback

            std::istringstream iss(line.substr(colon + 1));
            InterfaceStats stats;
            stats.name = name;
            uint64_t rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
            uint64_t tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

            iss >> stats.rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame
                >> rx_compressed >> rx_multicast
                >> stats.tx_bytes >> tx_packets >> tx_errs >> tx_drop >> tx_fifo >> tx_colls
                >> tx_carrier >> tx_compressed;

            stats.rx_errors = rx_errs;
            stats.tx_errors = tx_errs;
            stats.rx_dropped = rx_drop;
            stats.tx_dropped = tx_drop;
            stats.up = read_operstate(name);

            result.push_back(std::move(stats));
        }
        return result;
    }

private:
    static bool read_operstate(const std::string& if_name) {
        std::ifstream f("/sys/class/net/" + if_name + "/operstate");
        if (!f) return true; // assume up if we can't tell (e.g. permissions)
        std::string state;
        f >> state;
        return state == "up" || state == "unknown"; // "unknown" is common for virtual ifaces
    }
};

} // namespace dniip::agent
