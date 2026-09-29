#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace dniip::agent {

// Reads memory utilization from /proc/meminfo.
class MemoryCollector {
public:
    // Returns memory usage percent [0,100] as (MemTotal - MemAvailable) / MemTotal.
    // MemAvailable (kernel >= 3.14) accounts for reclaimable cache, giving a
    // truer "pressure" figure than MemTotal - MemFree.
    double sample() const {
        std::ifstream f("/proc/meminfo");
        if (!f) throw std::runtime_error("Failed to open /proc/meminfo");

        std::unordered_map<std::string, uint64_t> values;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            iss >> key >> value; // ignores trailing "kB" unit
            if (!key.empty() && key.back() == ':') key.pop_back();
            values[key] = value;
        }

        auto total_it = values.find("MemTotal");
        auto avail_it = values.find("MemAvailable");
        if (total_it == values.end() || total_it->second == 0) return 0.0;

        uint64_t avail = (avail_it != values.end()) ? avail_it->second
                                                      : values.at("MemFree");
        double used_pct = 100.0 * (1.0 - static_cast<double>(avail) / static_cast<double>(total_it->second));
        return used_pct;
    }
};

} // namespace dniip::agent
