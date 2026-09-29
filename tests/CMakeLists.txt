find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(dniip_server_tests
    server/correlation_engine_test.cpp
    server/incident_rca_test.cpp
    server/metrics_registry_test.cpp
    server/http_metrics_server_test.cpp
    server/http_api_server_test.cpp
    server/postgres_store_test.cpp
)
target_link_libraries(dniip_server_tests PRIVATE
    dniip_server_store
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(dniip_server_tests)
