find_package(GTest CONFIG REQUIRED)

add_executable(Tests
    src/ArchiveTests.cpp
    src/BoardTests.cpp
    src/CoreTests.cpp
    src/JobQueueTests.cpp
    src/ProtocolTests.cpp
    # The board is pure logic with no dependency on the instance, so it is
    # compiled straight into the test binary.
    ../InstanceServer/src/game/Board.cpp
)

target_include_directories(Tests PRIVATE ../InstanceServer/include)

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
