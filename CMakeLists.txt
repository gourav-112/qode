cmake_minimum_required(VERSION 3.16)
project(MarketDataFeedHandler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Detect OS for platform-specific I/O multiplexing
if(APPLE)
    add_compile_definitions(USE_KQUEUE)
    message(STATUS "Using kqueue for I/O multiplexing (macOS)")
elseif(UNIX)
    add_compile_definitions(USE_EPOLL)
    message(STATUS "Using epoll for I/O multiplexing (Linux)")
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer")

# Common source files
set(COMMON_SOURCES
    src/common/latency_tracker.cpp
    src/common/cache.cpp
    src/common/memory_pool.cpp
)

# Server sources
set(SERVER_SOURCES
    src/server/tick_generator.cpp
    src/server/client_manager.cpp
    src/server/exchange_simulator.cpp
    src/server/main.cpp
)

# Client sources
set(CLIENT_SOURCES
    src/client/socket.cpp
    src/client/parser.cpp
    src/client/visualizer.cpp
    src/client/feed_handler.cpp
    src/client/main.cpp
)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Exchange Simulator executable
add_executable(exchange_simulator ${SERVER_SOURCES} ${COMMON_SOURCES})
target_link_libraries(exchange_simulator PRIVATE pthread)

# Feed Handler executable
add_executable(feed_handler ${CLIENT_SOURCES} ${COMMON_SOURCES})
target_link_libraries(feed_handler PRIVATE pthread)

# Tests (optional, requires Google Test)
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    
    add_executable(test_protocol tests/test_protocol.cpp ${COMMON_SOURCES})
    target_link_libraries(test_protocol PRIVATE GTest::gtest_main pthread)
    add_test(NAME ProtocolTests COMMAND test_protocol)
    
    add_executable(test_cache tests/test_cache.cpp ${COMMON_SOURCES})
    target_link_libraries(test_cache PRIVATE GTest::gtest_main pthread)
    add_test(NAME CacheTests COMMAND test_cache)
    
    add_executable(test_latency tests/test_latency_tracker.cpp ${COMMON_SOURCES})
    target_link_libraries(test_latency PRIVATE GTest::gtest_main pthread)
    add_test(NAME LatencyTests COMMAND test_latency)
endif()

# Installation
install(TARGETS exchange_simulator feed_handler
        RUNTIME DESTINATION bin)
