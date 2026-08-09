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
option(PROGRESSIVE_CORE_UNITY "Merge progressive_native sources into unity groups" OFF)
option(PROGRESSIVE_CORE_USE_ARTIFACT
       "Download a prebuilt core (per-commit GitHub release) instead of compiling" OFF)

# --- Prebuilt artifact (optional) ---
# When consumers set PROGRESSIVE_CORE_USE_ARTIFACT=ON, we try to download
# libprogressive_native.a + libprogressive_core.a for THIS commit and THIS
# architecture from the core-<sha> release. On any failure we silently fall
# back to building from source. Headers always come from the fetched source.
set(PROGRESSIVE_CORE_ARTIFACT_USED OFF CACHE BOOL "" FORCE)
if(PROGRESSIVE_CORE_USE_ARTIFACT)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(CORE_ARTIFACT_ARCH "aarch64")
    else()
        set(CORE_ARTIFACT_ARCH "x86_64")
    endif()
    execute_process(
        COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE CORE_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CORE_SHA_RC)
    if(CORE_SHA_RC EQUAL 0 AND CORE_SHA)
        set(CORE_ARTIFACT_URL
            "https://github.com/progressive-chat/progressive-core/releases/download/core-${CORE_SHA}/progressive-core-${CORE_ARTIFACT_ARCH}.tar.gz")
        set(CORE_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/progressive-core-artifact")
        file(MAKE_DIRECTORY "${CORE_ARTIFACT_DIR}")
        file(DOWNLOAD "${CORE_ARTIFACT_URL}" "${CORE_ARTIFACT_DIR}/core.tar.gz"
             STATUS CORE_DL_STATUS TLS_VERIFY ON TIMEOUT 60)
        list(GET CORE_DL_STATUS 0 CORE_DL_RC)
        if(CORE_DL_RC EQUAL 0)
            file(MAKE_DIRECTORY "${CORE_ARTIFACT_DIR}/out")
            execute_process(COMMAND tar xzf "${CORE_ARTIFACT_DIR}/core.tar.gz" -C "${CORE_ARTIFACT_DIR}/out"
                            RESULT_VARIABLE CORE_TAR_RC)
            if(CORE_TAR_RC EQUAL 0 AND EXISTS "${CORE_ARTIFACT_DIR}/out/libprogressive_core.a")
                add_library(progressive_native STATIC IMPORTED GLOBAL)
                set_target_properties(progressive_native PROPERTIES
                    IMPORTED_LOCATION "${CORE_ARTIFACT_DIR}/out/libprogressive_native.a"
                    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
                add_library(progressive_core STATIC IMPORTED GLOBAL)
                set_target_properties(progressive_core PROPERTIES
                    IMPORTED_LOCATION "${CORE_ARTIFACT_DIR}/out/libprogressive_core.a"
                    INTERFACE_INCLUDE_DIRECTORIES
                        "${CMAKE_CURRENT_SOURCE_DIR}/src;${CMAKE_CURRENT_BINARY_DIR}/generated")
                set(PROGRESSIVE_CORE_ARTIFACT_USED ON CACHE BOOL "" FORCE)
                message(STATUS "progressive-core: using prebuilt artifact (${CORE_ARTIFACT_ARCH}, ${CORE_SHA})")
            endif()
        endif()
        if(NOT PROGRESSIVE_CORE_ARTIFACT_USED)
            message(STATUS "progressive-core: artifact not available (sha=${CORE_SHA} arch=${CORE_ARTIFACT_ARCH}) — building from source")
        endif()
    endif()
endif()

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

find_package(OpenSSL REQUIRED)

# SQLite3::SQLite3 exists on modern CMake; older CI images only have the
# deprecated SQLite::SQLite3 alias — pick whichever is available.
set(_sqlite_target SQLite::SQLite3)
if(TARGET SQLite3::SQLite3)
    set(_sqlite_target SQLite3::SQLite3)
endif()

if(NOT PROGRESSIVE_CORE_ARTIFACT_USED)
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

target_link_libraries(progressive_core PUBLIC
    progressive_native
    CURL::libcurl
    ${_sqlite_target}
    simdjson::simdjson
    PkgConfig::SODIUM
    OpenSSL::Crypto
)
endif()  # NOT PROGRESSIVE_CORE_ARTIFACT_USED

if(PROGRESSIVE_CORE_ARTIFACT_USED)
    # Deferred: the dep targets only exist after the block above.
    target_link_libraries(progressive_native INTERFACE olm)
    target_link_libraries(progressive_core INTERFACE
        progressive_native
        CURL::libcurl
        ${_sqlite_target}
        simdjson::simdjson
        PkgConfig::SODIUM
        OpenSSL::Crypto)
endif()

# --- Tests (Qt-free — the reason this repo exists) ---
if(PROGRESSIVE_BUILD_TESTS)
    enable_testing()

    # The vendored native modules rely on the Android log shim (the same way
    # the desktop's tests force-include progressive_compat.h).
    set(PN_SHIM "${CMAKE_CURRENT_SOURCE_DIR}/native_shim/progressive_compat.h")

    add_executable(test_sync_applier tests/test_sync_applier.cpp)
    target_link_libraries(test_sync_applier PRIVATE progressive_core)
    add_test(NAME sync_applier COMMAND test_sync_applier)

    add_executable(test_register_flows tests/test_register_flows.cpp)
    target_link_libraries(test_register_flows PRIVATE progressive_core)
    add_test(NAME register_flows COMMAND test_register_flows)

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
