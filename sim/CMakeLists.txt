cmake_minimum_required(VERSION 3.20)

project(furble_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FURBLE_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# Keep the simulator's control seam ABI-checked against the firmware header.
# This catches a stale one-argument simulator definition before the optional
# graphics dependencies are configured.
file(READ "${CMAKE_CURRENT_LIST_DIR}/FurbleControlSim.cpp" _sim_control_source)
if(NOT _sim_control_source MATCHES
   "void[ ]+Control::connectAll\\(bool[ ]+infiniteReconnect,[ ]*reconnect_origin_t[ ]+origin\\)")
  message(FATAL_ERROR "FurbleControlSim::connectAll is out of sync with FurbleControl.h")
endif()
set(FURBLE_DEP_ROOT
    "${FURBLE_ROOT}/.pio/libdeps/m5stick-s3"
    CACHE PATH "PlatformIO library cache used by the simulator")
set(FURBLE_LVGL_DIR "" CACHE PATH "LVGL 9 source directory")

set(M5GFX_DIR "${FURBLE_DEP_ROOT}/M5GFX")
set(M5UNIFIED_DIR "${FURBLE_DEP_ROOT}/M5Unified")

if(NOT EXISTS "${M5GFX_DIR}/src/M5GFX.cpp")
  message(FATAL_ERROR "M5GFX was not found at ${M5GFX_DIR}")
endif()

if(NOT EXISTS "${M5UNIFIED_DIR}/src/M5Unified.cpp")
  message(FATAL_ERROR "M5Unified was not found at ${M5UNIFIED_DIR}")
endif()

if(NOT EXISTS "${FURBLE_DEP_ROOT}/TinyGPSPlus/src/TinyGPS++.cpp")
  message(FATAL_ERROR
          "TinyGPSPlus was not found at ${FURBLE_DEP_ROOT}/TinyGPSPlus")
endif()

# The managed component tracks the LVGL version pinned by src/idf_component.yml.
if(NOT FURBLE_LVGL_DIR)
  set(_lvgl_candidate "${FURBLE_ROOT}/managed_components/lvgl__lvgl")
  if(EXISTS "${_lvgl_candidate}/CMakeLists.txt")
    set(FURBLE_LVGL_DIR "${_lvgl_candidate}")
  endif()
endif()

if(NOT FURBLE_LVGL_DIR OR NOT EXISTS "${FURBLE_LVGL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
          "LVGL 9 was not found. Set -DFURBLE_LVGL_DIR=/path/to/lvgl__lvgl")
endif()

list(APPEND CMAKE_PREFIX_PATH
     "/opt/homebrew"
     "/opt/homebrew/opt/sdl2-compat"
     "/usr/local")
find_package(SDL2 CONFIG QUIET)

if(NOT TARGET SDL2::SDL2)
  add_library(furble_sdl INTERFACE)
  target_include_directories(
    furble_sdl
    INTERFACE
      "/opt/homebrew/include/SDL2"
      "/usr/local/include/SDL2")
  target_link_directories(furble_sdl INTERFACE "/opt/homebrew/lib"
                          "/usr/local/lib")
  target_link_libraries(furble_sdl INTERFACE SDL2)
  add_library(SDL2::SDL2 ALIAS furble_sdl)
endif()

add_subdirectory("${FURBLE_LVGL_DIR}" "lvgl-build" EXCLUDE_FROM_ALL)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE
                                       LV_KCONFIG_IGNORE)
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

file(GLOB SIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB ICON_SOURCES CONFIGURE_DEPENDS "${FURBLE_ROOT}/components/icons/*.c")

