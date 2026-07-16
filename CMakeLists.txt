cmake_minimum_required(VERSION 3.16)
project(signalwire VERSION 3.2.1
        HOMEPAGE_URL "https://github.com/signalwire/signalwire-cpp"
        LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ccache — compiler-cache launcher, availability-gated so its ABSENCE is a
# strict no-op (the build never fails for a missing ccache). When present it
# wraps the C/C++ compiler invocations and makes warm (unchanged-source)
# rebuilds near-instant. Declared as a dev dependency in scripts/_env.sh (local
# brew hint) and the porting-sdk cross-port.yml cpp matrix install step.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

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
    GIT_TAG        v12.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# All sources
file(GLOB_RECURSE SOURCES "src/*.cpp")

# SHARED (not STATIC) so the published/installed library carries its transitive
# link dependencies (OpenSSL, ixwebsocket, zlib, and on Apple the CoreFoundation/
# Security frameworks) as recorded DT_NEEDED / load-command entries. That is what
# lets a downstream consumer link with a bare `-lsignalwire` (no need to re-list
# every private dep) — a static archive cannot convey those, so a plain
# `g++ … -lsignalwire` against a static build fails to resolve OpenSSL/ixwebsocket
# symbols (PACKAGE-SMOKE #92). The internal tools/tests below link it the same way.
add_library(signalwire SHARED ${SOURCES})
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

# Record the shared library's own ABSOLUTE installed location as its runtime name
# so a consumer that links it with a bare `-lsignalwire` (no rpath, no
# LD_LIBRARY_PATH) still finds it at load time from wherever it was installed.
#   macOS: INSTALL_NAME_DIR bakes an absolute install-name (LC_ID_DYLIB) instead
#          of the default `@rpath/…` (which a plain consumer has no LC_RPATH for),
#          so the consumer's load command is the absolute dylib path.
#   ELF:   bake an ABSOLUTE SONAME (`-Wl,-soname,<abs>`) so the consumer's
#          DT_NEEDED is the absolute .so path — ld.so loads it directly without a
#          DT_RPATH/RUNPATH or LD_LIBRARY_PATH on the consumer side.
# Uses the absolute libdir so the value is correct regardless of CMAKE_INSTALL_PREFIX.
include(GNUInstallDirs)
set_target_properties(signalwire PROPERTIES
    INSTALL_NAME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")
if(NOT APPLE AND NOT WIN32)
    target_link_options(signalwire PRIVATE
        "LINKER:-soname,${CMAKE_INSTALL_FULL_LIBDIR}/libsignalwire.so")
endif()

# ── Install rules ────────────────────────────────────────────────────────────
# What a consumer gets from `cmake --install` (or `--build --target install`):
# the built shared library into <prefix>/lib and every PUBLIC header into
# <prefix>/include, so `g++ -I<prefix>/include -L<prefix>/lib -lsignalwire`
# compiles, links, AND runs against nothing but the installed artifact. Without
# these the package ships nothing (PACKAGE-SMOKE #92). Only the public library +
# headers are installed — the internal audit/dump tools (emit_corpus /
# emit_skills / route_registry / *_dump), run_tests, and the examples are
# intentionally NOT installed; they're build-time verification programs, not part
# of the package.

# The library -> <prefix>/lib (libsignalwire.{so,dylib}) / <prefix>/bin on Windows.
install(TARGETS signalwire
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Public headers -> <prefix>/include/signalwire/... (preserves the include tree
# so `#include <signalwire/rest/rest_client.hpp>` resolves from the install).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/signalwire
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Vendored header-only dep the PUBLIC headers pull in. The ONLY vendored header a
# consumer's translation unit actually needs is nlohmann/json.hpp: every public
# header includes `<nlohmann/json.hpp>` (common.hpp too, normalized off the bare
# "json.hpp"). httplib is NEVER #included by a public header — the ones that use it
# only FORWARD-DECLARE `namespace httplib { ... }` and take httplib types by
# reference — so httplib.h must NOT be shipped: dumping it (and a bare json.hpp)
# into <prefix>/include pollutes the consumer's include root with unnamespaced
# `httplib.h` / `json.hpp` that collide with the consumer's own or another lib's
# vendored copy. Install ONLY nlohmann/json.hpp, and materialize it as a real file
# (deps/nlohmann/json.hpp is a relative symlink to ../json.hpp; installing the
# canonical deps/json.hpp AS <prefix>/include/nlohmann/json.hpp dereferences the
# link so the install tree carries no dangling symlink and no root-level pollutant).
# Result: <prefix>/include holds exactly `signalwire/` and `nlohmann/json.hpp`, so
# `-I<prefix>/include` resolves `<nlohmann/json.hpp>` and nothing else leaks.
install(FILES ${CMAKE_SOURCE_DIR}/deps/json.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nlohmann
    RENAME json.hpp)

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

# emit_skills — the SKILL-DUMP program for the cross-port SKILL-CONTRACT differ
# (porting-sdk/scripts/diff_skill_contracts.py). For each covered skill it
# instantiates the registered skill, runs setup() with the shared corpus config,
# and prints {id -> [{name, parameters, required?}]} from both register_tools()
# and get_datamap_functions(). The differ (run via the SKILL-CONTRACT gate in
# scripts/run-ci.sh) compares it against the Python reference. Mirrors the Java
# port's EmitSkills and Go's cmd/emit-skills. Built in the TEST gate alongside
# emit_corpus so the SKILL-CONTRACT gate has a ready binary.
add_executable(emit_skills tools/emit_skills.cpp)
target_link_libraries(emit_skills signalwire)

# route_registry — the SPEC-PARITY Set-B producer for the SPEC-PARITY gate
# (porting-sdk/scripts/diff_spec_implementation.py --registry-json). C++ has no
# runtime reflection, so it derives Set B from REAL dispatch: it stands up a
# loopback httplib server that records every (method, path) it receives, points
# a RestClient at it, invokes every public namespace method once, and prints the
# routes the SDK actually sent as {"routes","skipped","errors"} JSON. The route
# strings come from the SDK's own path-building code in rest_client.hpp — never
# hand-authored — so Set B cannot drift from what the client really does. Same
# Option-2 approach as Rust's src/bin/route_registry.rs. Built in the TEST gate
# alongside emit_corpus/emit_skills so the SPEC-PARITY gate has a ready binary.
add_executable(route_registry tools/route_registry.cpp)
target_link_libraries(route_registry signalwire)

# ── Layer-D per-surface dump programs ────────────────────────────────────────
# One dump per behavioral surface for the porting-sdk per-surface differs
# (porting-sdk/scripts/diff_port_{wire,swml,state,http,wire_relay}.py). Each
# builds the shared corpus natively, emits {case_id -> observable-artifact} as
# one canonical JSON object on stdout, and the differ byte-compares it against
# the Python oracle. Mirror the Go reference dumps
# (signalwire-go/cmd/{wire,swml,state,http,wire-relay}-dump).
add_executable(wire_dump tools/wire_dump.cpp)
target_link_libraries(wire_dump signalwire)

add_executable(swml_dump tools/swml_dump.cpp)
target_link_libraries(swml_dump signalwire)

add_executable(state_dump tools/state_dump.cpp)
target_link_libraries(state_dump signalwire)

add_executable(http_dump tools/http_dump.cpp)
target_link_libraries(http_dump signalwire)

# wire_relay_dump links the RELAY client + stands up an in-process mock WS
# server (IXWebSocket) to capture the frames the client sends. It uses the
# IXWebSocket server headers directly, so link ixwebsocket explicitly.
add_executable(wire_relay_dump tools/wire_relay_dump.cpp)
target_link_libraries(wire_relay_dump signalwire ixwebsocket)

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
