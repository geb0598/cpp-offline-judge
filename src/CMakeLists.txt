# cpp-offline-judge/src/CMakeLists.txt

file(GLOB CPP_OFFLINE_JUDGE_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(cpp-offline-judge STATIC
    ${CPP_OFFLINE_JUDGE_SOURCES}
)

target_include_directories(cpp-offline-judge PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)