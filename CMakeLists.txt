cmake_minimum_required(VERSION 3.20)
project(dniip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(nlohmann_json 3.11 REQUIRED)
find_package(spdlog REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)

# Linux-only components (epoll, /proc, ss/ip diagnostics). On other platforms
# (e.g. building/inspecting this tree on macOS) only the platform-independent
# pieces (correlation/incident/RCA engines, common protocol) are built; use
# docker/ to build and run the full stack on Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DNIIP_LINUX_TARGETS ON)
else()
    set(DNIIP_LINUX_TARGETS OFF)
    message(WARNING "Non-Linux host detected (${CMAKE_SYSTEM_NAME}): agent and epoll_server "
                     "targets require Linux (epoll, /proc). Build them via docker/ instead.")
endif()

enable_testing()

add_library(dniip_common INTERFACE)
target_include_directories(dniip_common INTERFACE ${CMAKE_SOURCE_DIR}/common)
target_link_libraries(dniip_common INTERFACE nlohmann_json::nlohmann_json spdlog::spdlog)

add_subdirectory(server)
add_subdirectory(scripts)

if(DNIIP_LINUX_TARGETS)
    add_subdirectory(agent)
endif()

if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()
