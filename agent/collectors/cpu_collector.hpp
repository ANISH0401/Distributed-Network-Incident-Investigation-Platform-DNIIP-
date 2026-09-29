#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

namespace dniip::agent {

// Reads aggregate CPU utilization from /proc/stat. Usage percent is
// computed as a delta between two successive reads (a single /proc/stat
// snapshot only gives cumulative jiffies since boot), so the collector
// keeps its previous reading internally.
class CpuCollector {
public:
    // Returns CPU usage percent [0,100] since the previous call, or 0.0 on
    // the very first call (no prior sample to diff against).
    double sample() {
        Jiffies cur = read_proc_stat();
        double usage = 0.0;
        if (has_prev_) {
            const uint64_t idle_delta = cur.idle - prev_.idle;
            const uint64_t total_delta = cur.total() - prev_.total();
            if (total_delta > 0) {
                usage = 100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta));
            }
        }
        prev_ = cur;
        has_prev_ = true;
        return usage;
    }

private:
    struct Jiffies {
        uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        uint64_t total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
    };

    static Jiffies read_proc_stat() {
        std::ifstream f("/proc/stat");
        if (!f) throw std::runtime_error("Failed to open /proc/stat");

        std::string line;
        std::getline(f, line); // first line is the aggregate "cpu  ..." line
        std::istringstream iss(line);
        std::string label;
        Jiffies j;
        iss >> label >> j.user >> j.nice >> j.system >> j.idle >> j.iowait >> j.irq >> j.softirq >> j.steal;
        return j;
    }

    Jiffies prev_{};
    bool has_prev_ = false;
};

} // namespace dniip::agent
