add_executable(pulsenet_tests
    test_models.cpp
    test_config.cpp
    test_persistence.cpp
    test_health.cpp
    test_probes.cpp
    test_incidents.cpp
    test_scheduler.cpp
    test_api.cpp
)

target_link_libraries(pulsenet_tests PRIVATE pulsenet_core GTest::gtest_main)
target_include_directories(pulsenet_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
pulsenet_set_warnings(pulsenet_tests)

include(GoogleTest)
gtest_discover_tests(pulsenet_tests DISCOVERY_TIMEOUT 30)
