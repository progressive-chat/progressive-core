# CMake generated Testfile for 
# Source directory: /tmp/opencode/progressive-core
# Build directory: /tmp/opencode/progressive-core/build-rel
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("sync_applier" "/tmp/opencode/progressive-core/build-rel/test_sync_applier")
set_tests_properties("sync_applier" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;182;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("register_flows" "/tmp/opencode/progressive-core/build-rel/test_register_flows")
set_tests_properties("register_flows" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;186;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("media_crypto" "/tmp/opencode/progressive-core/build-rel/test_media_crypto")
set_tests_properties("media_crypto" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;190;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("phase1" "/tmp/opencode/progressive-core/build-rel/test_phase1")
set_tests_properties("phase1" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;195;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("olm_inbound" "/tmp/opencode/progressive-core/build-rel/test_olm_inbound")
set_tests_properties("olm_inbound" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;200;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("megolm_inbound" "/tmp/opencode/progressive-core/build-rel/test_megolm_inbound")
set_tests_properties("megolm_inbound" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;205;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("e2ee_account" "/tmp/opencode/progressive-core/build-rel/test_e2ee_account")
set_tests_properties("e2ee_account" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;210;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("e2ee_otk_count" "/tmp/opencode/progressive-core/build-rel/test_e2ee_otk_count")
set_tests_properties("e2ee_otk_count" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;215;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("e2ee_sas" "/tmp/opencode/progressive-core/build-rel/test_e2ee_sas")
set_tests_properties("e2ee_sas" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;220;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("e2ee_verify_protocol" "/tmp/opencode/progressive-core/build-rel/test_e2ee_verify_protocol")
set_tests_properties("e2ee_verify_protocol" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;225;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("e2ee_store" "/tmp/opencode/progressive-core/build-rel/test_e2ee_store")
set_tests_properties("e2ee_store" PROPERTIES  _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;230;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
add_test("synapse_e2ee" "/tmp/opencode/progressive-core/build-rel/test_synapse_e2ee")
set_tests_properties("synapse_e2ee" PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "/tmp/opencode/progressive-core/CMakeLists.txt;237;add_test;/tmp/opencode/progressive-core/CMakeLists.txt;0;")
subdirs("_deps/olm-build")
subdirs("_deps/simdjson-build")
