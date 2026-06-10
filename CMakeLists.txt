cmake_minimum_required(VERSION 3.16)
project(signalwire VERSION 2.0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only deps
include_directories(${CMAKE_SOURCE_DIR}/deps)
include_directories(${CMAKE_SOURCE_DIR}/include)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Threads
find_package(Threads REQUIRED)

# IXWebSocket — cross-platform RFC 6455 WebSocket client with OpenSSL-backed
# TLS, used by the RELAY transport (src/relay/websocket.cpp). Fetched at
# configure time (the build env has network); pinned to a known-good tag.
# USE_TLS + USE_OPEN_SSL select the OpenSSL backend so wss:// verifies the
# server cert against the system store / SSL_CERT_FILE. Requires OpenSSL 3.0+,
# same toolchain bar as cpp-httplib's TLS above.
include(FetchContent)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# All sources
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(signalwire STATIC ${SOURCES})
# cpp-httplib TLS enabled — requires OpenSSL 3.0+ (build with a 3.0+ toolchain;
# OpenSSL 1.1.1 is EOL). Gives httplib::Client https:// for REST and
# httplib::SSLServer for the webhook server (in-process TLS termination).
target_compile_definitions(signalwire PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
# PROJECT_SOURCE_DIR must be visible to src/utils/schema_utils.cpp, which lives
# in THIS library (not just run_tests) — its default_schema_candidates() ladder
# uses ``#ifdef PROJECT_SOURCE_DIR`` as the first (absolute) candidate for
# src/swml/schema.json. Without it the library only carries the cwd-relative
# fallbacks (./schema.json, ./src/swml/schema.json, ../src/swml/schema.json),
# which don't resolve when run_tests runs from an out-of-source build dir (e.g.
# the CI's /tmp/run-ci-build), leaving SchemaUtils with zero verbs. Defining it
# on the library (PRIVATE — it's an implementation detail of the .cpp) makes the
# absolute candidate available wherever the library is linked.
target_compile_definitions(signalwire PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(signalwire OpenSSL::SSL OpenSSL::Crypto Threads::Threads ixwebsocket)
# cpp-httplib >= 0.16 loads the OS trust store natively on Apple platforms
# (httplib::tls::load_system_certs via SecTrustSettingsCopyCertificates), which
# needs the CoreFoundation + Security frameworks. PUBLIC so the link propagates
# to anything that links signalwire (e.g. run_tests / emit_corpus). Harmless on
# non-Apple hosts (guarded by APPLE).
if(APPLE)
    target_link_libraries(signalwire "-framework CoreFoundation" "-framework Security")
endif()
target_include_directories(signalwire PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/deps)
# src/ is PRIVATE: internal-only headers (e.g. server/tls_server.hpp, which
# pulls in httplib.h for SSLServer) live under src/ so they stay out of the
# public surface AND out of the signature enumerator (which scans include/
# only). Consumers never see them.
target_include_directories(signalwire PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Tests
enable_testing()
add_executable(run_tests
    tests/test_main.cpp
    tests/mocktest.cpp
    tests/relay_mocktest.cpp
    tests/tls_mocktest.cpp
)
target_link_libraries(run_tests signalwire)
target_compile_definitions(run_tests PRIVATE PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME all_tests COMMAND run_tests)

# emit_corpus — the EMISSION-DUMP program for the cross-port emission differ
# (porting-sdk/scripts/diff_port_emission.py). Builds the shared 81-entry
# FunctionResult corpus natively and prints {id -> to_json()} as one canonical
# JSON object on stdout. The differ (run via the EMISSION gate in
# scripts/run-ci.sh) byte-compares it against Python's to_dict() oracle. Mirrors
# the Go reference dump (signalwire-go/cmd/emit-corpus).
add_executable(emit_corpus tools/emit_corpus.cpp)
target_link_libraries(emit_corpus signalwire)

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
