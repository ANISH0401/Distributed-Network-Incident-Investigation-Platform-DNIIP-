// Standalone read-only REST API process for the dashboard (spec Component
// 8's backend). Deliberately its own executable, separate from
// dniip-server: it has no epoll/`/proc` dependency (see
// server/api/http_api_server.hpp), so unlike dniip-server it builds and
// runs on any platform and can be scaled/deployed independently of the
// Linux-only telemetry ingestion server. Both point at the same database.
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <spdlog/spdlog.h>

#include "server/api/http_api_server.hpp"
#include "server/database/store.hpp"
#include "server/database/sqlite_store.hpp"
#include "server/database/postgres_store.hpp"

namespace {
std::atomic<bool> g_running{true};
void handle_signal(int) { g_running = false; }
} // namespace

int main(int argc, char** argv) {
    uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 8080;
    std::string db_path = argc > 2 ? argv[2] : "dniip_server.sqlite";
    const char* pg_conninfo = std::getenv("DNIIP_PG_CONNINFO");

    spdlog::info("dniip-api starting: port={} backend={}", port, pg_conninfo ? "postgres" : "sqlite");

    std::unique_ptr<dniip::server::ITelemetryStore> store;
    if (pg_conninfo) {
        store = std::make_unique<dniip::server::PostgresStore>(pg_conninfo);
    } else {
        store = std::make_unique<dniip::server::SqliteStore>(db_path);
    }

    dniip::server::HttpApiServer api(port, *store);
    api.start();

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    api.stop();
    spdlog::info("dniip-api shut down cleanly");
    return 0;
}
