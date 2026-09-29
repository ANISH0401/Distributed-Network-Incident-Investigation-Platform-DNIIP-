#pragma once

#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "server/common/models.hpp"

namespace dniip::server {

inline std::string iso8601_now()
{
    const auto now =
        std::chrono::system_clock::now();

    const std::time_t t =
        std::chrono::system_clock::to_time_t(now);

    std::tm tm{};

    gmtime_r(&t, &tm);

    std::ostringstream oss;

    oss << std::put_time(
        &tm,
        "%Y-%m-%dT%H:%M:%SZ"
    );

    return oss.str();
}

inline Severity severity_from(
    const CorrelatedEvent& ev)
{
    const size_t n =
        ev.affected_nodes.size();

    switch (ev.type)
    {
        case EventType::kRoutingFailure:
            return n >= 5
                ? Severity::kCritical
                : Severity::kHigh;

        case EventType::kDnsServiceOutage:
            return n >= 3
                ? Severity::kCritical
                : Severity::kHigh;

        case EventType::kLinkFailure:
            return n >= 3
                ? Severity::kHigh
                : Severity::kMedium;

        case EventType::kNetworkCongestion:
            return n >= 3
                ? Severity::kHigh
                : Severity::kMedium;
    }

    return Severity::kLow;
}

class IncidentEngine
{
public:

    Incident create_or_update_incident(
        const CorrelatedEvent& ev)
    {
        std::lock_guard<std::mutex>
            lock(mutex_);

        std::string key =
            incident_key(ev);

        auto it =
            active_incidents_.find(key);

        if(it != active_incidents_.end())
        {
            Incident& inc =
                it->second;

            inc.last_seen =
                iso8601_now();

            inc.occurrence_count++;

            inc.confidence =
                std::max(
                    inc.confidence,
                    ev.confidence
                );

            inc.affected_nodes =
                ev.affected_nodes;

            inc.symptoms =
                ev.symptoms;

            return inc;
        }

        Incident inc;

        inc.incident_id =
            next_incident_id();

        inc.severity =
            severity_from(ev);

        inc.category =
            "NETWORK";

        inc.root_cause =
            to_string(ev.type);

        inc.affected_nodes =
            ev.affected_nodes;

        inc.symptoms =
            ev.symptoms;

        inc.confidence =
            ev.confidence;

        inc.created_at =
            iso8601_now();

        inc.first_seen =
            inc.created_at;

        inc.last_seen =
            inc.created_at;

        inc.occurrence_count =
            1;

        active_incidents_[key] =
            inc;

        return inc;
    }

private:

    std::string incident_key(
        const CorrelatedEvent& ev)
    {
        return
            std::string(
                to_string(ev.type)
            );
    }

    std::string next_incident_id()
    {
        const uint64_t n =
            counter_.fetch_add(1) + 1;

        std::ostringstream oss;

        oss
            << "INC-"
            << (1000 + n);

        return oss.str();
    }

private:

    std::mutex mutex_;

    std::unordered_map<
        std::string,
        Incident
    > active_incidents_;

    std::atomic<uint64_t>
        counter_{0};
};

}