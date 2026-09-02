cmake_minimum_required(VERSION 3.16)

project(furble_host_camera LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FURBLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(HOST_NIMBLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nimble)
set(HOST_PEER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/peer)
set(HOST_CORPUS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

add_library(furble_host_camera STATIC
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
            ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)

target_include_directories(furble_host_camera
                           PUBLIC ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR}
                                  ${HOST_CORPUS_DIR}
                                  ${FURBLE_ROOT}/lib/furble)
target_compile_options(furble_host_camera PRIVATE -Wall -Wextra -Werror -Wno-format
                                                 -Wno-unused-parameter)
target_compile_definitions(furble_host_camera PRIVATE FURBLE_HOST_REGISTRATION_TIMEOUT_MS=100)
target_link_libraries(furble_host_camera PUBLIC Threads::Threads)

add_executable(fujifilm_camera_test fujifilm_camera_test.cpp)
target_link_libraries(fujifilm_camera_test PRIVATE furble_host_camera)
target_compile_options(fujifilm_camera_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                     -Wno-unused-parameter)

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
               ${FURBLE_ROOT}/sim/freertos.cpp)
target_include_directories(sim_scheduler_test PRIVATE ${FURBLE_ROOT}/include
                                                       ${FURBLE_ROOT}/sim
                                                       ${FURBLE_ROOT}/sim/shim)
target_compile_options(sim_scheduler_test PRIVATE -Wall -Wextra -Werror -Wno-format
                                                  -Wno-unused-parameter)
target_link_libraries(sim_scheduler_test PRIVATE Threads::Threads)

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

# Real-Control end-to-end harness (PR #131). Compiles the production
# src/FurbleControl.cpp and runs it on real threads against the shared MockNimBLE
# peer, so the full connect/disconnect/reconnect state machine is exercised here,
# not only in the per-Camera tests above. Slim Platform/Power/Settings doubles
# stand in for the hardware singletons the real Control links against, and a small
# real-thread FreeRTOS shim provides the task and queue primitives. The freertos
# shim header is force-included so FurbleControl.h sees the queue and task types.
set(CONTROL_E2E_DIR ${CMAKE_CURRENT_SOURCE_DIR}/control_e2e)
add_executable(control_e2e_test
               ${CONTROL_E2E_DIR}/control_e2e.cpp
               ${CONTROL_E2E_DIR}/doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp)
target_include_directories(control_e2e_test
                           BEFORE PRIVATE
                                  ${CMAKE_CURRENT_SOURCE_DIR}
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
# FURBLE_HOST_CONTROL_TEST compiles Control::resetForTest(), the host-only
# restart seam (plan 156). Firmware builds never define it, so the reset does
# not exist on device.
target_compile_definitions(control_e2e_test PRIVATE FURBLE_HOST_CONTROL_TEST)
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
               ${FURBLE_ROOT}/src/FurbleControl.cpp
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
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR}
                                  ${HOST_CORPUS_DIR})
