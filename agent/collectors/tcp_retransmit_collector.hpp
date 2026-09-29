#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <stdexcept>

namespace dniip::agent {

// Reads the cumulative TCP retransmit segment counter from /proc/net/snmp
// ("Tcp:" section, RetransSegs column) and reports the delta since the
// previous sample, mirroring CpuCollector's delta-over-cumulative-counter
// approach. /proc/net/snmp's Tcp: RetransSegs is the kernel's own
// aggregate retransmit counter (same source `nstat`/`netstat -s` read),
// which is a far more direct signal than trying to infer retransmits from
// /proc/net/tcp's per-connection table (which has no retransmit column at
// all — only connection state).
class TcpRetransmitCollector {
public:
    // Returns retransmitted segments since the previous call, or 0 on the
    // first call (no prior sample to diff against).
    int sample() {
        const uint64_t cur = read_retrans_segs();
        int delta = 0;
        if (has_prev_) {
            delta = cur >= prev_ ? static_cast<int>(cur - prev_) : 0; // guard against counter reset
        }
        prev_ = cur;
        has_prev_ = true;
        return delta;
    }

private:
    static uint64_t read_retrans_segs() {
        std::ifstream f("/proc/net/snmp");
        if (!f) throw std::runtime_error("Failed to open /proc/net/snmp");

        // /proc/net/snmp lists each protocol as a pair of lines: a header
        // line naming the columns, then a data line with the same prefix
        // ("Tcp:") and matching column count.
        std::string header_line, data_line;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("Tcp:", 0) == 0) {
                if (header_line.empty()) {
                    header_line = line;
                } else {
                    data_line = line;
                    break;
                }
            }
        }
        if (header_line.empty() || data_line.empty()) return 0;

        std::istringstream header_iss(header_line);
        std::istringstream data_iss(data_line);
        std::string header_tok, data_tok;
        header_iss >> header_tok; // "Tcp:" label
        data_iss >> data_tok;     // "Tcp:" label

        int index = -1;
        for (int i = 0; header_iss >> header_tok; ++i) {
            if (header_tok == "RetransSegs") {
                index = i;
                break;
            }
        }
        if (index < 0) return 0;

        for (int i = 0; i <= index; ++i) {
            if (!(data_iss >> data_tok)) return 0;
        }
        try {
            return std::stoull(data_tok);
        } catch (...) {
            return 0;
        }
    }

    uint64_t prev_ = 0;
    bool has_prev_ = false;
};

} // namespace dniip::agent
