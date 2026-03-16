cmake_minimum_required(VERSION 3.16)
project(signalwire_agents VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only deps
include_directories(${CMAKE_SOURCE_DIR}/deps)
include_directories(${CMAKE_SOURCE_DIR}/include)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# Phase 1-3 sources (explicit list — Phase 4+ files excluded until their headers exist)
set(SOURCES
    src/logging/logger.cpp
    src/swml/document.cpp
    src/swml/schema.cpp
    src/swml/service.cpp
    src/swaig/function_result.cpp
    src/security/session_manager.cpp
    src/datamap/datamap.cpp
    src/contexts/contexts.cpp
)

add_library(signalwire_agents STATIC ${SOURCES})
target_link_libraries(signalwire_agents OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_include_directories(signalwire_agents PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps)

# Tests
enable_testing()
add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests signalwire_agents)
add_test(NAME all_tests COMMAND run_tests)
