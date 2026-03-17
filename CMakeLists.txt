cmake_minimum_required(VERSION 3.16)
project(signalwire_agents VERSION 1.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only deps
include_directories(${CMAKE_SOURCE_DIR}/deps)
include_directories(${CMAKE_SOURCE_DIR}/include)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# All sources
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(signalwire_agents STATIC ${SOURCES})
# Note: CPPHTTPLIB_OPENSSL_SUPPORT disabled - httplib requires OpenSSL 3.0+
# SSL connections handled externally; HMAC/crypto uses OpenSSL directly
target_link_libraries(signalwire_agents OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_include_directories(signalwire_agents PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps)

# Tests
enable_testing()
add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests signalwire_agents)
add_test(NAME all_tests COMMAND run_tests)
