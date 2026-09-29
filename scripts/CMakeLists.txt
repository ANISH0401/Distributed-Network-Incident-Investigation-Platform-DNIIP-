add_executable(dniip-seed-sample-data seed_sample_data.cpp)
target_link_libraries(dniip-seed-sample-data PRIVATE dniip_server_store)
