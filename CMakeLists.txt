cmake_minimum_required(VERSION 3.16)
project(signalwire VERSION 2.0.0 LANGUAGES CXX)
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

add_library(signalwire STATIC ${SOURCES})
# Note: CPPHTTPLIB_OPENSSL_SUPPORT disabled - httplib requires OpenSSL 3.0+
# SSL connections handled externally; HMAC/crypto uses OpenSSL directly
target_link_libraries(signalwire OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
target_include_directories(signalwire PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps)

# Tests
enable_testing()
add_executable(run_tests tests/test_main.cpp)
target_link_libraries(run_tests signalwire)
target_compile_definitions(run_tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME all_tests COMMAND run_tests)

# Examples: every .cpp under examples/, rest/examples/, relay/examples/ is
# compiled against the built library. The audit_docs.py tool verifies that
# every method referenced in docs and examples resolves to a real symbol; the
# `examples` target additionally verifies each example builds clean. Add new
# examples by just dropping a .cpp file in one of the example directories —
# the glob picks it up automatically.
file(GLOB EXAMPLE_SOURCES
    "${CMAKE_SOURCE_DIR}/examples/*.cpp"
    "${CMAKE_SOURCE_DIR}/rest/examples/*.cpp"
    "${CMAKE_SOURCE_DIR}/relay/examples/*.cpp"
)

add_custom_target(examples)
foreach(example_src ${EXAMPLE_SOURCES})
    # Derive a unique target name from the file path. e.g. examples/foo.cpp
    # becomes example_foo; rest/examples/foo.cpp becomes example_rest_foo.
    file(RELATIVE_PATH rel_src "${CMAKE_SOURCE_DIR}" "${example_src}")
    string(REPLACE "/" "_" target_name "${rel_src}")
    string(REPLACE ".cpp" "" target_name "${target_name}")
    string(REPLACE "examples_" "example_" target_name "${target_name}")

    add_executable(${target_name} EXCLUDE_FROM_ALL ${example_src})
    target_link_libraries(${target_name} signalwire)
    target_compile_options(${target_name} PRIVATE -Wall -Wno-unused-variable)
    add_dependencies(examples ${target_name})
endforeach()
