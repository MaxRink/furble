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

  # Known-failing findings. WILL_FAIL keeps CI green while the finding is tracked:
  # CTest inverts the pass/fail flag, so the crashing/failing run counts as a pass
  # today, and once FujifilmBasic::_connect is fixed the run turns clean and the
  # inverted flag turns the test red, forcing the marker off with the fix.
  add_test(NAME fujifilm-missing-shutter-service COMMAND fujifilm_missing_shutter_service_test)
  set_tests_properties(fujifilm-missing-shutter-service PROPERTIES WILL_FAIL TRUE)
  add_test(NAME fujifilm-missing-shutter-char COMMAND fujifilm_missing_shutter_char_test)
  set_tests_properties(fujifilm-missing-shutter-char PROPERTIES WILL_FAIL TRUE)
endif()
