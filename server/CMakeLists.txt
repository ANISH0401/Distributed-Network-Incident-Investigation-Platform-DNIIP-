# Portable engines: correlation/incident/RCA + models are pure C++20 with no
# Linux-only dependencies, so they build and are unit-testable on any host
# (including macOS while iterating). Only epoll_server/collector/main
# require Linux (epoll, raw sockets) and are gated below.
add_library(dniip_server_engines INTERFACE)
target_include_directories(dniip_server_engines INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(dniip_server_engines INTERFACE dniip_common Threads::Threads)

add_library(dniip_server_store INTERFACE)
target_include_directories(dniip_server_store INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(dniip_server_store INTERFACE dniip_server_engines SQLite::SQLite3 PostgreSQL::PostgreSQL)

# dniip-api (REST API for the dashboard) has no epoll/`/proc` dependency,
# so unlike dniip-server it builds and runs on every platform.
add_executable(dniip-api api_main.cpp)
target_link_libraries(dniip-api PRIVATE dniip_server_store Threads::Threads)
install(TARGETS dniip-api RUNTIME DESTINATION bin)

if(DNIIP_LINUX_TARGETS)
    add_library(dniip_epoll_server epoll_server/epoll_server.cpp)
    target_include_directories(dniip_epoll_server PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(dniip_epoll_server PUBLIC dniip_common)

    add_executable(dniip-server main.cpp)
    target_link_libraries(dniip-server PRIVATE
        dniip_server_store
        dniip_epoll_server
    )
    install(TARGETS dniip-server RUNTIME DESTINATION bin)
endif()
