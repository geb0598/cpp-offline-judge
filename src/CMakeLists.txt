set(COJ_SOURCES
    checker.cpp
    compiler.cpp
    file_descriptor.cpp
    file_io.cpp
    process.cpp
    runner.cpp
)

add_library(coj STATIC ${COJ_SOURCES})

target_include_directories(coj PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)