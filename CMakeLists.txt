# progressive-core — Canonical Qt-free Matrix core (E2EE + sync + CS API).
# Shared by progressive-desktop and progressive-cli (and future frontends).
#
# Build:
#   cmake -S . -B build
#   cmake --build build -j4
#   ctest --test-dir build
cmake_minimum_required(VERSION 3.22)

project(progressive-core
    VERSION 0.1.0
    DESCRIPTION "Qt-free Matrix core: E2EE (Olm/Megolm), /sync, CS API — shared by all progressive frontends"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# --- Version header (configured) ---
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/version.h
    @ONLY
)

# --- Options ---
option(PROGRESSIVE_BUILD_TESTS "Build tests" ON)

# --- libcurl (HTTP client with TLS + SOCKS5/Tor proxy) ---
find_package(CURL REQUIRED COMPONENTS HTTPS)

# --- Vendored progressive_native (built from native/ + native_shim/) ---
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/progressive_native.cmake)

# --- The core library ---
find_package(SQLite3 REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SODIUM QUIET IMPORTED_TARGET libsodium)
endif()
if(NOT TARGET PkgConfig::SODIUM)
    find_library(SODIUM_LIBRARY sodium REQUIRED)
    find_path(SODIUM_INCLUDE_DIR sodium.h REQUIRED)
    add_library(PkgConfig::SODIUM UNKNOWN IMPORTED)
    set_target_properties(PkgConfig::SODIUM PROPERTIES
        IMPORTED_LOCATION "${SODIUM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
endif()

add_library(progressive_core STATIC
    src/core/http_client.cpp
    src/core/json_utils.cpp
    src/core/matrix_client.cpp
    src/core/session_store.cpp
    src/core/sync_engine.cpp
    src/core/fast_sync.cpp
    src/core/memory_stats.cpp
    src/core/thread_pool.cpp
    src/core/crypto/ed25519.cpp
    src/core/crypto/olm_account.cpp
    src/core/crypto/random.cpp
    src/core/crypto/sig_verify.cpp
    src/core/crypto/sas.cpp
    src/core/crypto/sas_emojis.cpp
    src/core/crypto/verification.cpp
    src/core/crypto/verify_controller.cpp
    src/core/crypto/megolm_store.cpp
    src/core/crypto/cross_sign.cpp
    src/core/crypto/key_backup.cpp
    src/core/crypto/media_crypto.cpp
    src/core/crypto/recovery_key.cpp
    src/core/crypto/ssss.cpp
    src/core/engine/room_state.cpp
    src/core/engine/sync_applier.cpp
    src/core/engine/timeline_state.cpp
    src/core/crypto/backup_crypto.cpp
    src/core/crypto/decryptor.cpp
)
target_include_directories(progressive_core PUBLIC
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
find_package(OpenSSL REQUIRED)

# SQLite3::SQLite3 exists on modern CMake; older CI images only have the
# deprecated SQLite::SQLite3 alias — pick whichever is available.
set(_sqlite_target SQLite::SQLite3)
if(TARGET SQLite3::SQLite3)
    set(_sqlite_target SQLite3::SQLite3)
endif()

target_link_libraries(progressive_core PUBLIC
    progressive_native
    CURL::libcurl
    ${_sqlite_target}
    simdjson::simdjson
    PkgConfig::SODIUM
    OpenSSL::Crypto
)

# --- Tests (Qt-free — the reason this repo exists) ---
if(PROGRESSIVE_BUILD_TESTS)
    enable_testing()

    # The vendored native modules rely on the Android log shim (the same way
    # the desktop's tests force-include progressive_compat.h).
    set(PN_SHIM "${CMAKE_CURRENT_SOURCE_DIR}/native_shim/progressive_compat.h")

    add_executable(test_sync_applier tests/test_sync_applier.cpp)
    target_link_libraries(test_sync_applier PRIVATE progressive_core)
    add_test(NAME sync_applier COMMAND test_sync_applier)

    add_executable(test_media_crypto tests/test_media_crypto.cpp)
    target_link_libraries(test_media_crypto PRIVATE progressive_core)
    add_test(NAME media_crypto COMMAND test_media_crypto)

    add_executable(test_phase1 tests/test_phase1.cpp)
    target_link_libraries(test_phase1 PRIVATE progressive_core)
    target_compile_options(test_phase1 PRIVATE -include "${PN_SHIM}")
    add_test(NAME phase1 COMMAND test_phase1)

    add_executable(test_olm_inbound tests/test_olm_inbound.cpp)
    target_link_libraries(test_olm_inbound PRIVATE progressive_core)
    target_compile_options(test_olm_inbound PRIVATE -include "${PN_SHIM}")
    add_test(NAME olm_inbound COMMAND test_olm_inbound)

    add_executable(test_megolm_inbound tests/test_megolm_inbound.cpp)
    target_link_libraries(test_megolm_inbound PRIVATE progressive_core)
    target_compile_options(test_megolm_inbound PRIVATE -include "${PN_SHIM}")
    add_test(NAME megolm_inbound COMMAND test_megolm_inbound)

    add_executable(test_e2ee_account tests/test_e2ee_account.cpp)
    target_link_libraries(test_e2ee_account PRIVATE progressive_core)
    target_compile_options(test_e2ee_account PRIVATE -include "${PN_SHIM}")
    add_test(NAME e2ee_account COMMAND test_e2ee_account)

    add_executable(test_e2ee_otk_count tests/test_e2ee_otk_count.cpp)
    target_link_libraries(test_e2ee_otk_count PRIVATE progressive_core)
    target_compile_options(test_e2ee_otk_count PRIVATE -include "${PN_SHIM}")
    add_test(NAME e2ee_otk_count COMMAND test_e2ee_otk_count)

    add_executable(test_e2ee_sas tests/test_e2ee_sas.cpp)
    target_link_libraries(test_e2ee_sas PRIVATE progressive_core)
    target_compile_options(test_e2ee_sas PRIVATE -include "${PN_SHIM}")
    add_test(NAME e2ee_sas COMMAND test_e2ee_sas)

    add_executable(test_e2ee_verify_protocol tests/test_e2ee_verify_protocol.cpp)
    target_link_libraries(test_e2ee_verify_protocol PRIVATE progressive_core)
    target_compile_options(test_e2ee_verify_protocol PRIVATE -include "${PN_SHIM}")
    add_test(NAME e2ee_verify_protocol COMMAND test_e2ee_verify_protocol)

    add_executable(test_e2ee_store tests/test_e2ee_store.cpp)
    target_link_libraries(test_e2ee_store PRIVATE progressive_core)
    target_compile_options(test_e2ee_store PRIVATE -include "${PN_SHIM}")
    add_test(NAME e2ee_store COMMAND test_e2ee_store)

    # Live-Synapse E2EE integration test (CI: synapse-e2ee workflow).
    # Skips gracefully when no server is reachable (no SYNAPSE_URL set).
    add_executable(test_synapse_e2ee tests/test_synapse_e2ee.cpp)
    target_link_libraries(test_synapse_e2ee PRIVATE progressive_core)
    target_compile_options(test_synapse_e2ee PRIVATE -include "${PN_SHIM}")
    add_test(NAME synapse_e2ee COMMAND test_synapse_e2ee)
    set_tests_properties(synapse_e2ee PROPERTIES TIMEOUT 120)
endif()