# Keep this firmware source list in sync with sim/build.sh.
set(FURBLE_SOURCES
    ${SIM_SOURCES}
    "${FURBLE_ROOT}/src/FurbleBootScreen.cpp"
    "${FURBLE_ROOT}/src/FurbleCalibrate.cpp"
    "${FURBLE_ROOT}/src/FurbleCompanionService.cpp"
    "${FURBLE_ROOT}/src/FurbleGPS.cpp"
    "${FURBLE_ROOT}/src/FurbleOTAMQTT.cpp"
    "${FURBLE_ROOT}/src/FurbleOTAPartitionSink.cpp"
    "${FURBLE_ROOT}/src/FurbleOTAReplayStore.cpp"
    "${FURBLE_ROOT}/src/FurblePower.cpp"
    "${FURBLE_ROOT}/src/FurbleProvision.cpp"
    "${FURBLE_ROOT}/src/FurbleSettings.cpp"
    "${FURBLE_ROOT}/src/FurbleSpinValue.cpp"
    "${FURBLE_ROOT}/src/FurbleTimeKeeper.cpp"
    "${FURBLE_ROOT}/src/FurbleTimeKeeperPolicy.cpp"
    "${FURBLE_ROOT}/src/FurbleUI.cpp"
    "${FURBLE_ROOT}/src/FurbleUIBulb.cpp"
    "${FURBLE_ROOT}/src/FurbleUIIntervalometer.cpp"
    "${FURBLE_ROOT}/lib/furble/protocol/ProvisionTLV.cpp"
    "${FURBLE_DEP_ROOT}/TinyGPSPlus/src/TinyGPS++.cpp"
    ${ICON_SOURCES})

file(GLOB M5GFX_SOURCES CONFIGURE_DEPENDS
     "${M5GFX_DIR}/src/*.cpp"
     "${M5GFX_DIR}/src/lgfx/Fonts/efont/*.c"
     "${M5GFX_DIR}/src/lgfx/Fonts/IPA/*.c"
     "${M5GFX_DIR}/src/lgfx/utility/*.c"
     "${M5GFX_DIR}/src/lgfx/v1/*.cpp"
     "${M5GFX_DIR}/src/lgfx/v1/misc/*.cpp"
     "${M5GFX_DIR}/src/lgfx/v1/panel/*.cpp"
     "${M5GFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp"
     "${M5GFX_DIR}/src/lgfx/v1/touch/*.cpp")
file(GLOB_RECURSE M5UNIFIED_SOURCES CONFIGURE_DEPENDS
     "${M5UNIFIED_DIR}/src/*.cpp")

add_executable(furble-sim ${FURBLE_SOURCES} ${M5GFX_SOURCES}
                           ${M5UNIFIED_SOURCES})

target_include_directories(
  furble-sim
  PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/shim"
    "${FURBLE_ROOT}/include"
    "${FURBLE_ROOT}/components/icons"
    "${FURBLE_ROOT}/lib/preferences"
    "${FURBLE_ROOT}/lib/furble"
    "${FURBLE_DEP_ROOT}/TinyGPSPlus/src"
    "${M5GFX_DIR}/src"
    "${M5UNIFIED_DIR}/src"
    "${FURBLE_LVGL_DIR}"
    "${FURBLE_LVGL_DIR}/..")

target_compile_definitions(
  furble-sim
  PRIVATE
    FURBLE_M5STICKS3
    FURBLE_RIG
    FURBLE_SIM
    FURBLE_VERSION=\"sim\"
    FURBLE_TEST_VERSION=1
    FURBLE_BATTERY_DEBUG=0
    M5GFX_SCALE=2
    M5GFX_BOARD=board_M5StickS3
    LV_CONF_INCLUDE_SIMPLE
    LV_LVGL_H_INCLUDE_SIMPLE
    LV_KCONFIG_IGNORE
    _THREAD_SAFE)

target_compile_options(
  furble-sim
  PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    "-include${CMAKE_CURRENT_LIST_DIR}/shim/esp_log.h"
    "-include${CMAKE_CURRENT_LIST_DIR}/shim/esp_system.h"
    "-include${CMAKE_CURRENT_LIST_DIR}/shim/esp_heap_caps.h")
target_link_libraries(furble-sim PRIVATE lvgl SDL2::SDL2)

if(APPLE)
  target_link_libraries(furble-sim PRIVATE "-framework Cocoa")
endif()
