cmake_minimum_required(VERSION 3.16)

project(furble_host_camera LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opt-in source-based coverage instrumentation, used by tools/coverage.py.
# Off by default so the plain configure and the existing CI job are unchanged.
# Configure with -DFURBLE_COVERAGE=ON, or export FURBLE_COVERAGE=1, and every
# test binary writes a raw profile to the path in LLVM_PROFILE_FILE.
option(FURBLE_COVERAGE "Build the host harness with clang coverage" OFF)
if(NOT FURBLE_COVERAGE AND DEFINED ENV{FURBLE_COVERAGE})
  if("$ENV{FURBLE_COVERAGE}" STREQUAL "1")
    set(FURBLE_COVERAGE ON)
  endif()
endif()
if(FURBLE_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
endif()

# Where each test writes its raw profile. tools/coverage.py passes the directory
# it collects from, and the block at the end of this file names every test's
# profile after the test. That is what lets a lost measurement be reported by
# test name instead of as an anonymous empty file (issue #277). Empty outside a
# coverage run, and then LLVM_PROFILE_FILE from the environment applies as
# before.
set(FURBLE_PROFILE_DIR "" CACHE PATH
    "Directory each test writes its raw coverage profile into")

set(FURBLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(HOST_NIMBLE_DIR ${FURBLE_ROOT}/lib/testing/nimble)
set(HOST_PEER_DIR ${FURBLE_ROOT}/lib/testing/peer)
set(HOST_CORPUS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
set(HOST_MQTT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mqtt)

# TestSyncController.cpp implements the FURBLE_TEST_SYNC named sync points for
# every camera test that links this library, and FURBLE_TEST_SYNC below compiles
# the points into these sources. A point with no barrier or callback armed on
# its name returns immediately, so this changes nothing for the tests that do
# not use one. The registration gate test arms the Fujifilm point.
add_library(furble_host_camera STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/testsync/TestSyncController.cpp
            ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
            ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
            ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
            ${FURBLE_ROOT}/lib/furble/Camera.cpp
            ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
            ${FURBLE_ROOT}/lib/furble/Device.cpp
            ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
            ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
            # Fujifilm.cpp and FujifilmBasic.cpp delegate their wire encoding to
            # the shared protocol module landed by the Tier A/B tests. The
            # end-to-end harness compiles the real production sources, so it
            # links the same helper the firmware uses.
            ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp
            ${FURBLE_ROOT}/src/FurbleTimezone.cpp)

target_include_directories(furble_host_camera
                           PUBLIC ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR}
                                  ${HOST_CORPUS_DIR}
                                  ${CMAKE_CURRENT_SOURCE_DIR}/testsync
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble)
target_compile_options(furble_host_camera PRIVATE -Wall -Wextra -Werror -Wno-format
                                                 -Wno-unused-parameter)
target_compile_definitions(furble_host_camera
                           PRIVATE FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100 FURBLE_TEST_SYNC)
target_compile_definitions(furble_host_camera PUBLIC FURBLE_HOST_TEST)
target_link_libraries(furble_host_camera PUBLIC Threads::Threads)

# Compile the production MQTT task against deterministic host dependency
# shims. The test drives one owner-task step at a time, so callback copies and
# timer notifications are observable without creating an ESP task.
add_executable(mqtt_owner_test
               mqtt_test.cpp
               ${HOST_MQTT_DIR}/cJSON.cpp
               ${HOST_MQTT_DIR}/mqtt_client_mock.cpp
               ${HOST_MQTT_DIR}/mqtt_host_doubles.cpp
               boot_timeline_host_stub.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp
               ${FURBLE_ROOT}/src/FurbleMQTT.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(mqtt_owner_test BEFORE PRIVATE
                           ${HOST_MQTT_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(mqtt_owner_test PRIVATE FURBLE_MQTT=1 FURBLE_MQTT_HOST_TEST=1
                                                   FURBLE_BOOT_TIMELINE_HOST_TEST=1
                                                   FURBLE_VERSION="host-test")
target_compile_options(mqtt_owner_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                               -Wno-unused-parameter)
target_link_libraries(mqtt_owner_test PRIVATE Threads::Threads)

# Seeded MQTT topic/payload fuzzer over the same production owner task and host
# broker double. It delivers malformed, oversized, fragmented and retained
# traffic and asserts only an exact subscribed command can reach Control.
set(HOST_FUZZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
add_executable(mqtt_fuzz
               ${HOST_FUZZ_DIR}/mqtt_fuzz.cpp
               ${HOST_MQTT_DIR}/cJSON.cpp
               ${HOST_MQTT_DIR}/mqtt_client_mock.cpp
               ${HOST_MQTT_DIR}/mqtt_host_doubles.cpp
               boot_timeline_host_stub.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp
               ${FURBLE_ROOT}/src/FurbleMQTT.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(mqtt_fuzz BEFORE PRIVATE
                           ${HOST_MQTT_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(mqtt_fuzz PRIVATE FURBLE_MQTT=1 FURBLE_MQTT_HOST_TEST=1
                                          FURBLE_BOOT_TIMELINE_HOST_TEST=1
                                          FURBLE_VERSION="host-test")
target_compile_options(mqtt_fuzz PRIVATE -Wall -Wextra -Werror -Wno-format
                                         -Wno-unused-parameter)
target_link_libraries(mqtt_fuzz PRIVATE Threads::Threads)

add_executable(fujifilm_camera_test fujifilm_camera_test.cpp)
target_link_libraries(fujifilm_camera_test PRIVATE furble_host_camera)
target_compile_options(fujifilm_camera_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                     -Wno-unused-parameter)

add_executable(camera_pairing_test camera_pairing_test.cpp)
target_link_libraries(camera_pairing_test PRIVATE furble_host_camera)
target_compile_options(camera_pairing_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                    -Wno-unused-parameter)

# Exercise the production security callbacks and the NimBLE injection boundary
# against the Ricoh peer's blocking numeric-comparison handshake.
add_executable(camera_pairing_peer_test
               camera_pairing_peer_test.cpp
               ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(camera_pairing_peer_test PRIVATE furble_host_camera Threads::Threads)
target_include_directories(camera_pairing_peer_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(camera_pairing_peer_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)

add_executable(camera_pairing_race_test
               camera_pairing_race_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)
target_include_directories(camera_pairing_race_test PRIVATE
                           ${FURBLE_ROOT}/lib/furble
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR})
target_compile_definitions(camera_pairing_race_test PRIVATE FURBLE_HOST_TEST
                                                            FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(camera_pairing_race_test PRIVATE
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(camera_pairing_race_test PRIVATE -fsanitize=address)
target_link_libraries(camera_pairing_race_test PRIVATE Threads::Threads)

add_executable(corpus_replay_test
               corpus_replay_test.cpp
               ${HOST_CORPUS_DIR}/GoldenCapture.cpp)
target_link_libraries(corpus_replay_test PRIVATE furble_host_camera)
target_compile_options(corpus_replay_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                    -Wno-unused-parameter)

add_executable(lifecycle_test lifecycle_test.cpp)
target_link_libraries(lifecycle_test PRIVATE furble_host_camera)
target_compile_options(lifecycle_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                              -Wno-unused-parameter)

add_executable(wrap_safe_time_test wrap_safe_time_test.cpp)
target_compile_options(wrap_safe_time_test PRIVATE -Wall -Wextra -Werror)

add_executable(time_keeper_policy_test
               time_keeper_policy_test.cpp
               ${FURBLE_ROOT}/src/FurbleTimeKeeperPolicy.cpp)
target_include_directories(time_keeper_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(time_keeper_policy_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

# Local civil time from the curated POSIX TZ table. The cases pin both sides of
# the 2026 DST transitions for the supported zones.
add_executable(timezone_test timezone_test.cpp ${FURBLE_ROOT}/src/FurbleTimezone.cpp)
target_include_directories(timezone_test PRIVATE ${FURBLE_ROOT}/include
                                                   ${FURBLE_ROOT}/lib/furble)
target_compile_options(timezone_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

# Exercise the production TimeKeeper and Preferences wrapper against the
# durable in-memory NVS implementation. The clock and RTC are controllable so
# reboot, shutdown, and correction-storm wear budgets are deterministic.
set(TIME_KEEPER_TEST_STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/time_keeper_stubs)
add_executable(time_keeper_persistence_test
               time_keeper_persistence_test.cpp
               settings_nvs_stub.cpp
               ${FURBLE_ROOT}/src/FurbleTimeKeeper.cpp
               ${FURBLE_ROOT}/src/FurbleTimeKeeperPolicy.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(time_keeper_persistence_test BEFORE PRIVATE
                           ${TIME_KEEPER_TEST_STUB_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/settings_stubs
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(time_keeper_persistence_test PRIVATE FURBLE_SIM)
target_compile_options(time_keeper_persistence_test PRIVATE -Wall -Wextra -Werror
                                                            -Wno-unused-parameter
                                                            -Wno-vla-extension)

# Exercise the simulator's real file-backed Preferences adapter. Each CTest
# case is a fresh process so load status and the process-local store are reset.
add_executable(real_wifi_lifecycle_test
               wifi/real_wifi_test.cpp
               eth/MockEthNetif.cpp
               boot_timeline_host_stub.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp
               ${FURBLE_ROOT}/src/FurbleWiFi.cpp
               ${FURBLE_ROOT}/src/FurbleEthernet.cpp)
target_include_directories(real_wifi_lifecycle_test BEFORE PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/wifi/stubs
                           ${FURBLE_ROOT}/include)
target_link_libraries(real_wifi_lifecycle_test PRIVATE Threads::Threads)
target_compile_definitions(real_wifi_lifecycle_test PRIVATE FURBLE_WIFI_HOST_TEST
                                                          FURBLE_BOOT_TIMELINE_HOST_TEST=1)
target_compile_options(real_wifi_lifecycle_test PRIVATE -Wall -Wextra -Werror
                                                         -Wno-unused-parameter)

add_executable(actualPreferencesSim
               preferences_sim_test.cpp
               ${FURBLE_ROOT}/sim/PreferencesSim.cpp)
target_include_directories(actualPreferencesSim PRIVATE
                           ${FURBLE_ROOT}/lib/preferences)
target_compile_options(actualPreferencesSim PRIVATE -Wall -Wextra -Werror
                                                     -Wno-unused-parameter)

# The PMIC button is polled from the 5 ms UI loop. This policy test proves the
# 20 ms throttle and the wrap-safe tick arithmetic.
add_executable(pm1_poll_policy_test pm1_poll_policy_test.cpp)
target_include_directories(pm1_poll_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(pm1_poll_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME pm1-poll-policy COMMAND pm1_poll_policy_test)

# The staged display mode dims at the inactivity timeout and sleeps after a
# fixed dwell. This policy test pins the dwell and its wrap arithmetic.
add_executable(display_policy_test display_policy_test.cpp)
target_include_directories(display_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(display_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME display-policy COMMAND display_policy_test)

# The headless build has no display task; its loop idles at 50 ms and only
# shortens the sleep to keep the 1 Hz GPS service on time.
add_executable(headless_loop_policy_test headless_loop_policy_test.cpp)
target_include_directories(headless_loop_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(headless_loop_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME headless-loop-policy COMMAND headless_loop_policy_test)

# esp_pm_configure() rejects light_sleep_enable when tickless idle is off, so the
# runtime request must follow the compiled support. This test pins the exact
# request production builds from.
add_executable(power_request_policy_test power_request_policy_test.cpp)
target_include_directories(power_request_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(power_request_policy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME power-request-policy COMMAND power_request_policy_test)

# The PMIC is a separate chip from the ESP32. Its watchdog and download lock
# therefore survive an ESP32 reset and must be modeled as retained state.
add_executable(pmic_recovery_test
               pmic_recovery_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp)
target_include_directories(pmic_recovery_test PRIVATE
                           ${FURBLE_ROOT}/sim/shim
                           ${FURBLE_ROOT}/sim
                           ${FURBLE_ROOT}/include)
target_compile_options(pmic_recovery_test PRIVATE -Wall -Wextra -Werror
                                                       -Wno-unused-parameter)

add_executable(gesture_detector_test
               gesture_detector_test.cpp
               ${FURBLE_ROOT}/src/FurbleUIGesture.cpp)
target_include_directories(gesture_detector_test BEFORE PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/gesture_stubs
                           ${FURBLE_ROOT}/include)
target_compile_options(gesture_detector_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME gesture-detector COMMAND gesture_detector_test)

add_executable(headless_ui_contract_test headless_ui_contract_test.cpp)
target_include_directories(headless_ui_contract_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_definitions(headless_ui_contract_test PRIVATE FURBLE_NO_DISPLAY)
target_compile_options(headless_ui_contract_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME headless-ui-contract COMMAND headless_ui_contract_test)

add_executable(webui_security_test webui_security_test.cpp)
target_include_directories(webui_security_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(webui_security_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME webui-security COMMAND webui_security_test)

add_executable(ui_memory_test ui_memory_test.cpp)
target_include_directories(ui_memory_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(ui_memory_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui-memory COMMAND ui_memory_test)

add_executable(sim_webui_memory_model_test
               sim_webui_memory_model_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/heap_model.cpp
               ${FURBLE_ROOT}/sim/webui_memory_model.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(sim_webui_memory_model_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/sim
                           ${FURBLE_ROOT}/sim/shim
                           ${HOST_NIMBLE_DIR})
target_compile_definitions(sim_webui_memory_model_test PRIVATE FURBLE_SIM)
target_compile_options(sim_webui_memory_model_test PRIVATE -Wall -Wextra -Werror
                                                            -Wno-unused-parameter)
add_test(NAME sim-webui-memory-model COMMAND sim_webui_memory_model_test)

add_executable(device_identity_test
               device_identity_test.cpp
               advertisement_preferences_stub.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionIdentity.cpp)
target_include_directories(device_identity_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/preferences
                           ${HOST_NIMBLE_DIR})
target_compile_definitions(device_identity_test PRIVATE FURBLE_HOST_TEST)
target_compile_options(device_identity_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME device-identity COMMAND device_identity_test)

add_executable(companion_memory_test companion_memory_test.cpp)
target_include_directories(companion_memory_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(companion_memory_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME companion-memory COMMAND companion_memory_test)

add_executable(memory_pressure_test memory_pressure_test.cpp)
target_include_directories(memory_pressure_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(memory_pressure_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME memory-pressure COMMAND memory_pressure_test)

add_executable(crash_record_test crash_record_test.cpp)
target_include_directories(crash_record_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(crash_record_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME crash-record COMMAND crash_record_test)

add_executable(crash_log_test crash_log_test.cpp ${FURBLE_ROOT}/src/FurbleCrashLog.cpp)
target_include_directories(crash_log_test BEFORE PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/crash_log
                           ${FURBLE_ROOT}/include)
target_compile_definitions(crash_log_test PRIVATE FURBLE_CRASH_LOG_HOST_TEST=1)
target_compile_options(crash_log_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME crash-log COMMAND crash_log_test)

add_executable(webui_handler_test
               webui_handler_test.cpp
               webui/webui_platform_stubs.cpp
               mqtt/cJSON.cpp
               companion/companion_hmac.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp
               ${FURBLE_ROOT}/src/FurbleWebUI.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(webui_handler_test
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/webui
                                   ${CMAKE_CURRENT_SOURCE_DIR}/mqtt
                                   ${CMAKE_CURRENT_SOURCE_DIR}/companion
                                   ${FURBLE_ROOT}/include)
target_compile_definitions(webui_handler_test
                           PRIVATE FURBLE_MQTT=1 FURBLE_WEBUI=1 FURBLE_WEBUI_HOST_TEST=1)
target_compile_options(webui_handler_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(webui_handler_test PRIVATE Threads::Threads)
add_test(NAME webui-handler COMMAND webui_handler_test)

# Seeded API fuzzer for the same production WebUI handlers. The fixed corpus
# covers malformed JSON, auth and origin failures, method mismatches and
# oversized bodies; seeded mutations widen the search while keeping a finding
# reproducible from its seed. The smoke budget runs in the normal host suite,
# and tools/check_fuzz.py runs the wider seed set.
add_executable(webui_fuzz
               ${HOST_FUZZ_DIR}/webui_fuzz.cpp
               webui/webui_platform_stubs.cpp
               mqtt/cJSON.cpp
               companion/companion_hmac.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp
               ${FURBLE_ROOT}/src/FurbleWebUI.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(webui_fuzz
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/webui
                                   ${CMAKE_CURRENT_SOURCE_DIR}/mqtt
                                   ${CMAKE_CURRENT_SOURCE_DIR}/companion
                                   ${FURBLE_ROOT}/include)
target_compile_definitions(webui_fuzz
                           PRIVATE FURBLE_MQTT=1 FURBLE_WEBUI=1 FURBLE_WEBUI_HOST_TEST=1)
target_compile_options(webui_fuzz PRIVATE -Wall -Wextra -Werror)
target_link_libraries(webui_fuzz PRIVATE Threads::Threads)
add_test(NAME webui-fuzz COMMAND webui_fuzz --seed 1 --iterations 200)
set_tests_properties(webui-fuzz PROPERTIES TIMEOUT 120)

set(FURBLE_PROTOCOL_DIR ${FURBLE_ROOT}/lib/furble/protocol)

# Compile and call the real CameraList dispatch path with the host NimBLE
# advertisement double. This guards null input, Lumix dispatch, and address
# de-duplication without reconstructing the production matcher in a test.
add_executable(advertisement_dispatch_test
               advertisement_dispatch_test.cpp
               advertisement_preferences_stub.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/CameraList.cpp
               ${FURBLE_ROOT}/lib/furble/CanonEOS.cpp
               ${FURBLE_ROOT}/lib/furble/CanonEOSRemote.cpp
               ${FURBLE_ROOT}/lib/furble/CanonEOSSmart.cpp
               ${FURBLE_ROOT}/lib/furble/DJIOsmo.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/FauxNY.cpp
               ${FURBLE_ROOT}/lib/furble/Lumix.cpp
               ${FURBLE_ROOT}/lib/furble/Nikon.cpp
               ${FURBLE_ROOT}/lib/furble/NikonBase.cpp
               ${FURBLE_ROOT}/lib/furble/NikonRemote.cpp
               ${FURBLE_ROOT}/lib/furble/NikonSmart.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_ROOT}/lib/furble/Sony.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/CameraListProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               advertisement_scan_stub.cpp)
target_include_directories(advertisement_dispatch_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${FURBLE_ROOT}/sim
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(advertisement_dispatch_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension
                       -Wno-vla-extension-static-assert -Wno-c++11-narrowing
                       -Wno-sign-compare -Wno-format -Wno-error
                       -Wno-unused-parameter)
target_link_libraries(advertisement_dispatch_test PRIVATE Threads::Threads)

add_executable(camera_regression_test
               camera_regression_test.cpp
               ${FURBLE_ROOT}/lib/furble/Nikon.cpp
               ${FURBLE_ROOT}/lib/furble/NikonBase.cpp
               ${FURBLE_ROOT}/lib/furble/NikonRemote.cpp
               ${FURBLE_ROOT}/lib/furble/NikonSmart.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               advertisement_scan_stub.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(camera_regression_test PRIVATE furble_host_camera)
target_include_directories(camera_regression_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${FURBLE_ROOT}/sim
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(camera_regression_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)

# Prove the displayed camera name derivation over the production Fujifilm
# constructors and the virtual peer's advertisement, in both the scanned and
# the saved directions.
add_executable(fujifilm_device_name_test
               fujifilm_device_name_test.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               advertisement_scan_stub.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(fujifilm_device_name_test PRIVATE furble_host_camera)
target_include_directories(fujifilm_device_name_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${FURBLE_ROOT}/sim
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(fujifilm_device_name_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)

# Drive the production Ricoh focus and shutter paths through the in-memory peer.
# The semantic assertions protect the distinction between unsupported focus
# gestures and the supported capture-with-autofocus operation.
add_executable(ricoh_focus_test
               ricoh_focus_test.cpp
               ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(ricoh_focus_test PRIVATE furble_host_camera)
target_include_directories(ricoh_focus_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(ricoh_focus_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)

# Drive the production LUMIX session and shutter paths. The test proves the
# Confirm 01, Start 02, and Finish 03 handshake and that focus never maps to a
# session command.
add_executable(lumix_session_test
               lumix_session_test.cpp
               ${HOST_PEER_DIR}/LumixVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Lumix.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(lumix_session_test PRIVATE furble_host_camera)
target_include_directories(lumix_session_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(lumix_session_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)
add_test(NAME lumix-session COMMAND lumix_session_test)

# Canon Smart sends an absolute UTC epoch. This pure helper test proves the
# conversion does not depend on the runtime timezone.
add_executable(canon_time_test canon_time_test.cpp)
target_include_directories(canon_time_test PRIVATE
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(canon_time_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME canon-time COMMAND canon_time_test)

# Ricoh Date Time Zone is a fixed 7-byte packet with a little-endian year.
add_executable(ricoh_time_test ricoh_time_test.cpp)
target_include_directories(ricoh_time_test PRIVATE
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(ricoh_time_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ricoh-time COMMAND ricoh_time_test)

# Drive the production Sony location path through the Sony peer and prove the
# DD21-selected 91/95-byte form.
add_executable(sony_location_test
               sony_location_test.cpp
               ${HOST_PEER_DIR}/SonyVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Sony.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(sony_location_test PRIVATE furble_host_camera)
target_include_directories(sony_location_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(sony_location_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)
add_test(NAME sony-location COMMAND sony_location_test)

# The sleep gate test drives the production Ricoh shutter path against the
# virtual peer in BLE standby. It protects the invariant that only a fresh
# OperationMode read of CAPTURE authorizes the ShootingFlavor and
# OperationRequest writes; a capture write in BLE_STARTUP cold boots the
# camera and can wedge its firmware.
add_executable(ricoh_sleep_gate_test
               ricoh_sleep_gate_test.cpp
               ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(ricoh_sleep_gate_test PRIVATE furble_host_camera)
target_include_directories(ricoh_sleep_gate_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(ricoh_sleep_gate_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)

# The registration gate test compiles the real Secure path against the same
# virtual peer as the normal camera suite. The host-only timeout keeps a
# withheld-registration case fast while firmware retains the 25 s bound.
add_executable(fujifilm_registration_gate_test
               fujifilm_registration_gate_test.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               advertisement_scan_stub.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(fujifilm_registration_gate_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_definitions(fujifilm_registration_gate_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(fujifilm_registration_gate_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(fujifilm_registration_gate_test PRIVATE furble_host_camera
                                                             Threads::Threads)

# Stale-bond recovery for Fujifilm Secure (plan 151). Uses the shared library's
# 100 ms registration timeout: the recovery runs before the registration wait,
# so the short timeout only keeps the successful fresh-pair phase fast.
add_executable(fujifilm_stale_bond_test
               fujifilm_stale_bond_test.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               advertisement_scan_stub.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(fujifilm_stale_bond_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_definitions(fujifilm_stale_bond_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(fujifilm_stale_bond_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(fujifilm_stale_bond_test PRIVATE furble_host_camera
                                                       Threads::Threads)

# Camera-level cancel during the registration wait (plans 148/151). Compiles
# its own camera sources with a 10 s registration timeout, an order of
# magnitude above the asserted abort bound, so a wait that ignores the
# Camera::cancelConnect token fails the bounded-time check deterministically
# instead of hiding inside the 100 ms timeout the shared library uses.
add_executable(fujifilm_registration_cancel_test
               fujifilm_registration_cancel_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               advertisement_scan_stub.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(fujifilm_registration_cancel_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_definitions(fujifilm_registration_cancel_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=10000)
target_compile_options(fujifilm_registration_cancel_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(fujifilm_registration_cancel_test PRIVATE Threads::Threads)

# Keep the host Ethernet test tied to the firmware source manifest. The test
# links the production implementation directly, so this configure-time guard
# catches the failure mode that matters here: a firmware build which forgets
# to compile the implementation at all.
file(STRINGS ${FURBLE_ROOT}/src/CMakeLists.txt FURBLE_SOURCE_LINES
     REGEX "^[ \\t]*FurbleEthernet\\.cpp[ \\t]*$")
if(NOT FURBLE_SOURCE_LINES)
  message(FATAL_ERROR
          "src/CMakeLists.txt must include FurbleEthernet.cpp in its production source list")
endif()

# Pure logic unit suites. These link their production source directly instead
# of the mock BLE library because the modules under test have no BLE or NVS
# dependency. Each suite is standalone so a build or link break is isolated to
# one module.

# The companion password gate is transport-independent. Keep the production
# state machine in this host target and inject the test HMAC and nonce seams.
add_executable(companion_auth_test
               companion_auth_test.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp)
target_include_directories(companion_auth_test PRIVATE ${FURBLE_ROOT}/include
                                                       ${CMAKE_CURRENT_SOURCE_DIR}/companion)
target_compile_options(companion_auth_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                    -Wno-unused-parameter)

add_executable(camera_list_protocol_test
               camera_list_protocol_test.cpp
               ${FURBLE_PROTOCOL_DIR}/CameraListProtocol.cpp)
target_include_directories(camera_list_protocol_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_options(camera_list_protocol_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                         -Wno-unused-parameter)

add_executable(auto_off_policy_test auto_off_policy_test.cpp)
target_include_directories(auto_off_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(auto_off_policy_test PRIVATE -Wall -Wextra -Werror)

add_executable(fujifilm_protocol_test
               fujifilm_protocol_test.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(fujifilm_protocol_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_options(fujifilm_protocol_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                      -Wno-unused-parameter)

# The settings table suite parses src/FurbleSettings.cpp as text, so it needs
# no furble sources on its link line.
add_executable(settings_table_test settings_table_test.cpp)
target_compile_options(settings_table_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                   -Wno-unused-parameter)

# Compile the production Scan implementation against the host NimBLE double.
# This catches callback-thread ownership, bounded handoff, generation fencing,
# expiry, and custom callback lifetime regressions without a radio or UI.
add_executable(scan_generation_test
               scan_generation_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp)
target_include_directories(scan_generation_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(scan_generation_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                        -Wno-unused-parameter)
target_link_libraries(scan_generation_test PRIVATE Threads::Threads)

# Typed BLE diagnostics are checked through the event API, including bounded
# ring overwrite and bounded live draining. This prevents tests from merely
# asserting formatted console strings.
add_executable(bt_debug_journal_test
               bt_debug_journal_test.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp)
target_include_directories(bt_debug_journal_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_options(bt_debug_journal_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(bt_debug_journal_test PRIVATE Threads::Threads)

# Compile the same assertions against the S3+PSRAM and S3-without-PSRAM
# preprocessor profiles. Runtime allocation failure remains covered by the
# capability-checked firmware path; these targets lock down both capacities.
add_executable(bt_debug_journal_s3_test
               bt_debug_journal_test.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp)
target_include_directories(bt_debug_journal_s3_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(bt_debug_journal_s3_test PRIVATE FURBLE_HOST_S3_PSRAM
                                                            CONFIG_IDF_TARGET_ESP32S3
                                                            CONFIG_SPIRAM)
target_compile_options(bt_debug_journal_s3_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(bt_debug_journal_s3_test PRIVATE Threads::Threads)

add_executable(bt_debug_journal_s3_fallback_test
               bt_debug_journal_test.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp)
target_include_directories(bt_debug_journal_s3_fallback_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(bt_debug_journal_s3_fallback_test PRIVATE FURBLE_HOST_S3_FALLBACK
                                                                     CONFIG_IDF_TARGET_ESP32S3)
target_compile_options(bt_debug_journal_s3_fallback_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(bt_debug_journal_s3_fallback_test PRIVATE Threads::Threads)

# Drive production Camera GATT wrappers with the in-memory Fujifilm peer while
# the console journal is enabled. Assertions inspect typed events, not output.
add_executable(camera_journal_event_test
               camera_journal_event_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp)
target_include_directories(camera_journal_event_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(camera_journal_event_test PRIVATE FURBLE_CONSOLE)
target_compile_options(camera_journal_event_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                           -Wno-unused-parameter)
target_link_libraries(camera_journal_event_test PRIVATE Threads::Threads)

# Run saved Fujifilm and Nikon callback objects through the production Scan
# cancellation barrier. This catches a late callback touching a destroyed
# camera and proves finite reconnect scans can be cancelled promptly.
add_executable(saved_scan_cancel_test
               saved_scan_cancel_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Nikon.cpp
               ${FURBLE_ROOT}/lib/furble/NikonBase.cpp
               ${FURBLE_ROOT}/lib/furble/NikonRemote.cpp
               ${FURBLE_ROOT}/lib/furble/NikonSmart.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp)
target_include_directories(saved_scan_cancel_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_options(saved_scan_cancel_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(saved_scan_cancel_test PRIVATE Threads::Threads)

# Saved Fujifilm Secure reconnect scan match. The camera advertises either the
# pairing service or only the Secure service in reconnect standby, so the
# matcher must accept both. This target compiles the production Scan and
# Secure paths against the host NimBLE double and the virtual peer, and it
# asserts the reconnect completes promptly once the advertisement arrives.
add_executable(saved_scan_delivery_test
               saved_scan_delivery_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(saved_scan_delivery_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_options(saved_scan_delivery_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(saved_scan_delivery_test PRIVATE Threads::Threads)

# Failed Secure registration must leave an asynchronous GAP disconnect event
# addressable until NimBLE has dispatched it. This target combines the real
# Secure registration and production Scan path with the host NimBLE double.
add_executable(ble_registration_cleanup_test
               ble_registration_cleanup_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(ble_registration_cleanup_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/blowfish
                           ${FURBLE_ROOT}/include)
target_compile_options(ble_registration_cleanup_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare)
target_link_libraries(ble_registration_cleanup_test PRIVATE Threads::Threads)

# Exercise the real NVS/Preferences settings path against a typed in-memory
# NVS stub. The test also includes the production SD settings helpers so every
# table row is checked through both the firmware store and the SD import/export
# representation. M5STICKS3 keeps the conditional watchdog row in the host
# table, so a new or conditional setting cannot be omitted silently.
set(SETTINGS_TEST_STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/settings_stubs)
add_executable(settings_nvs_roundtrip_test
               settings_nvs_roundtrip_test.cpp
               settings_nvs_stub.cpp
               settings_platform_stub.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(settings_nvs_roundtrip_test BEFORE PRIVATE
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(settings_nvs_roundtrip_test PRIVATE FURBLE_M5STICKS3)
target_compile_options(settings_nvs_roundtrip_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                             -Wno-unused-parameter
                                                             -Wno-vla-extension)

# The boot seeder is one transaction per namespace plus a version marker. This
# suite pins the commit count, proves a current marker skips the scan, and shows
# an upgrade or a failed commit never overwrites a stored value.
add_executable(settings_boot_test
               settings_boot_test.cpp
               settings_nvs_stub.cpp
               settings_platform_stub.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(settings_boot_test BEFORE PRIVATE
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(settings_boot_test PRIVATE FURBLE_M5STICKS3)
target_compile_options(settings_boot_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter -Wno-vla-extension)

# Seeded migration permutations for the first-boot default seeder. The marker
# is set to missing, old, future, string and blob shapes, random default keys
# are removed, and a commit fails at a random ordinal. A clean boot must repair
# whatever the failed seed left behind.
set(HOST_FUZZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
add_executable(settings_migration_fuzz
               ${HOST_FUZZ_DIR}/settings_migration_fuzz.cpp
               settings_nvs_stub.cpp
               settings_platform_stub.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(settings_migration_fuzz BEFORE PRIVATE
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(settings_migration_fuzz PRIVATE FURBLE_M5STICKS3)
target_compile_options(settings_migration_fuzz PRIVATE -Wall -Wextra -Werror -Wno-format
                                                       -Wno-unused-parameter -Wno-vla-extension)
add_test(NAME settings-migration-fuzz COMMAND settings_migration_fuzz)

add_executable(boot_timeline_test
               boot_timeline_test.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp)
target_include_directories(boot_timeline_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_definitions(boot_timeline_test PRIVATE FURBLE_BOOT_TIMELINE_HOST_TEST)
target_compile_options(boot_timeline_test PRIVATE -Wall -Wextra -Werror)

add_executable(boot_policy_test boot_policy_test.cpp)
target_include_directories(boot_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(boot_policy_test PRIVATE -Wall -Wextra -Werror)

# Exhaustive channel/auth permutations and seeded fast-connect lifecycle
# sequences for the WiFi fallback policy.
add_executable(wifi_policy_fuzz
               ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/wifi_policy_fuzz.cpp)
target_include_directories(wifi_policy_fuzz PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(wifi_policy_fuzz PRIVATE -Wall -Wextra -Werror)
add_test(NAME wifi-policy-fuzz COMMAND wifi_policy_fuzz)

add_executable(provision_tlv_test
               provision_tlv_test.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/ProvisionTLV.cpp)
target_include_directories(provision_tlv_test PRIVATE ${FURBLE_ROOT}/lib/furble)
target_compile_options(provision_tlv_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)

add_executable(provision_apply_test
               provision_apply_test.cpp
               settings_nvs_stub.cpp
               ${FURBLE_ROOT}/src/FurbleProvision.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/ProvisionTLV.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(provision_apply_test BEFORE PRIVATE
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(provision_apply_test PRIVATE FURBLE_M5STICKS3)
target_compile_options(provision_apply_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                    -Wno-unused-parameter
                                                    -Wno-vla-extension)

# Exercise the positive MQTT provisioning branch as well as the default build.
# FurbleProvision only needs the settings/network validator here; keep this
# target independent from the MQTT client implementation.
add_executable(provision_apply_mqtt_test
               provision_apply_test.cpp
               settings_nvs_stub.cpp
               ${FURBLE_ROOT}/src/FurbleProvision.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/ProvisionTLV.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp)
target_include_directories(provision_apply_mqtt_test BEFORE PRIVATE
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(provision_apply_mqtt_test PRIVATE FURBLE_M5STICKS3 FURBLE_MQTT=1)
target_compile_options(provision_apply_mqtt_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                           -Wno-unused-parameter
                                                           -Wno-vla-extension)

# Board-conditional text size policy from include/FurbleTextSize.h. The header is
# dependency free, so each variant compiles standalone. The suite is built twice:
# the plain target exercises the large-board policy, the -stickc target defines
# FURBLE_M5STICKC to exercise the 80x160 small-board policy. The checks are
# static_asserts, so a wrong policy fails to compile.
add_executable(text_size_policy_test text_size_policy_test.cpp)
target_include_directories(text_size_policy_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(text_size_policy_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                     -Wno-unused-parameter)
add_executable(text_size_policy_stickc_test text_size_policy_test.cpp)
target_include_directories(text_size_policy_stickc_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_definitions(text_size_policy_stickc_test PRIVATE FURBLE_M5STICKC)
target_compile_options(text_size_policy_stickc_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                            -Wno-unused-parameter)

# The BMI270 and MPU6886 motion engines are register programming against an I2C
# bus. The simulator has no bus and models only what the engines mean, so the
# wire side is pinned here instead: the two engine translation units are built
# against a recording bus and a stub platform, and the test asserts the register
# sequence and the feature word encoding against the datasheet and Bosch API
# values cited in its header. The stub include directory is listed first so it
# shadows the real FurblePlatform.h.
add_executable(imu_motion_encoding_test
               imu_motion_encoding_test.cpp
               ${FURBLE_ROOT}/src/FurbleIMUMotionBMI270.cpp
               ${FURBLE_ROOT}/src/FurbleIMUMotionMPU6886.cpp)
target_include_directories(imu_motion_encoding_test PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/imu_stubs
                           ${FURBLE_ROOT}/include)
target_compile_options(imu_motion_encoding_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                        -Wno-unused-parameter)

add_executable(stale_connect_test stale_connect_test.cpp)
target_link_libraries(stale_connect_test PRIVATE furble_host_camera)
target_compile_options(stale_connect_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)
add_executable(client_leak_test client_leak_test.cpp)
target_link_libraries(client_leak_test PRIVATE furble_host_camera)
target_compile_options(client_leak_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                -Wno-unused-parameter)
add_executable(reconnect_stuck_test reconnect_stuck_test.cpp)
target_link_libraries(reconnect_stuck_test PRIVATE furble_host_camera)
target_compile_options(reconnect_stuck_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                    -Wno-unused-parameter)

# Fault-injection tooling demonstration. Green: proves the expanded MockNimBLE
# and FujifilmVirtualCamera fault hooks work and guards the reclaim and abort
# paths they reach.
add_executable(fault_tooling_test fault_tooling_test.cpp)
target_link_libraries(fault_tooling_test PRIVATE furble_host_camera)
target_compile_options(fault_tooling_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)

# Adversarial bug-hunt findings, now real regression guards. FujifilmBasic::_connect
# returns false when the shutter service or characteristic is missing, so both
# tests pass as normal tests below: no null deref crash, no silent success.
add_executable(fujifilm_missing_shutter_service_test fujifilm_missing_shutter_service_test.cpp)
target_link_libraries(fujifilm_missing_shutter_service_test PRIVATE furble_host_camera)
target_compile_options(fujifilm_missing_shutter_service_test PRIVATE -Wall -Wextra -Werror
                                                                     -Wno-format
                                                                     -Wno-unused-parameter)
add_executable(fujifilm_missing_shutter_char_test fujifilm_missing_shutter_char_test.cpp)
target_link_libraries(fujifilm_missing_shutter_char_test PRIVATE furble_host_camera)
target_compile_options(fujifilm_missing_shutter_char_test PRIVATE -Wall -Wextra -Werror
                                                                  -Wno-format
                                                                  -Wno-unused-parameter)

# The GPX writer suite compiles the production writer directly. It has no BLE or
# NVS dependency, only C file I/O, so it redirects the mount point to a build
# sandbox with FURBLE_GPX_DIRECTORY and needs the host esp_log shim.
add_executable(gpx_writer_test
               gpx_writer_test.cpp
               ${FURBLE_ROOT}/src/FurbleGPX.cpp)
target_include_directories(gpx_writer_test PRIVATE
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble
                           ${HOST_NIMBLE_DIR})
target_compile_definitions(gpx_writer_test PRIVATE
                           FURBLE_GPX_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/gpx_sandbox")
target_compile_options(gpx_writer_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                               -Wno-unused-parameter)

# Pure backoff arithmetic from include/FurbleReconnectBackoff.h. No BLE or NVS
# dependency, so it needs no furble sources on its link line.
add_executable(reconnect_backoff_test reconnect_backoff_test.cpp)
target_include_directories(reconnect_backoff_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(reconnect_backoff_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                      -Wno-unused-parameter)

# Pure Battery Saver override arithmetic from include/FurbleBatterySaver.h. No
# BLE or NVS dependency, so it needs no furble sources on its link line.
add_executable(battery_saver_test battery_saver_test.cpp)
target_include_directories(battery_saver_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(battery_saver_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)

add_executable(sim_clock_test sim_clock_test.cpp ${FURBLE_ROOT}/sim/clock.cpp)
target_include_directories(sim_clock_test PRIVATE ${FURBLE_ROOT}/include ${FURBLE_ROOT}/sim
                                                  ${FURBLE_ROOT}/sim/shim)
target_compile_options(sim_clock_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                               -Wno-unused-parameter)

add_executable(sim_action_parser_test sim_action_parser_test.cpp
               ${FURBLE_ROOT}/sim/scenario_action.cpp)
target_include_directories(sim_action_parser_test PRIVATE ${FURBLE_ROOT}/sim)
target_compile_options(sim_action_parser_test PRIVATE -Wall -Wextra -Werror)

add_executable(sim_scheduler_test
               sim_scheduler_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/freertos.cpp
               # freertos.cpp charges task stacks to the shared heap model.
               ${FURBLE_ROOT}/sim/heap_model.cpp
               ${FURBLE_ROOT}/sim/CrashLogSim.cpp
               ${FURBLE_ROOT}/sim/watchdog.cpp)
target_include_directories(sim_scheduler_test PRIVATE ${FURBLE_ROOT}/include
                                                       ${FURBLE_ROOT}/sim
                                                       ${FURBLE_ROOT}/sim/shim)
target_compile_options(sim_scheduler_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)
target_link_libraries(sim_scheduler_test PRIVATE Threads::Threads)

add_executable(sim_watchdog_test
               sim_watchdog_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/watchdog.cpp)
target_include_directories(sim_watchdog_test PRIVATE ${FURBLE_ROOT}/include
                                                       ${FURBLE_ROOT}/sim
                                                       ${FURBLE_ROOT}/sim/shim)
target_compile_options(sim_watchdog_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)
target_link_libraries(sim_watchdog_test PRIVATE Threads::Threads)

add_executable(sim_fuzz_machine_test
               sim_fuzz_machine_test.cpp
               ${FURBLE_ROOT}/sim/fuzz_machine.cpp)
target_include_directories(sim_fuzz_machine_test PRIVATE ${FURBLE_ROOT}/sim)
target_compile_options(sim_fuzz_machine_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                       -Wno-unused-parameter)

add_executable(sim_power_profiler_test
               sim_power_profiler_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/power_profiler.cpp)
target_include_directories(sim_power_profiler_test PRIVATE ${FURBLE_ROOT}/include
                                                           ${FURBLE_ROOT}/sim)
target_compile_options(sim_power_profiler_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                         -Wno-unused-parameter)

# Dead-pid reclamation for generated simulator preference stores (issue #289).
add_executable(preferences_sweep_test
               preferences_sweep_test.cpp
               ${FURBLE_ROOT}/sim/preferences_sweep.cpp)
target_include_directories(preferences_sweep_test PRIVATE ${FURBLE_ROOT}/sim)
target_compile_options(preferences_sweep_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                       -Wno-unused-parameter)

# Link the host regression against the production Ethernet implementation.
# The source-manifest guard above ensures this test cannot silently drift away
# from the firmware source list which owns the link.
add_executable(ethernet_test
               ethernet_test.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/eth/MockEthNetif.cpp
               ${FURBLE_ROOT}/src/FurbleEthernet.cpp)
target_include_directories(ethernet_test PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${FURBLE_ROOT}/include)
target_compile_options(ethernet_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                             -Wno-unused-parameter)
target_link_libraries(ethernet_test PRIVATE Threads::Threads)

# Test-only power/source model for the Waveshare board. The real board has no
# software-readable PoE HAT or negotiated-power signal, so this fixture keeps
# HAT, PoE, link, and USB observations explicit and independent.
add_executable(poe_power_model_test
               poe_power_model_test.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/eth/PoEPowerModel.cpp)
target_include_directories(poe_power_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(poe_power_model_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                     -Wno-unused-parameter)
# Transport-independent OTA lifecycle. The production state machine is linked
# directly, while the host test supplies a mock for the ESP-IDF transport seam.
add_executable(ota_state_machine_test
               ota_state_machine_test.cpp
               ${FURBLE_ROOT}/src/FurbleOTA.cpp)
target_include_directories(ota_state_machine_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(ota_state_machine_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                        -Wno-unused-parameter)

# Transport-neutral MQTT OTA envelope and delivery policy. This is production
# source compiled directly into the host test, so simulator and firmware use
# the same parser, chunk ledger, and retained/replay policy.
add_executable(ota_mqtt_protocol_test
               ota_mqtt_protocol_test.cpp
               ${FURBLE_ROOT}/src/FurbleOTAMQTT.cpp)
target_include_directories(ota_mqtt_protocol_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(ota_mqtt_protocol_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                        -Wno-unused-parameter)

# Teardown must not dereference a self-deleting client: the peer drops the link
# inline while the GPS stop packet is written.
add_executable(lumix_teardown_drop_test
               lumix_teardown_drop_test.cpp
               ${HOST_PEER_DIR}/LumixVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Lumix.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_link_libraries(lumix_teardown_drop_test PRIVATE furble_host_camera)
target_include_directories(lumix_teardown_drop_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/include)
target_compile_options(lumix_teardown_drop_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-vla-extension -Wno-format
                       -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-sign-compare)
# Admission gate: a new OTA session is refused unless the runtime gate admits it
# (Control idle and enough battery), and a duplicate BEGIN is never refused.
add_executable(ota_mqtt_admission_test
               ota_mqtt_admission_test.cpp
               ${FURBLE_ROOT}/src/FurbleOTAMQTT.cpp)
target_include_directories(ota_mqtt_admission_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(ota_mqtt_admission_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                          -Wno-unused-parameter)

# Durable anti-rollback journal. The test supplies a fault-injecting backend,
# while the production JournalReplayStore and its fixed record format are
# compiled unchanged for host and simulator.
add_executable(ota_replay_store_test
               ota_replay_store_test.cpp
               nvs_replay_fake/nvs_fake.cpp
               ${FURBLE_ROOT}/src/FurbleOTAReplayStore.cpp
               ${FURBLE_ROOT}/src/FurbleOTAReplayStoreNVS.cpp)
target_include_directories(ota_replay_store_test BEFORE PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/nvs_replay_fake
                           ${FURBLE_ROOT}/include)
target_compile_definitions(ota_replay_store_test PRIVATE ESP_PLATFORM)
target_compile_options(ota_replay_store_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                       -Wno-unused-parameter)

add_executable(ota_partition_sink_test
               ota_partition_sink_test.cpp
               ${FURBLE_ROOT}/src/FurbleOTAPartitionSink.cpp
               ${FURBLE_ROOT}/src/FurbleOTAMQTT.cpp)
target_include_directories(ota_partition_sink_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(ota_partition_sink_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                          -Wno-unused-parameter)
# Pure GPS degraded retry policy from include/FurbleGPSPowerCycle.h. The GPS task
# shares this header, so the suite pins the bounded, self recovering behaviour
# that replaced the permanent lock trap. No hardware dependency, so it needs no
# furble sources on its link line.
add_executable(gps_power_cycle_test gps_power_cycle_test.cpp)
target_include_directories(gps_power_cycle_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(gps_power_cycle_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                     -Wno-unused-parameter)

# Bounded GPS fix hold and dead reckoning. include/FurbleGPSHold.h holds the
# bound, the held UTC advance and the straight line projection, free of
# FreeRTOS, LVGL, NVS and the camera headers, so this pins the exact production
# arithmetic without a device. No furble sources on its link line either.
add_executable(gps_hold_test gps_hold_test.cpp)
target_include_directories(gps_hold_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(gps_hold_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                             -Wno-unused-parameter)

# The GPS Data page sentence age formatter. Pure, header only, and the only
# unbounded field on a row with a hard character budget, so its clamp is a unit
# test rather than a scenario assertion. The simulator cannot reach the ages
# that matter: any received byte, including a configuration acknowledgement,
# refreshes the tick.
add_executable(gps_format_test gps_format_test.cpp)
target_include_directories(gps_format_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(gps_format_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                               -Wno-unused-parameter)

# Exercise the simulator's production power profiler accounting for the GPS
# degraded state. This stays host-only because it drives the same profiler API
# that the SDL simulator uses, including lock and virtual-clock transitions.
add_executable(gps_power_profiler_test
               gps_power_profiler_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/power_profiler.cpp)
target_include_directories(gps_power_profiler_test PRIVATE
                           ${FURBLE_ROOT}/sim
                           ${FURBLE_ROOT}/include)
target_compile_options(gps_power_profiler_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                         -Wno-unused-parameter)
target_link_libraries(gps_power_profiler_test PRIVATE Threads::Threads)
# Pure CASIC and NMEA helper logic from lib/furble/protocol/GpsCasic. No BLE or
# NVS dependency, so it compiles the production source directly.
add_executable(gps_casic_test
               gps_casic_test.cpp
               ${FURBLE_PROTOCOL_DIR}/GpsCasic.cpp)
target_include_directories(gps_casic_test PRIVATE ${FURBLE_ROOT}/lib/furble/protocol)
target_compile_options(gps_casic_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                              -Wno-unused-parameter)
# The status freshness predicate is pure logic. Keeping it in a header target
# proves the fix/age boundary without pulling TinyGPSPlus and the UART in.
add_executable(gps_fix_freshness_test gps_fix_freshness_test.cpp)
target_include_directories(gps_fix_freshness_test PRIVATE ${FURBLE_ROOT}/include)
target_compile_options(gps_fix_freshness_test PRIVATE -Wall -Wextra -Werror)

# Real-Control end-to-end harness (PR #131). Compiles the production
# src/FurbleControl.cpp and runs it on real threads against the shared MockNimBLE
# peer, so the full connect/disconnect/reconnect state machine is exercised here,
# not only in the per-Camera tests above. Slim Platform/Power/Settings doubles
# stand in for the hardware singletons the real Control links against, and a small
# real-thread FreeRTOS shim provides the task and queue primitives. The freertos
# shim header is force-included so FurbleControl.h sees the queue and task types.
set(CONTROL_E2E_DIR ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e)
set(CONTROL_TESTSYNC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/testsync)
add_executable(control_e2e_test
               ${CONTROL_E2E_DIR}/control_e2e.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${CONTROL_TESTSYNC_DIR}/TestSyncController.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp)
target_include_directories(control_e2e_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${CONTROL_TESTSYNC_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
# FURBLE_HOST_CONTROL_TEST compiles Control::resetForTest(), the host-only
# restart seam (plan 156). Firmware builds never define it, so the reset does
# not exist on device.
target_compile_definitions(control_e2e_test PRIVATE FURBLE_HOST_CONTROL_TEST FURBLE_TEST_SYNC)
target_compile_options(control_e2e_test PRIVATE -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_e2e_test PRIVATE furble_host_camera)

# AddressSanitizer variant of the same harness, for the restart scenarios
# (plan 156). Their use-after-free class is the late onDisconnect a gone peer's
# client fires after the Camera it calls back into is gone: the mock loads the
# freed callbacks object's vptr to dispatch, a read inside MockNimBLE.cpp, and
# the object it reads belongs to lib/furble/Camera.cpp. Both of those live in
# the furble_host_camera library, which is not built with the sanitizer, so
# neither the read nor the free would be instrumented through it. This target
# therefore compiles the same sources directly, exactly as
# control_reclaim_uaf_test does, so the class is caught as an ASan report
# rather than a segfault or, worse, a pass. It repeats the library's short
# registration timeout so the scenarios keep their timing.
add_executable(control_e2e_asan_test
               ${CONTROL_E2E_DIR}/control_e2e.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${CONTROL_TESTSYNC_DIR}/TestSyncController.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)
target_include_directories(control_e2e_asan_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${CONTROL_TESTSYNC_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR}
                                  ${HOST_CORPUS_DIR})
target_compile_definitions(control_e2e_asan_test
                           PRIVATE FURBLE_HOST_CONTROL_TEST
                                   FURBLE_TEST_SYNC
                                   FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(control_e2e_asan_test PRIVATE -Wall -Wextra -Wno-format
                       -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h
                       -fsanitize=address -g -O1)
target_link_options(control_e2e_asan_test PRIVATE -fsanitize=address)
target_link_libraries(control_e2e_asan_test PRIVATE Threads::Threads)

# Teardown wedge regression (plan 148). Compiles the production Control and
# Fujifilm camera sources with a 10 s registration timeout, an order of
# magnitude above the asserted disconnect bounds, so an uncancellable
# registration wait fails the test deterministically instead of hiding inside
# the 100 ms timeout the shared furble_host_camera library uses. It therefore
# compiles its own copies of the camera sources rather than linking the shared
# library.
add_executable(control_teardown_wedge_test
               control_teardown_wedge_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(control_teardown_wedge_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(control_teardown_wedge_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=10000)
target_compile_options(control_teardown_wedge_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_teardown_wedge_test PRIVATE Threads::Threads)

# Bench reproduction for the Fujifilm Secure stale-bond stall (issue 271, the
# 2026-09-04 hardware session). Not enabled as a ctest yet: it reproduces a
# defect neither master nor plan 170 fixes, because the block is inside a NimBLE
# call that polls nothing. It is the PR 245 gate.
add_executable(control_secure_stall_test
               control_secure_stall_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(control_secure_stall_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/preferences
                                  ${FURBLE_ROOT}/lib/furble
                                  ${FURBLE_ROOT}/lib/blowfish
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
# Deliberately not FURBLE_CONSOLE. getDebugState() is console only, and
# defining it here would compile eleven otherwise unbuilt blocks of Scan.cpp
# into the coverage union as instrumented but uncovered lines, which drops that
# file from 90.20 to 88.01 percent and fails its floor. The assertions below use
# the user-visible consequence instead, which is what the bench measured anyway.
target_compile_definitions(control_secure_stall_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=10000)
# No -Werror here: this target compiles the vendored lib/blowfish, whose
# sign-compare warnings predate the project and are not this test to fix.
target_compile_options(control_secure_stall_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_secure_stall_test PRIVATE Threads::Threads)

# Regression for issue 271: a drained target whose connect attempt is still in
# flight. Models the CanonEOSSmart pairing wait as a blind window inside
# _connect() that ignores the cancel token, so the pre-fix behaviour fails in
# milliseconds instead of waiting out a real vendor timeout.
add_executable(control_zombie_cancel_test
               control_zombie_cancel_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${CONTROL_TESTSYNC_DIR}/TestSyncController.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/FauxNY.cpp)
target_include_directories(control_zombie_cancel_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${CONTROL_TESTSYNC_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(control_zombie_cancel_test PRIVATE FURBLE_TEST_SYNC)
target_compile_options(control_zombie_cancel_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_zombie_cancel_test PRIVATE Threads::Threads)

# Thread-sanitizer proof for the Control::m_ConnectCamera guard. Separate target
# because -fsanitize=thread has to apply to the production sources it links, and
# the rest of the suite is not built under it.
#
# Excluded from the coverage build. Thread sanitizer and source-based coverage
# do not combine usefully, this target adds no coverage the instrumented suite
# does not already have, and its ctest command is a shell wrapper rather than an
# instrumented binary, which tools/coverage.py cannot run llvm-cov against.
if(NOT FURBLE_COVERAGE)
add_executable(control_connect_camera_race_test
               control_connect_camera_race_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/FauxNY.cpp)
target_include_directories(control_connect_camera_race_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_options(control_connect_camera_race_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -fsanitize=thread -fno-omit-frame-pointer -g
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_options(control_connect_camera_race_test PRIVATE -fsanitize=thread)
target_link_libraries(control_connect_camera_race_test PRIVATE Threads::Threads)
target_compile_definitions(control_connect_camera_race_test PRIVATE FURBLE_CONSOLE)
endif()

# Deterministic proof for the connecting-camera snapshot. The UI pairing timer
# reads Control::getConnectingCamera() while the control task publishes the
# shared_ptr around an unlocked BLE connect. Four synchronized readers overlap
# every publication. The production Control and camera sources are compiled
# here rather than linked from the shared library so a TSAN build instruments
# both sides of the handoff.
if(NOT FURBLE_COVERAGE)
add_executable(control_connecting_snapshot_test
               control_connecting_snapshot_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/FauxNY.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(control_connecting_snapshot_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(control_connecting_snapshot_test
                           PRIVATE FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(control_connecting_snapshot_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h
                       -fsanitize=thread -fno-omit-frame-pointer -g)
target_link_options(control_connecting_snapshot_test PRIVATE -fsanitize=thread)
target_link_libraries(control_connecting_snapshot_test PRIVATE Threads::Threads)
endif()

# Deterministic interleaving proof for the STATE_DISCONNECTING republish
# guard (plan 157). Compiles the production Control with FURBLE_TEST_SYNC so
# the named sync points in lib/furble/FurbleTestSync.h are live, and links the
# host controller from testsync/. The barrier on "connectall_returned" parks
# the control task holding a stale DISCONNECTING result while disconnect()
# completes to IDLE, the exact sub-50 ms window the gap analysis could not
# force. Like the wedge test it compiles its own camera sources with a 10 s
# registration timeout so the connect attempt stays parked while the
# disconnect lands.
set(TESTSYNC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/testsync)
add_executable(control_interleave_test
               control_interleave_test.cpp
               ${TESTSYNC_DIR}/TestSyncController.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(control_interleave_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${TESTSYNC_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(control_interleave_test PRIVATE
                           FURBLE_TEST_SYNC
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=10000)
target_compile_options(control_interleave_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_interleave_test PRIVATE Threads::Threads)

# Deterministic interleaving proof for the other half of the same window (plan
# 169). Same build shape as control_interleave_test, plus FURBLE_CONSOLE for the
# debug snapshot: the test waits for Control::getDebugState().reconnectAttempt
# rather than sleeping, which is what makes the interleaving forced. The
# registration timeout is the host default here because this test parks in the
# reconnect retry wait, not in the registration wait.
add_executable(control_abort_republish_test
               control_abort_republish_test.cpp
               ${TESTSYNC_DIR}/TestSyncController.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(control_abort_republish_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${TESTSYNC_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(control_abort_republish_test PRIVATE
                           FURBLE_TEST_SYNC
                           FURBLE_CONSOLE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(control_abort_republish_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_abort_republish_test PRIVATE Threads::Threads)

# Companion GATT transport-e2e harness. The test drives the production
# transport-independent CompanionService through a UUID-addressed mock central,
# then carries location and trigger commands through the real Control and
# Fujifilm camera paths. The companion-specific doubles replace only hardware
# status/GPS/settings surfaces that cannot run on the host.
set(COMPANION_HOST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/companion)
# Shared by the transport end-to-end test and the companion protocol fuzzer so
# both run the production service over the same host doubles and real Control
# and camera stack.
set(COMPANION_GATT_SOURCES
    ${COMPANION_HOST_DIR}/companion_doubles.cpp
    ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
    advertisement_preferences_stub.cpp
    ${COMPANION_HOST_DIR}/companion_hmac.cpp
    ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp
    ${CONTROL_E2E_DIR}/freertos_shim.cpp
    ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
    ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
    ${FURBLE_ROOT}/src/FurbleCompanionService.cpp
    ${FURBLE_ROOT}/src/FurbleControl.cpp
    ${FURBLE_ROOT}/src/FurbleTimezone.cpp
    ${FURBLE_ROOT}/lib/furble/Camera.cpp
    ${FURBLE_ROOT}/lib/furble/CameraList.cpp
    ${FURBLE_ROOT}/lib/furble/Device.cpp
    ${FURBLE_ROOT}/lib/furble/Scan.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOS.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOSRemote.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOSSmart.cpp
    ${FURBLE_ROOT}/lib/furble/DJIOsmo.cpp
    ${FURBLE_ROOT}/lib/furble/FauxNY.cpp
    ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
    ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
    ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
    ${FURBLE_ROOT}/lib/furble/Lumix.cpp
    ${FURBLE_ROOT}/lib/furble/Nikon.cpp
    ${FURBLE_ROOT}/lib/furble/NikonBase.cpp
    ${FURBLE_ROOT}/lib/furble/NikonRemote.cpp
    ${FURBLE_ROOT}/lib/furble/NikonSmart.cpp
    ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
    ${FURBLE_ROOT}/lib/furble/Sony.cpp
    ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
    ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
    ${FURBLE_PROTOCOL_DIR}/CameraListProtocol.cpp
    ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
add_executable(companion_gatt_test companion_gatt_test.cpp ${COMPANION_GATT_SOURCES})
target_include_directories(companion_gatt_test
                           BEFORE PRIVATE
                                  ${COMPANION_HOST_DIR}/doubles
                                  ${COMPANION_HOST_DIR}
                                  ${CONTROL_E2E_DIR}
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${FURBLE_ROOT}/lib/preferences
                                  ${FURBLE_ROOT}/lib/blowfish
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(companion_gatt_test PRIVATE FURBLE_NO_DISPLAY
                                                       FURBLE_HOST_COMPANION_TEST)
# Omitted designated-initializer members are value initialized, so the glibc
# struct tm tail in the Canon and Lumix time helpers is not a real gap.
target_compile_options(companion_gatt_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                   -Wno-missing-field-initializers
                                                   -Wno-sign-compare
                                                   -Wno-unused-parameter
                                                   -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(companion_gatt_test PRIVATE Threads::Threads)

# End-to-end Control disconnect regression. Compiles the real
# src/FurbleControl.cpp against MockNimBLE, the real Camera and the host
# FreeRTOS shim under control/, to lock in the non-blocking interactive
# disconnect fix. The control/ shim dir is first on the include path so it
# shadows the hardware-only FurblePlatform/FurblePower/FurbleSettings headers;
# freertos/FreeRTOS.h is force-included so the FreeRTOS types Control uses are
# defined before FurbleControl.h is parsed. Camera, Device and the mock come
# from the shared furble_host_camera library, not from a stub.
set(HOST_CONTROL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/control)
add_executable(control_disconnect_test
               control_disconnect_test.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${HOST_CONTROL_DIR}/control_shim.cpp)
target_include_directories(control_disconnect_test BEFORE PRIVATE
                           ${HOST_CONTROL_DIR}
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(control_disconnect_test PRIVATE
                       -include ${HOST_CONTROL_DIR}/freertos/FreeRTOS.h
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_libraries(control_disconnect_test PRIVATE furble_host_camera)

# AddressSanitizer regression for the gone-peer client-reclaim use-after-free.
# It cannot link the shared furble_host_camera library: the late onDisconnect
# reads the freed Camera's callbacks vptr to dispatch, and both the mock making
# that read and the Camera being read live in that unsanitized library, so
# neither the access nor the free would be instrumented. This target therefore
# compiles the furble sources directly with -fsanitize=address. The mock models NimBLE's deferred deleteClient on a still
# connected client, so removing the reclaimClient() callback detach makes the
# late onDisconnect land on the freed Camera and ASan aborts.
add_executable(control_reclaim_uaf_test
               control_reclaim_uaf_test.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${HOST_CONTROL_DIR}/control_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)
target_include_directories(control_reclaim_uaf_test BEFORE PRIVATE
                           ${HOST_CONTROL_DIR}
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${HOST_CORPUS_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(control_reclaim_uaf_test PRIVATE
                       -include ${HOST_CONTROL_DIR}/freertos/FreeRTOS.h
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(control_reclaim_uaf_test PRIVATE -fsanitize=address)
target_link_libraries(control_reclaim_uaf_test PRIVATE Threads::Threads)

# AddressSanitizer regression for the mid-connect peer-drop use-after-free. Like
# the reclaim test it compiles lib/furble/Camera.cpp and the mock directly with
# -fsanitize=address, so the dereference _connect() performs after a mid-connect
# self-delete is instrumented. The peer's dropLinkDuringConnect fault frees the
# self-deleting client during the identify write; on the unfixed code _connect()
# then touches the freed client and ASan aborts, so restoring the connect-window
# setSelfDelete(true, true) reproduces the crash.
add_executable(connect_mid_drop_test
               connect_mid_drop_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos_shim.cpp)
target_include_directories(connect_mid_drop_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(connect_mid_drop_test PRIVATE
                       -include ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e/freertos/FreeRTOS.h
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(connect_mid_drop_test PRIVATE -fsanitize=address)
target_link_libraries(connect_mid_drop_test PRIVATE Threads::Threads)

# AddressSanitizer regression for the Ricoh secure-timeout reclaim
# use-after-free. A bonded saved reconnect fails secureConnection() (rc=520)
# while the controller's disconnect event is still queued on the host task. The
# mock's mockMarkLinkDeadEventPending() models that queued event; on the broken
# ordering (self-delete armed before _disconnect()) the event frees the client
# while _disconnect() still dereferences it and ASan aborts.
add_executable(ricoh_secure_timeout_uaf_test
               ricoh_secure_timeout_uaf_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/AdvertisementProtocol.cpp)
target_include_directories(ricoh_secure_timeout_uaf_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${CONTROL_E2E_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(ricoh_secure_timeout_uaf_test PRIVATE
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(ricoh_secure_timeout_uaf_test PRIVATE -fsanitize=address)
target_link_libraries(ricoh_secure_timeout_uaf_test PRIVATE Threads::Threads)

# Deterministic ordering guard for a stalled secureConnection() waiter. The
# callback barrier makes the old early-wake ordering fail without a sleeps-only
# race: the blocked call must remain parked until callback and peer cleanup are
# complete.
add_executable(mock_disconnect_order_test
               mock_disconnect_order_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp)
target_include_directories(mock_disconnect_order_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR})
target_compile_options(mock_disconnect_order_test PRIVATE -Wall -Wextra -Werror
                       -Wno-unused-parameter)
target_link_libraries(mock_disconnect_order_test PRIVATE Threads::Threads)

# Stale-bond recovery through the REAL Control reconnect loop (plan 151). Uses
# the production Scan so the SAVED reconnect path (scan, then connect, then
# secure) is exercised exactly as the bench drives it.
add_executable(fujifilm_repair_needed_test
               fujifilm_repair_needed_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp)
target_include_directories(fujifilm_repair_needed_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR}
                                  ${FURBLE_ROOT}/lib/preferences
                                  ${FURBLE_ROOT}/lib/blowfish)
target_compile_definitions(fujifilm_repair_needed_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(fujifilm_repair_needed_test PRIVATE
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(fujifilm_repair_needed_test PRIVATE Threads::Threads)

# Ricoh standby flap through the real Control (plan 154). The first
# Ricoh-through-Control coverage: a GR IV style standby camera (autonomous
# RicohVirtualCamera flappy mode: rc=520 secureConnection failures, then a
# CameraPower 0x00 notify and a timed link drop) churns the reconnect cycle
# next to a healthy Fujifilm while a mid-cycle disconnect must land in a clean
# IDLE and a follow-up connect must work.
add_executable(ricoh_control_flap_test
               ricoh_control_flap_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${HOST_PEER_DIR}/RicohVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp)
target_include_directories(ricoh_control_flap_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(ricoh_control_flap_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(ricoh_control_flap_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(ricoh_control_flap_test PRIVATE Threads::Threads)

# Absent-peer model through the real Control (plan 154). A paired Fujifilm
# Secure camera is registered but flagged absent, so the SAVED reconnect scan
# is starved (setScanAbsentAddress withholds the advertisement a background
# advertiser keeps emitting) instead of the connect call failing. A mid-scan
# disconnect must abort the scan wait via the connect-cancel poll, land in a
# clean IDLE, and leave a manual connect to the healthy camera responsive.
add_executable(absent_peer_scan_test
               absent_peer_scan_test.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
               ${FURBLE_ROOT}/lib/furble/Scan.cpp
               ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp)
target_include_directories(absent_peer_scan_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${FURBLE_ROOT}/lib/blowfish
                                  ${FURBLE_ROOT}/lib/preferences
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(absent_peer_scan_test PRIVATE
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(absent_peer_scan_test PRIVATE
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -Wno-unused-lambda-capture -Wno-sign-compare
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(absent_peer_scan_test PRIVATE Threads::Threads)

# AddressSanitizer regression for the GATT journal use-after-free. The console
# journal in the pointer-based Camera::gattRead and gattWrite used to read the
# service and characteristic UUIDs from the NimBLERemoteCharacteristic after
# readValue()/writeValue() returned. A peer drop during the operation lets the
# reconnect rediscovery free those remote objects, so the post-operation
# dereference crashed on hardware (LoadProhibited 0xfefefefe in journalRecord
# during a Ricoh GR IV standby drop). The peer's faultNextOperation hook frees
# the client's service cache mid-operation via dropServiceCache(); the fixed
# code journals pre-operation UUID snapshots, so the freed characteristic is
# never touched again. Camera.cpp is compiled here directly with
# -fsanitize=address and FURBLE_CONSOLE so the journal dereference is both
# compiled in and instrumented.
add_executable(gatt_journal_uaf_test
               gatt_journal_uaf_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp)
target_include_directories(gatt_journal_uaf_test BEFORE PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(gatt_journal_uaf_test PRIVATE FURBLE_CONSOLE)
target_compile_options(gatt_journal_uaf_test PRIVATE
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(gatt_journal_uaf_test PRIVATE -fsanitize=address)
target_link_libraries(gatt_journal_uaf_test PRIVATE Threads::Threads)

# Seeded BLE lifecycle fuzzer. Drives the real Control state machine through
# randomised, deterministic (seeded) sequences of connect/disconnect/reconnect
# operations with injected BLE faults and asserts a battery of invariants after
# each. It compiles its own copy of the furble sources and the mock BLE seam so
# the whole call graph (including the mock's client frees) is built under the
# AddressSanitizer + UndefinedBehaviorSanitizer, which is how the reclaim
# use-after-free class is caught. The sanitizer is used only when the host
# toolchain supports it, so the fuzz job stays portable.
set(FUZZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fuzz)
include(CheckCXXCompilerFlag)
set(FUZZ_SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
check_cxx_compiler_flag("" FUZZ_HAVE_SANITIZERS)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT FUZZ_HAVE_SANITIZERS)
  set(FUZZ_SANITIZER_FLAGS "")
endif()

add_executable(control_fuzz
               ${FUZZ_DIR}/control_fuzz.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)
target_include_directories(control_fuzz
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_options(control_fuzz PRIVATE -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h ${FUZZ_SANITIZER_FLAGS})
target_link_options(control_fuzz PRIVATE ${FUZZ_SANITIZER_FLAGS})
target_link_libraries(control_fuzz PRIVATE Threads::Threads)

# Seeded companion protocol fuzzer. It drives the production CompanionService
# packet handlers with malformed auth, settings, camera, location and trigger
# writes, and with fragmented and oversized payloads. The real service, auth
# state machine and camera stack run over the host transport doubles, under the
# same sanitizers as control_fuzz.
add_executable(companion_fuzz
               ${FUZZ_DIR}/companion_fuzz.cpp
               ${COMPANION_GATT_SOURCES})
target_include_directories(companion_fuzz
                           BEFORE PRIVATE
                                  ${COMPANION_HOST_DIR}/doubles
                                  ${COMPANION_HOST_DIR}
                                  ${CONTROL_E2E_DIR}
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${FURBLE_ROOT}/lib/preferences
                                  ${FURBLE_ROOT}/lib/blowfish
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(companion_fuzz PRIVATE FURBLE_NO_DISPLAY
                                                   FURBLE_HOST_COMPANION_TEST)
target_compile_options(companion_fuzz PRIVATE -Wall -Wextra -Wno-format
                                              -Wno-missing-field-initializers -Wno-sign-compare
                                              -Wno-unused-parameter
                                              -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h
                                              ${FUZZ_SANITIZER_FLAGS})
target_link_options(companion_fuzz PRIVATE ${FUZZ_SANITIZER_FLAGS})
target_link_libraries(companion_fuzz PRIVATE Threads::Threads)

# USB console command suite (plan 162). CLAUDE.md names the developer console
# as the automation surface, yet src/FurbleConsole.cpp was compiled by neither
# the host suite nor the simulator, so all 1851 of its lines were unmeasured.
#
# This target compiles the real console against a faithful double of the small
# ESP-IDF console API (a registration table, the built in help command and a
# tokenizing esp_console_run) plus a USB-Serial/JTAG receive queue a test types
# into. Every command handler therefore runs unmodified, driven the way a host
# script drives the device.
#
# Real firmware is linked wherever a host model exists: the Control state
# machine and the Camera stack against MockNimBLE, Settings and Preferences
# against the in-memory NVS store, Power, and the provisioning decoder. The
# hardware-bound subsystems (GPS receiver, BtDebug HCI dumping, IR, feedback,
# SD, the PMIC platform) are doubled at their boundary, so the console's own
# parsing and dispatch still execute and a test asserts the call landed.
set(HOST_CONSOLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/console)
add_executable(console_commands_test
               console_commands_test.cpp
               ${HOST_CONSOLE_DIR}/console_shim.cpp
               ${HOST_CONSOLE_DIR}/crash_log_stub.cpp
               settings_nvs_stub.cpp
               boot_timeline_host_stub.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp
               ${FURBLE_ROOT}/src/FurbleConsole.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/src/FurbleIMU.cpp
               ${FURBLE_ROOT}/src/FurblePower.cpp
               ${FURBLE_ROOT}/src/FurbleProvision.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/ProvisionTLV.cpp
               ${FURBLE_ROOT}/src/FurbleUIGesture.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp)
target_include_directories(console_commands_test BEFORE PRIVATE
                           ${HOST_CONSOLE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/settings_stubs
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/preferences)
target_compile_definitions(console_commands_test PRIVATE
                           # Every command prints to stdout, so the suite reads
                           # its assertions back out of a captured stdout. Keep
                           # that file in the build tree, not in /tmp.
                           FURBLE_CONSOLE_CAPTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}"
                           FURBLE_CONSOLE
                           FURBLE_M5STICKS3
                           FURBLE_BOOT_TIMELINE_HOST_TEST=1
                           FURBLE_VERSION="host"
                           CONFIG_ESP_CONSOLE_UART_NUM=0
                           CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
                           CONFIG_SPIRAM
                           CONFIG_LV_USE_PERF_MONITOR
                           FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_compile_options(console_commands_test PRIVATE
                       -include ${HOST_CONSOLE_DIR}/freertos/FreeRTOS.h
                       -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -Wno-unused-const-variable -Wno-c99-designator
                       -Wno-c++20-designator
                       # include/FurblePower.h names an anonymous struct through
                       # a typedef, which is the house style across the firmware
                       # headers. Nothing here depends on C linkage for it.
                       -Wno-non-c-typedef-for-linkage)
target_link_libraries(console_commands_test PRIVATE Threads::Threads)

target_sources(settings_nvs_roundtrip_test PRIVATE ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp)
target_sources(provision_apply_test PRIVATE ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp)
target_sources(companion_gatt_test PRIVATE ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp)
target_sources(console_commands_test PRIVATE ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp)

if(BUILD_TESTING)
  add_test(NAME fujifilm-camera COMMAND fujifilm_camera_test)
  add_test(NAME camera-pairing COMMAND camera_pairing_test)
  add_test(NAME camera-pairing-peer COMMAND camera_pairing_peer_test)
  add_test(NAME camera-pairing-race COMMAND camera_pairing_race_test)
  add_test(NAME golden-capture-replay
           COMMAND corpus_replay_test
                   ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/x100vi/synthetic.golden)
  add_test(NAME camera-lifecycle COMMAND lifecycle_test)
  add_test(NAME wrap-safe-time COMMAND wrap_safe_time_test)
  add_test(NAME time-keeper-policy COMMAND time_keeper_policy_test)
  add_test(NAME time-keeper-persistence COMMAND time_keeper_persistence_test)
  add_test(NAME timezone COMMAND timezone_test)
  add_test(NAME real-wifi-lifecycle COMMAND real_wifi_lifecycle_test)
  add_test(NAME boot-timeline COMMAND boot_timeline_test)
  add_test(NAME boot-policy COMMAND boot_policy_test)
  foreach(preferences_case missing empty zero-count empty-string typeerror embedded-nul corruption failed-save)
    add_test(NAME preferences-sim-${preferences_case}
             COMMAND actualPreferencesSim ${preferences_case})
  endforeach()
  add_test(NAME camera-list-protocol COMMAND camera_list_protocol_test)
  add_test(NAME advertisement-dispatch COMMAND advertisement_dispatch_test)
  add_test(NAME camera-regression COMMAND camera_regression_test)
  add_test(NAME fujifilm-device-name COMMAND fujifilm_device_name_test)
  add_test(NAME ricoh-focus COMMAND ricoh_focus_test)
  add_test(NAME ricoh-sleep-gate COMMAND ricoh_sleep_gate_test)

  add_executable(vendor_protocol_conformance_test
                 vendor_protocol_conformance_test.cpp
                 ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
                 ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
  target_include_directories(vendor_protocol_conformance_test
                             PRIVATE ${FURBLE_ROOT}/lib/furble/protocol)
  target_compile_options(vendor_protocol_conformance_test PRIVATE -Wall -Wextra -Werror)
  add_test(NAME vendor-protocol-conformance COMMAND vendor_protocol_conformance_test)
  add_test(NAME fujifilm-protocol COMMAND fujifilm_protocol_test)
  add_test(NAME settings-table COMMAND settings_table_test ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  add_test(NAME scan-generation COMMAND scan_generation_test)
  add_test(NAME bt-debug-journal COMMAND bt_debug_journal_test)
  add_test(NAME bt-debug-journal-s3-psram COMMAND bt_debug_journal_s3_test)
  add_test(NAME bt-debug-journal-s3-fallback COMMAND bt_debug_journal_s3_fallback_test)
  add_test(NAME camera-journal-events COMMAND camera_journal_event_test)
  add_test(NAME saved-scan-cancel COMMAND saved_scan_cancel_test)
  add_test(NAME saved-scan-delivery COMMAND saved_scan_delivery_test)
  add_test(NAME ble-registration-cleanup COMMAND ble_registration_cleanup_test)
  add_test(NAME fujifilm-registration-gate COMMAND fujifilm_registration_gate_test)
  add_test(NAME fujifilm-stale-bond COMMAND fujifilm_stale_bond_test)
  add_test(NAME fujifilm-repair-needed COMMAND fujifilm_repair_needed_test)
  add_test(NAME control-secure-stall COMMAND control_secure_stall_test)
  add_test(NAME fujifilm-registration-cancel COMMAND fujifilm_registration_cancel_test)
  add_test(NAME settings-nvs-roundtrip COMMAND settings_nvs_roundtrip_test)
  add_test(NAME settings-boot COMMAND settings_boot_test)
  add_executable(settings_default_policy_test settings_default_policy_test.cpp)
  target_include_directories(settings_default_policy_test PRIVATE ${FURBLE_ROOT}/include)
  target_compile_options(settings_default_policy_test PRIVATE -Wall -Wextra -Werror)
  add_executable(settings_default_policy_sticks3_test settings_default_policy_test.cpp)
  target_include_directories(settings_default_policy_sticks3_test PRIVATE ${FURBLE_ROOT}/include)
  target_compile_definitions(settings_default_policy_sticks3_test PRIVATE FURBLE_M5STICKS3)
  target_compile_options(settings_default_policy_sticks3_test PRIVATE -Wall -Wextra -Werror)
  add_executable(settings_default_policy_core_test settings_default_policy_test.cpp)
  target_include_directories(settings_default_policy_core_test PRIVATE ${FURBLE_ROOT}/include)
  target_compile_definitions(settings_default_policy_core_test PRIVATE FURBLE_M5STACK_CORE)
  target_compile_options(settings_default_policy_core_test PRIVATE -Wall -Wextra -Werror)
  add_test(NAME settings-default-policy COMMAND settings_default_policy_test)
  add_test(NAME settings-default-policy-sticks3 COMMAND settings_default_policy_sticks3_test)
  add_test(NAME settings-default-policy-core COMMAND settings_default_policy_core_test)
  add_test(NAME auto-off-policy COMMAND auto_off_policy_test)
  add_test(NAME provision-tlv COMMAND provision_tlv_test)
  add_test(NAME provision-apply COMMAND provision_apply_test)
  add_test(NAME provision-apply-mqtt COMMAND provision_apply_mqtt_test)
  add_test(NAME text-size-policy COMMAND text_size_policy_test)
  add_test(NAME imu-motion-encoding COMMAND imu_motion_encoding_test)
  add_test(NAME text-size-policy-stickc COMMAND text_size_policy_stickc_test)
  add_test(NAME stale-connect COMMAND stale_connect_test)
  add_test(NAME client-leak COMMAND client_leak_test)
  add_test(NAME reconnect-stuck COMMAND reconnect_stuck_test)
  add_test(NAME gpx-writer COMMAND gpx_writer_test)
  add_test(NAME reconnect-backoff COMMAND reconnect_backoff_test)
  add_test(NAME battery-saver COMMAND battery_saver_test)
  add_test(NAME mqtt-owner COMMAND mqtt_owner_test)
  foreach(seed 1 2 3 7 42)
    add_test(NAME mqtt-fuzz-seed-${seed} COMMAND mqtt_fuzz --seed ${seed} --iterations 300)
  endforeach()
  add_test(NAME pmic-recovery COMMAND pmic_recovery_test)
  add_test(NAME sim-clock COMMAND sim_clock_test)
  add_test(NAME sim-action-parser COMMAND sim_action_parser_test)
  add_test(NAME sim-scheduler COMMAND sim_scheduler_test)
  set_tests_properties(sim-scheduler PROPERTIES TIMEOUT 10)
  add_test(NAME sim-watchdog COMMAND sim_watchdog_test)
  set_tests_properties(sim-watchdog PROPERTIES TIMEOUT 10)
  add_test(NAME sim-fuzz-machine COMMAND sim_fuzz_machine_test)
  add_test(NAME sim-power-profiler COMMAND sim_power_profiler_test)
  add_test(NAME preferences-sweep COMMAND preferences_sweep_test)
  # Run several independent profiler processes so ctest -j covers report-path
  # isolation. Each process must be able to create and clean up its own report.
  foreach(profiler_worker RANGE 1 4)
    add_test(NAME sim-power-profiler-concurrent-${profiler_worker}
             COMMAND sim_power_profiler_test)
  endforeach()
  add_test(NAME ethernet-transport COMMAND ethernet_test)
  add_test(NAME poe-power-model COMMAND poe_power_model_test)
  add_test(NAME ota-state-machine COMMAND ota_state_machine_test)
  add_test(NAME lumix-teardown-drop COMMAND lumix_teardown_drop_test)
  add_test(NAME ota-mqtt-protocol COMMAND ota_mqtt_protocol_test)
  add_test(NAME ota-mqtt-admission COMMAND ota_mqtt_admission_test)
  add_test(NAME ota-replay-store COMMAND ota_replay_store_test)
  add_test(NAME ota-partition-sink COMMAND ota_partition_sink_test)
  add_test(NAME gps-format COMMAND gps_format_test)
  add_test(NAME gps-hold COMMAND gps_hold_test)
  add_test(NAME gps-power-cycle COMMAND gps_power_cycle_test)
  add_test(NAME gps-power-profiler COMMAND gps_power_profiler_test)
  add_test(NAME gps-casic COMMAND gps_casic_test)
  add_test(NAME gps-fix-freshness COMMAND gps_fix_freshness_test)
  add_test(NAME fault-tooling COMMAND fault_tooling_test)
  # The console suite drives the real console task through its transport, and
  # 'perf tasks' samples over a full second, so give it room while still
  # bounding a wedged command.
  add_test(NAME console-commands COMMAND console_commands_test)
  set_tests_properties(console-commands PROPERTIES TIMEOUT 120)

  add_test(NAME control-disconnect COMMAND control_disconnect_test)

  # Teardown wedge regression (plan 148). Bounded by a hard timeout so a
  # reintroduced uncancellable wait fails the suite instead of hanging it.
  add_test(NAME control-teardown-wedge COMMAND control_teardown_wedge_test)
  set_tests_properties(control-teardown-wedge PROPERTIES TIMEOUT 60)

  add_test(NAME control-zombie-cancel COMMAND control_zombie_cancel_test)
  set_tests_properties(control-zombie-cancel PROPERTIES TIMEOUT 60)

  if(NOT FURBLE_COVERAGE)
    add_test(NAME control-connect-camera-race
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_tsan_race.sh
                     $<TARGET_FILE:control_connect_camera_race_test>)
    set_tests_properties(control-connect-camera-race PROPERTIES TIMEOUT 180)
    add_test(NAME control-connecting-snapshot
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_tsan_race.sh
                     $<TARGET_FILE:control_connecting_snapshot_test>
                     "control connecting-camera snapshot: PASS")
    set_tests_properties(control-connecting-snapshot PROPERTIES TIMEOUT 120)
    # The shell contract emits no profraw, so keep it out of coverage's CTest
    # profile inventory while retaining the normal host and standalone checks.
    add_test(NAME control-tsan-wrapper-contract
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/run_tsan_race_contract.sh)
    set_tests_properties(control-tsan-wrapper-contract PROPERTIES TIMEOUT 10)
  endif()

  # Interleaving proof for the DISCONNECTING republish guard (plan 157). The
  # sync-point barriers carry their own timeouts, the ctest timeout is the
  # backstop for a wedged control task. Both sit above the 30 s
  # DISCONNECT_WAIT_MAX_MS so a stalled teardown reports its own cause rather
  # than a sync point timeout.
  add_test(NAME control-interleave COMMAND control_interleave_test)
  set_tests_properties(control-interleave PROPERTIES TIMEOUT 120)

  # Interleaving proof for the aborted-connect republish (plan 169). Waits out
  # two reconnect retries before it forces the window, so it is slower than the
  # other interleave test. Same timeout reasoning: above DISCONNECT_WAIT_MAX_MS
  # so a stalled teardown reports its own cause.
  add_test(NAME control-abort-republish COMMAND control_abort_republish_test)
  set_tests_properties(control-abort-republish PROPERTIES TIMEOUT 120)
  add_test(NAME control-reclaim-uaf COMMAND control_reclaim_uaf_test)
  add_test(NAME connect-mid-drop COMMAND connect_mid_drop_test)
  add_test(NAME ricoh-secure-timeout-uaf COMMAND ricoh_secure_timeout_uaf_test)
  add_test(NAME gatt-journal-uaf COMMAND gatt_journal_uaf_test)
  add_test(NAME mock-disconnect-order COMMAND mock_disconnect_order_test)
  add_test(NAME companion-gatt COMMAND companion_gatt_test)
  add_test(NAME companion-auth
           COMMAND companion_auth_test ${FURBLE_ROOT}/tests/protocol/golden/companion_auth.json)

  # Peer realism through the real Control (plan 154). Both carry their own
  # watchdog, the ctest timeout is the backstop for a wedged control task.
  add_test(NAME ricoh-control-flap COMMAND ricoh_control_flap_test)
  set_tests_properties(ricoh-control-flap PROPERTIES TIMEOUT 90)
  add_test(NAME absent-peer-scan COMMAND absent_peer_scan_test)
  set_tests_properties(absent-peer-scan PROPERTIES TIMEOUT 60)

  # Real-Control end-to-end scenarios. Each runs the production control state
  # machine end to end against the mock peer and is registered individually so a
  # failure names the scenario.
  foreach(scenario
          empty-target-connect-stays-idle
          fresh-connect
          dead-camera-disconnect-no-freeze
          connect-after-dead-disconnect
          stale-session-reconnect
          false-connected-guard
          transient-connect-recovers
          scoped-bounded-retry-gap
          client-pool-exhaustion
          multi-connect-fujifilm
          reconnect-shutter-drop
          multi-flappy-disconnect
          flappy-cancel-stress
          flappy-peer-autonomous
          restart-restore-commandable
          restart-stalled-peer-reclaim)
    add_test(NAME control-e2e-${scenario} COMMAND control_e2e_test ${scenario})
  endforeach()

  # The restart scenarios also run instrumented, because the gone-peer reboot
  # asserts the absence of a use-after-free and only the sanitizer can see it.
  foreach(scenario
          restart-restore-commandable
          restart-stalled-peer-reclaim)
    add_test(NAME control-e2e-asan-${scenario} COMMAND control_e2e_asan_test ${scenario})
  endforeach()

  # Regression guards for the FujifilmBasic missing-shutter connect faults.
  # FujifilmBasic::_connect now returns false when the shutter service or
  # characteristic is missing, so each test connects cleanly: no null deref
  # crash on a missing service, no silent success with a null shutter on a
  # missing characteristic. Both run as normal passing tests.
  add_test(NAME fujifilm-missing-shutter-service COMMAND fujifilm_missing_shutter_service_test)
  add_test(NAME fujifilm-missing-shutter-char COMMAND fujifilm_missing_shutter_char_test)

  # Seeded BLE lifecycle fuzz. Each fixed seed is a deterministic, reproducible
  # run; running several widens the explored interleavings while staying green on
  # the fixed code. A regression or a new bug turns the matching seed red and the
  # failure line prints the exact seed and operation to replay.
  foreach(seed 1 2 3 7 42)
    add_test(NAME control-fuzz-seed-${seed} COMMAND control_fuzz ${seed} 10)
  endforeach()

  # Seed-pinned regression guards for the two FujifilmBasic missing-shutter
  # findings, driven through the real control lifecycle by the fuzzer. Both are
  # now fixed: FujifilmBasic::_connect returns false when the shutter service or
  # characteristic is missing. Each repro runs clean as a normal passing test.
  # The service repro drives the connect in a forked child and asserts it never
  # crashes; the characteristic repro asserts the connect never reports a
  # connected camera whose shutter is silently dead.
  add_test(NAME control-fuzz-repro-missing-shutter-service
           COMMAND control_fuzz --repro missing-shutter-service 1)
  add_test(NAME control-fuzz-repro-missing-shutter-char
           COMMAND control_fuzz --repro missing-shutter-char 1)

  # Deterministic guard for the connect-handshake-phase fault sweep. Injects a
  # peer reset or a rejected write at more than one point across the connect
  # handshake (the pairing token exchange and the identifier write), so a
  # mid-connect fault is exercised before the pairing completes as well as during
  # it, not only after a link is established. The fixed control code aborts each
  # cleanly and recovers, so this passes; the deeper inline-self-delete point that
  # reproduces the mid-connect use-after-free crash is owned by PR #155
  # (fix/connect-crash-mid-drop) via the shared dropLinkDuringConnect hook, which
  # the guarded opMidConnectSelfDeleteDrop consumes once that lands.
  add_test(NAME control-fuzz-repro-handshake-phase-drop
           COMMAND control_fuzz --repro handshake-phase-drop 1)

  # Companion protocol fuzz. The fixed seeds keep malformed auth, settings and
  # camera packet streams reproducible; every finding prints the exact seed and
  # operation so it can be pinned as a regression.
  foreach(seed 1 2 3 7 42)
    add_test(NAME companion-fuzz-seed-${seed} COMMAND companion_fuzz --seed ${seed} --iterations 300)
  endforeach()
endif()

# Peer contract: every virtual camera constructs, advertises, exposes its
# primary service, and exposes a writable characteristic. This is a harness
# smoke test, not a camera behavior claim.
include(${HOST_PEER_DIR}/CanonNikonPeers.cmake)
include(${HOST_PEER_DIR}/SonyLumixDjiPeers.cmake)

add_executable(peer_contract_test
               peer_contract_test.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${CANON_NIKON_PEER_SOURCES}
               ${CANON_NIKON_PEER_DEPENDENCY_SOURCES}
               ${SONY_LUMIX_DJI_PEER_SOURCES})
target_include_directories(peer_contract_test PRIVATE
                           ${HOST_NIMBLE_DIR}
                           ${CANON_NIKON_PEER_INCLUDE_DIRS}
                           ${SONY_LUMIX_DJI_PEER_INCLUDE_DIRS})
target_link_libraries(peer_contract_test PRIVATE Threads::Threads)
add_test(NAME peer-contract COMMAND peer_contract_test)

# One raw profile per test, named after the test. ctest gives every test the
# same environment, so without this every profile is named after the process id
# alone and a test that wrote nothing cannot be told from a test that never ran.
# tools/coverage.py checks these files and fails the run naming the test whose
# profile is missing or empty. The dot before the process id is the separator
# that check matches on: test names carry hyphens and one name is a hyphenated
# prefix of another, so a hyphen there would let a longer name's profile stand
# in for a shorter name that wrote nothing.
if(FURBLE_COVERAGE AND FURBLE_PROFILE_DIR)
  get_property(furble_test_names DIRECTORY PROPERTY TESTS)
  foreach(furble_test_name IN LISTS furble_test_names)
    set_tests_properties(${furble_test_name} PROPERTIES ENVIRONMENT
                         "LLVM_PROFILE_FILE=${FURBLE_PROFILE_DIR}/${furble_test_name}.%p.profraw")
  endforeach()
endif()

# Sim-first boundary conformance. Each target compiles the production driver or
# service path unchanged and replaces only the lowest hardware boundary. These
# are the linked-code evidence the flash-readiness manifest names for the
# approximate sim boundaries.
set(BOUNDARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boundaries)
set(BOUNDARY_SHIM_DIR ${BOUNDARY_DIR}/shims)
set(BOUNDARY_SUPPORT ${BOUNDARY_DIR}/boundary_support.cpp ${BOUNDARY_DIR}/m5_stub.cpp)
set(BOUNDARY_COMMON_INCLUDES
    ${BOUNDARY_DIR}
    ${BOUNDARY_SHIM_DIR}
    ${CONTROL_E2E_DIR}
    ${SETTINGS_TEST_STUB_DIR}
    ${FURBLE_ROOT}/include
    ${FURBLE_ROOT}/lib/preferences
    ${FURBLE_ROOT}/lib/furble)
set(BOUNDARY_FREERTOS_INCLUDE
    -include${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
set(BOUNDARY_SETTINGS_SOURCES
    settings_nvs_stub.cpp
    ${FURBLE_ROOT}/src/FurbleSettings.cpp
    ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
    ${FURBLE_ROOT}/src/FurbleTimezone.cpp
    ${FURBLE_ROOT}/lib/preferences/Preferences.cpp
    ${CONTROL_E2E_DIR}/freertos_shim.cpp)

add_executable(boundary_ir_test
               ${BOUNDARY_DIR}/ir_driver_test.cpp
               ${BOUNDARY_SUPPORT}
               ${BOUNDARY_SETTINGS_SOURCES}
               ${FURBLE_ROOT}/src/FurbleIR.cpp)
target_include_directories(boundary_ir_test PRIVATE ${BOUNDARY_COMMON_INCLUDES})
target_compile_options(boundary_ir_test PRIVATE ${BOUNDARY_FREERTOS_INCLUDE}
                                                -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(boundary_ir_test PRIVATE FURBLE_M5STICKS3 CONFIG_SPIRAM
                                                     CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=1)
target_link_libraries(boundary_ir_test PRIVATE Threads::Threads)

add_executable(boundary_sd_test
               ${BOUNDARY_DIR}/sd_driver_test.cpp
               ${BOUNDARY_SUPPORT}
               ${BOUNDARY_SETTINGS_SOURCES}
               ${FURBLE_ROOT}/src/FurbleSD.cpp
               ${FURBLE_ROOT}/src/FurbleGPX.cpp)
target_include_directories(boundary_sd_test PRIVATE ${BOUNDARY_COMMON_INCLUDES})
target_compile_options(boundary_sd_test PRIVATE ${BOUNDARY_FREERTOS_INCLUDE}
                                                -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(boundary_sd_test PRIVATE
                           FURBLE_M5STICKS3
                           FURBLE_SD_MOUNT_POINT="/tmp/furble-boundary-sd"
                           FURBLE_GPX_DIRECTORY="/tmp/furble-boundary-sd/furble"
                           FURBLE_BOUNDARY_SD_MOUNT="/tmp/furble-boundary-sd")
target_link_libraries(boundary_sd_test PRIVATE Threads::Threads)

add_executable(boundary_feedback_test
               ${BOUNDARY_DIR}/feedback_driver_test.cpp
               ${BOUNDARY_SUPPORT}
               ${BOUNDARY_SETTINGS_SOURCES}
               ${FURBLE_ROOT}/src/FurbleFeedback.cpp)
target_include_directories(boundary_feedback_test PRIVATE ${BOUNDARY_COMMON_INCLUDES})
target_compile_options(boundary_feedback_test PRIVATE ${BOUNDARY_FREERTOS_INCLUDE}
                                                      -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(boundary_feedback_test PRIVATE FURBLE_M5STICKS3)
target_link_libraries(boundary_feedback_test PRIVATE Threads::Threads)

add_executable(boundary_ethernet_test
               ${BOUNDARY_DIR}/ethernet_w5500_test.cpp
               ${BOUNDARY_SUPPORT}
               ${FURBLE_ROOT}/src/FurbleEthernet.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp)
target_include_directories(boundary_ethernet_test PRIVATE ${BOUNDARY_COMMON_INCLUDES})
target_compile_options(boundary_ethernet_test PRIVATE ${BOUNDARY_FREERTOS_INCLUDE}
                                                      -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(boundary_ethernet_test PRIVATE ESP_PLATFORM FURBLE_ETHERNET)
target_link_libraries(boundary_ethernet_test PRIVATE Threads::Threads)

set(BOUNDARY_COMPANION_SOURCES
    ${BOUNDARY_DIR}/companion_gatt_transport_test.cpp
    ${COMPANION_HOST_DIR}/companion_doubles.cpp
    ${COMPANION_HOST_DIR}/companion_hmac.cpp
    ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
    ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp
    ${FURBLE_ROOT}/src/FurbleCompanionService.cpp
    ${FURBLE_ROOT}/src/FurbleCompanion.cpp
    ${FURBLE_ROOT}/src/FurbleCompanionIdentity.cpp
    ${FURBLE_ROOT}/src/FurbleControl.cpp
    ${FURBLE_ROOT}/src/FurbleTimezone.cpp
    ${FURBLE_ROOT}/lib/furble/Camera.cpp
    ${FURBLE_ROOT}/lib/furble/CameraList.cpp
    ${FURBLE_ROOT}/lib/furble/Device.cpp
    ${FURBLE_ROOT}/lib/furble/Scan.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOS.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOSRemote.cpp
    ${FURBLE_ROOT}/lib/furble/CanonEOSSmart.cpp
    ${FURBLE_ROOT}/lib/furble/DJIOsmo.cpp
    ${FURBLE_ROOT}/lib/furble/FauxNY.cpp
    ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
    ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
    ${FURBLE_ROOT}/lib/furble/FujifilmSecure.cpp
    ${FURBLE_ROOT}/lib/furble/Lumix.cpp
    ${FURBLE_ROOT}/lib/furble/Nikon.cpp
    ${FURBLE_ROOT}/lib/furble/NikonBase.cpp
    ${FURBLE_ROOT}/lib/furble/NikonRemote.cpp
    ${FURBLE_ROOT}/lib/furble/NikonSmart.cpp
    ${FURBLE_ROOT}/lib/furble/Ricoh.cpp
    ${FURBLE_ROOT}/lib/furble/Sony.cpp
    ${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp
    ${FURBLE_PROTOCOL_DIR}/AdvertisementProtocol.cpp
    ${FURBLE_PROTOCOL_DIR}/CameraListProtocol.cpp
    ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
    ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
    ${FURBLE_ROOT}/sim/PreferencesSim.cpp
    ${CONTROL_E2E_DIR}/freertos_shim.cpp)
add_executable(boundary_companion_test ${BOUNDARY_COMPANION_SOURCES})
target_include_directories(boundary_companion_test BEFORE PRIVATE
                           ${COMPANION_HOST_DIR}/doubles
                           ${COMPANION_HOST_DIR}
                           ${CONTROL_E2E_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${BOUNDARY_DIR}
                           ${BOUNDARY_SHIM_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/furble
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/blowfish
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR})
target_compile_definitions(boundary_companion_test PRIVATE
                           FURBLE_NO_DISPLAY
                           FURBLE_HOST_COMPANION_TEST
                           FURBLE_WEBUI=1
                           FURBLE_VERSION="boundary")
target_compile_options(boundary_companion_test PRIVATE
                       ${BOUNDARY_FREERTOS_INCLUDE}
                       -Wall -Wextra -Wno-format -Wno-sign-compare
                       -Wno-unused-parameter)
target_link_libraries(boundary_companion_test PRIVATE Threads::Threads)

add_executable(boundary_webui_tls_test
               ${BOUNDARY_DIR}/webui_tls_socket_test.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/webui/webui_platform_stubs.cpp
               ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/cJSON.cpp
               ${COMPANION_HOST_DIR}/companion_hmac.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionAuth.cpp
               ${FURBLE_ROOT}/src/FurbleWebUI.cpp
               ${FURBLE_ROOT}/src/FurbleMemoryPressure.cpp)
target_include_directories(boundary_webui_tls_test PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/webui
                           ${CMAKE_CURRENT_SOURCE_DIR}/mqtt
                           ${COMPANION_HOST_DIR}
                           ${FURBLE_ROOT}/include)
target_compile_definitions(boundary_webui_tls_test PRIVATE
                           FURBLE_MQTT=1 FURBLE_WEBUI=1 FURBLE_WEBUI_HOST_TEST=1)
target_compile_options(boundary_webui_tls_test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(boundary_webui_tls_test PRIVATE Threads::Threads)

set(HEADLESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/headless)
add_executable(boundary_headless_service_test
               ${HEADLESS_DIR}/headless_service_test.cpp
               ${HEADLESS_DIR}/headless_doubles.cpp
               ${FURBLE_ROOT}/src/main.cpp
               settings_nvs_stub.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/src/FurbleNetworkSettings.cpp
               ${FURBLE_ROOT}/src/FurbleTimezone.cpp
               ${FURBLE_ROOT}/src/FurbleBootTimeline.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp
               ${BOUNDARY_DIR}/m5_stub.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp)
target_include_directories(boundary_headless_service_test BEFORE PRIVATE
                           ${HEADLESS_DIR}/include
                           ${HEADLESS_DIR}
                           ${BOUNDARY_SHIM_DIR}
                           ${CONTROL_E2E_DIR}
                           ${SETTINGS_TEST_STUB_DIR}
                           ${FURBLE_ROOT}/include
                           ${FURBLE_ROOT}/lib/preferences
                           ${FURBLE_ROOT}/lib/furble)
target_compile_definitions(boundary_headless_service_test PRIVATE
                           FURBLE_NO_DISPLAY
                           FURBLE_CONSOLE
                           FURBLE_M5STICKS3
                           FURBLE_WEBUI=1
                           FURBLE_MQTT=1
                           FURBLE_ETHERNET
                           FURBLE_BOOT_TIMELINE_HOST_TEST=1)
target_compile_options(boundary_headless_service_test PRIVATE
                       ${BOUNDARY_FREERTOS_INCLUDE}
                       -include${BOUNDARY_SHIM_DIR}/esp_log.h
                       -Wall -Wextra -Wno-unused-parameter -Wno-format)
target_link_libraries(boundary_headless_service_test PRIVATE Threads::Threads)

if(BUILD_TESTING)
  foreach(mode protocols psram-fallback queue-timeout unsupported)
    add_test(NAME boundary-ir-${mode} COMMAND boundary_ir_test ${mode})
  endforeach()
  foreach(mode mount-write mount-fail full removed)
    add_test(NAME boundary-sd-${mode} COMMAND boundary_sd_test ${mode})
  endforeach()
  set_tests_properties(boundary-sd-mount-write boundary-sd-mount-fail
                       boundary-sd-full boundary-sd-removed
                       PROPERTIES RESOURCE_LOCK boundary-sd)
  foreach(mode sound light overcurrent low-battery vibration)
    add_test(NAME boundary-feedback-${mode} COMMAND boundary_feedback_test ${mode})
  endforeach()
  add_test(NAME boundary-ethernet-w5500 COMMAND boundary_ethernet_test)
  add_test(NAME boundary-companion-gatt COMMAND boundary_companion_test)
  set_tests_properties(boundary-companion-gatt PROPERTIES ENVIRONMENT
                       "FURBLE_SIM_PREFS=${CMAKE_CURRENT_BINARY_DIR}/boundary-companion-prefs.bin")
  add_test(NAME boundary-webui-tls COMMAND boundary_webui_tls_test)
  add_test(NAME boundary-headless-service COMMAND boundary_headless_service_test)
endif()