target_compile_definitions(control_e2e_asan_test
                           PRIVATE FURBLE_HOST_CONTROL_TEST
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

# Companion GATT transport-e2e harness. The test drives the production
# transport-independent CompanionService through a UUID-addressed mock central,
# then carries location and trigger commands through the real Control and
# Fujifilm camera paths. The companion-specific doubles replace only hardware
# status/GPS/settings surfaces that cannot run on the host.
set(COMPANION_HOST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/companion)
add_executable(companion_gatt_test
               companion_gatt_test.cpp
               ${COMPANION_HOST_DIR}/companion_doubles.cpp
               ${CONTROL_E2E_DIR}/freertos_shim.cpp
               ${HOST_NIMBLE_DIR}/MockNimBLE.cpp
               ${HOST_PEER_DIR}/FujifilmVirtualCamera.cpp
               ${FURBLE_ROOT}/src/FurbleCompanionService.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp)
target_include_directories(companion_gatt_test
                           BEFORE PRIVATE
                                  ${COMPANION_HOST_DIR}/doubles
                                  ${COMPANION_HOST_DIR}
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_definitions(companion_gatt_test PRIVATE FURBLE_NO_DISPLAY
                                                       FURBLE_HOST_COMPANION_TEST)
target_compile_options(companion_gatt_test PRIVATE -Wall -Wextra -Werror -Wno-format
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
               settings_nvs_stub.cpp
               ${FURBLE_ROOT}/src/FurbleConsole.cpp
               ${FURBLE_ROOT}/src/FurbleControl.cpp
               ${FURBLE_ROOT}/src/FurblePower.cpp
               ${FURBLE_ROOT}/src/FurbleProvision.cpp
               ${FURBLE_ROOT}/src/FurbleSettings.cpp
               ${FURBLE_ROOT}/lib/preferences/Preferences.cpp
               ${FURBLE_ROOT}/lib/furble/BtDebugJournal.cpp
               ${FURBLE_ROOT}/lib/furble/Camera.cpp
               ${FURBLE_ROOT}/lib/furble/Device.cpp
               ${FURBLE_ROOT}/lib/furble/Fujifilm.cpp
               ${FURBLE_ROOT}/lib/furble/FujifilmBasic.cpp
               ${FURBLE_PROTOCOL_DIR}/FujifilmProtocol.cpp
               ${FURBLE_PROTOCOL_DIR}/ProvisionTLV.cpp
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

if(BUILD_TESTING)
  add_test(NAME fujifilm-camera COMMAND fujifilm_camera_test)
  add_test(NAME golden-capture-replay
           COMMAND corpus_replay_test
                   ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/x100vi/synthetic.golden)
  add_test(NAME camera-lifecycle COMMAND lifecycle_test)
  add_test(NAME wrap-safe-time COMMAND wrap_safe_time_test)
  add_test(NAME time-keeper-policy COMMAND time_keeper_policy_test)
  add_test(NAME time-keeper-persistence COMMAND time_keeper_persistence_test)
  add_test(NAME camera-list-protocol COMMAND camera_list_protocol_test)
  add_test(NAME advertisement-dispatch COMMAND advertisement_dispatch_test)
  add_test(NAME camera-regression COMMAND camera_regression_test)
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
  add_test(NAME settings-nvs-roundtrip COMMAND settings_nvs_roundtrip_test)
  add_test(NAME auto-off-policy COMMAND auto_off_policy_test)
  add_test(NAME provision-tlv COMMAND provision_tlv_test)
  add_test(NAME provision-apply COMMAND provision_apply_test)
  add_test(NAME text-size-policy COMMAND text_size_policy_test)
  add_test(NAME text-size-policy-stickc COMMAND text_size_policy_stickc_test)
  add_test(NAME stale-connect COMMAND stale_connect_test)
  add_test(NAME client-leak COMMAND client_leak_test)
  add_test(NAME reconnect-stuck COMMAND reconnect_stuck_test)
  add_test(NAME gpx-writer COMMAND gpx_writer_test)
  add_test(NAME reconnect-backoff COMMAND reconnect_backoff_test)
  add_test(NAME battery-saver COMMAND battery_saver_test)
  add_test(NAME pmic-recovery COMMAND pmic_recovery_test)
  add_test(NAME sim-clock COMMAND sim_clock_test)
  add_test(NAME sim-action-parser COMMAND sim_action_parser_test)
  add_test(NAME sim-scheduler COMMAND sim_scheduler_test)
  set_tests_properties(sim-scheduler PROPERTIES TIMEOUT 10)
  add_test(NAME sim-fuzz-machine COMMAND sim_fuzz_machine_test)
  add_test(NAME sim-power-profiler COMMAND sim_power_profiler_test)
  # Run several independent profiler processes so ctest -j covers report-path
  # isolation. Each process must be able to create and clean up its own report.
  foreach(profiler_worker RANGE 1 4)
    add_test(NAME sim-power-profiler-concurrent-${profiler_worker}
             COMMAND sim_power_profiler_test)
  endforeach()
  add_test(NAME ethernet-transport COMMAND ethernet_test)
  add_test(NAME poe-power-model COMMAND poe_power_model_test)
  add_test(NAME ota-state-machine COMMAND ota_state_machine_test)
  add_test(NAME ota-mqtt-protocol COMMAND ota_mqtt_protocol_test)
  add_test(NAME ota-replay-store COMMAND ota_replay_store_test)
  add_test(NAME ota-partition-sink COMMAND ota_partition_sink_test)
  add_test(NAME gps-power-cycle COMMAND gps_power_cycle_test)
  add_test(NAME gps-power-profiler COMMAND gps_power_profiler_test)
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
  add_test(NAME control-reclaim-uaf COMMAND control_reclaim_uaf_test)
  add_test(NAME connect-mid-drop COMMAND connect_mid_drop_test)
  add_test(NAME ricoh-secure-timeout-uaf COMMAND ricoh_secure_timeout_uaf_test)
  add_test(NAME gatt-journal-uaf COMMAND gatt_journal_uaf_test)
  add_test(NAME companion-gatt COMMAND companion_gatt_test)

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
          fresh-connect
          dead-camera-disconnect-no-freeze
          connect-after-dead-disconnect
          stale-session-reconnect
          false-connected-guard
          transient-connect-recovers
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
endif()
