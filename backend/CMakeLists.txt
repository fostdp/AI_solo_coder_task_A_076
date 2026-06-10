cmake_minimum_required(VERSION 3.16)
project(cavitation_monitor VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.3
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
    clickhouse_cpp
    GIT_REPOSITORY https://github.com/ClickHouse/clickhouse-cpp.git
    GIT_TAG v2.5.1
)
FetchContent_MakeAvailable(clickhouse_cpp)

if(WIN32)
    find_package(lz4 REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)
endif()

add_executable(cavitation_monitor
    src/main.cpp
    src/config_loader.cpp
    src/udp_receiver.cpp
    src/signal_processor.cpp
    src/pxi_collector.cpp
    src/cavitation_detector.cpp
    src/cavitation_service.cpp
    src/fatigue_analyzer.cpp
    src/fatigue_service.cpp
    src/alarm_manager.cpp
    src/alarm_service.cpp
    src/clickhouse_writer.cpp
    src/api_server.cpp
)

target_include_directories(cavitation_monitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cavitation_monitor PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    httplib::httplib
    clickhouse-cpp-lib
)

if(WIN32)
    target_link_libraries(cavitation_monitor PRIVATE ws2_32)
    target_compile_definitions(cavitation_monitor PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
else()
    target_link_libraries(cavitation_monitor PRIVATE PkgConfig::LZ4)
endif()

target_compile_options(cavitation_monitor PRIVATE
    -Wall -Wextra -O2
)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(cavitation_monitor PRIVATE /W4 /O2)
    string(REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()
