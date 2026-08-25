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
            ${FURBLE_ROOT}/lib/furble/Camera.cpp
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

# Exercise the real NVS/Preferences settings path against a typed in-memory
# NVS stub. The test also includes the production SD settings helpers so every
# table row is checked through both the firmware store and the SD import/export
# representation. M5STICKS3 keeps the conditional watchdog row in the host
# table, so a new or conditional setting cannot be omitted silently.
set(SETTINGS_TEST_STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/settings_stubs)
add_executable(settings_nvs_roundtrip_test
               settings_nvs_roundtrip_test.cpp
               settings_nvs_stub.cpp
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

add_executable(sim_power_profiler_test
               sim_power_profiler_test.cpp
               ${FURBLE_ROOT}/sim/clock.cpp
               ${FURBLE_ROOT}/sim/power_profiler.cpp)
target_include_directories(sim_power_profiler_test PRIVATE ${FURBLE_ROOT}/include
                                                           ${FURBLE_ROOT}/sim)
target_compile_options(sim_power_profiler_test PRIVATE -Wall -Wextra -Werror -Wno-format
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
target_compile_options(control_e2e_test PRIVATE -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_e2e_test PRIVATE furble_host_camera)

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
# It cannot link the shared furble_host_camera library, because the late
# onDisconnect writes through the freed Camera inside lib/furble/Camera.cpp, so
# that translation unit must be compiled with ASan for the access to be
# instrumented. This target therefore compiles the furble sources directly with
# -fsanitize=address. The mock models NimBLE's deferred deleteClient on a still
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
               ${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp)
target_include_directories(connect_mid_drop_test BEFORE PRIVATE
                           ${FURBLE_ROOT}/include
                           ${HOST_NIMBLE_DIR}
                           ${HOST_PEER_DIR}
                           ${FURBLE_ROOT}/lib/furble)
target_compile_options(connect_mid_drop_test PRIVATE
                       -fsanitize=address -g -O1
                       -Wall -Wextra -Werror -Wno-format -Wno-unused-parameter)
target_link_options(connect_mid_drop_test PRIVATE -fsanitize=address)
target_link_libraries(connect_mid_drop_test PRIVATE Threads::Threads)

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

if(BUILD_TESTING)
  add_test(NAME fujifilm-camera COMMAND fujifilm_camera_test)
  add_test(NAME golden-capture-replay
           COMMAND corpus_replay_test
                   ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/x100vi/synthetic.golden)
  add_test(NAME camera-lifecycle COMMAND lifecycle_test)
  add_test(NAME wrap-safe-time COMMAND wrap_safe_time_test)
  add_test(NAME camera-list-protocol COMMAND camera_list_protocol_test)
  add_test(NAME advertisement-dispatch COMMAND advertisement_dispatch_test)

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
  add_test(NAME settings-nvs-roundtrip COMMAND settings_nvs_roundtrip_test)
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
  add_test(NAME sim-clock COMMAND sim_clock_test)
  add_test(NAME sim-power-profiler COMMAND sim_power_profiler_test)
  add_test(NAME ota-state-machine COMMAND ota_state_machine_test)
  add_test(NAME ota-mqtt-protocol COMMAND ota_mqtt_protocol_test)
  add_test(NAME fault-tooling COMMAND fault_tooling_test)
  add_test(NAME control-disconnect COMMAND control_disconnect_test)
  add_test(NAME control-reclaim-uaf COMMAND control_reclaim_uaf_test)
  add_test(NAME connect-mid-drop COMMAND connect_mid_drop_test)
  add_test(NAME companion-gatt COMMAND companion_gatt_test)

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
          reconnect-shutter-drop)
    add_test(NAME control-e2e-${scenario} COMMAND control_e2e_test ${scenario})
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
