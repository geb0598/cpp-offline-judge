# cpp-offline-judge/test/src/CMakeLists.txt

find_package(GTest REQUIRED)
enable_testing()

# file(GLOB TEST_SOURCES CONFIGURE_DEPENDS *.cpp)
set(TEST_SOURCES exception.cpp
    pipe.cpp)

foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/test/include
    )
    target_link_libraries(${TEST_NAME} PRIVATE 
        cpp-offline-judge
        GTest::gtest_main
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()