find_package(GTest CONFIG REQUIRED)

add_executable(Tests
    src/ArchiveTests.cpp
    src/CoreTests.cpp
    src/JobQueueTests.cpp
    src/ProtocolTests.cpp
)

target_link_libraries(Tests PRIVATE
    ServerCore
    Protocol
    Common
    GTest::gtest
    GTest::gtest_main
    nhn_compile_options
)

include(GoogleTest)
gtest_discover_tests(Tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 120
)
