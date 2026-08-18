cmake_minimum_required(VERSION 3.16)

project(furble_camera_protocol_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FURBLE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(camera_protocol_tests
  camera_protocol_tests.cpp
  "${FURBLE_ROOT}/lib/blowfish/Blowfish.cpp"
  "${FURBLE_ROOT}/lib/furble/protocol/CameraListProtocol.cpp"
  "${FURBLE_ROOT}/lib/furble/protocol/FujifilmProtocol.cpp"
)

target_include_directories(camera_protocol_tests PRIVATE
  "${FURBLE_ROOT}/lib/blowfish"
  "${FURBLE_ROOT}/lib/furble/protocol"
)

target_compile_options(camera_protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-sign-compare)

include(CTest)
add_test(NAME camera_protocol_tests COMMAND camera_protocol_tests)
