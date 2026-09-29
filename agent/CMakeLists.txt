# Only added to the build on Linux (see top-level CMakeLists.txt) since
# it depends on /proc, /sys/class/net, and raw ICMP sockets.
add_executable(dniip-agent service/main.cpp)
target_include_directories(dniip-agent PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(dniip-agent PRIVATE dniip_common SQLite::SQLite3 Threads::Threads)
install(TARGETS dniip-agent RUNTIME DESTINATION bin)
