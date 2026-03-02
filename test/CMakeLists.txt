# test 디렉토리 기준의 경로
set(TEST_SOURCES
    src/compiler_test.cpp
    src/file_descriptor_test.cpp
    src/file_io_test.cpp
    src/process_test.cpp
    src/runner_test.cpp
)

add_executable(coj_tests ${TEST_SOURCES})

target_link_libraries(coj_tests
    PRIVATE
    coj
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(coj_tests)