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

set(FURBLE_PROTOCOL_DIR ${FURBLE_ROOT}/lib/furble/protocol)

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

# Adversarial bug-hunt findings. Both are KNOWN-FAILING against current master and
# are registered WILL_FAIL below so CI stays green while the finding is tracked.
# When FujifilmBasic::_connect gains the missing shutter guard, each test passes,
# and WILL_FAIL flips the job red so the marker gets removed with the fix.
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
                                  ${CONTROL_E2E_DIR}/doubles
                                  ${CONTROL_E2E_DIR}
                                  ${FURBLE_ROOT}/include
                                  ${FURBLE_ROOT}/lib/furble
                                  ${HOST_NIMBLE_DIR}
                                  ${HOST_PEER_DIR})
target_compile_options(control_e2e_test PRIVATE -Wall -Wextra -Wno-format -Wno-unused-parameter
                       -include ${CONTROL_E2E_DIR}/freertos/FreeRTOS.h)
target_link_libraries(control_e2e_test PRIVATE furble_host_camera)

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

if(BUILD_TESTING)
  add_test(NAME fujifilm-camera COMMAND fujifilm_camera_test)
  add_test(NAME golden-capture-replay
           COMMAND corpus_replay_test
                   ${CMAKE_CURRENT_SOURCE_DIR}/../corpus/x100vi/synthetic.golden)
  add_test(NAME camera-lifecycle COMMAND lifecycle_test)
  add_test(NAME camera-list-protocol COMMAND camera_list_protocol_test)
  add_test(NAME fujifilm-protocol COMMAND fujifilm_protocol_test)
  add_test(NAME settings-table COMMAND settings_table_test ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  add_test(NAME stale-connect COMMAND stale_connect_test)
  add_test(NAME client-leak COMMAND client_leak_test)
  add_test(NAME reconnect-stuck COMMAND reconnect_stuck_test)
  add_test(NAME gpx-writer COMMAND gpx_writer_test)
  add_test(NAME reconnect-backoff COMMAND reconnect_backoff_test)
  add_test(NAME fault-tooling COMMAND fault_tooling_test)
  add_test(NAME control-disconnect COMMAND control_disconnect_test)
  add_test(NAME control-reclaim-uaf COMMAND control_reclaim_uaf_test)

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
          client-pool-exhaustion)
    add_test(NAME control-e2e-${scenario} COMMAND control_e2e_test ${scenario})
  endforeach()

  # Known-failing findings. WILL_FAIL keeps CI green while the finding is tracked:
  # CTest inverts the pass/fail flag, so the crashing/failing run counts as a pass
  # today, and once FujifilmBasic::_connect is fixed the run turns clean and the
  # inverted flag turns the test red, forcing the marker off with the fix.
  add_test(NAME fujifilm-missing-shutter-service COMMAND fujifilm_missing_shutter_service_test)
  set_tests_properties(fujifilm-missing-shutter-service PROPERTIES WILL_FAIL TRUE)
  add_test(NAME fujifilm-missing-shutter-char COMMAND fujifilm_missing_shutter_char_test)
  set_tests_properties(fujifilm-missing-shutter-char PROPERTIES WILL_FAIL TRUE)
endif()
