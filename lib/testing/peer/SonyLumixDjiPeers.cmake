# Sony, LUMIX, and DJI virtual camera peers.
#
# The three peers model the camera-server side of the Sony Camera Remote,
# Location, Pairing, and Remote Control services; the Panasonic LUMIX MEI0 and
# APS generations; and the DJI R SDK over FFF0. They subclass NimBLEMockPeer and
# are plain C++17 translation units with no radio dependency, so the host test
# harness and the simulator can link the same sources.
#
# Usage:
#
#   include(${FURBLE_ROOT}/lib/testing/peer/SonyLumixDjiPeers.cmake)
#   target_sources(my_target PRIVATE ${SONY_LUMIX_DJI_PEER_SOURCES})
#   target_include_directories(my_target PRIVATE ${SONY_LUMIX_DJI_PEER_INCLUDE_DIRS})
#
# The including target still owns MockNimBLE.cpp and any production sources it
# needs. This fragment only declares variables. It creates no target and changes
# no global settings.

include_guard(GLOBAL)

if(NOT DEFINED SONY_LUMIX_DJI_PEER_DIR)
  set(SONY_LUMIX_DJI_PEER_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

get_filename_component(_SONY_LUMIX_DJI_PEER_ROOT
                       "${SONY_LUMIX_DJI_PEER_DIR}/../../.." ABSOLUTE)

set(SONY_LUMIX_DJI_PEER_SOURCES
    "${SONY_LUMIX_DJI_PEER_DIR}/SonyVirtualCamera.cpp"
    "${SONY_LUMIX_DJI_PEER_DIR}/LumixVirtualCamera.cpp"
    "${SONY_LUMIX_DJI_PEER_DIR}/DjiVirtualCamera.cpp")

set(SONY_LUMIX_DJI_PEER_HEADERS
    "${SONY_LUMIX_DJI_PEER_DIR}/SonyVirtualCamera.h"
    "${SONY_LUMIX_DJI_PEER_DIR}/LumixVirtualCamera.h"
    "${SONY_LUMIX_DJI_PEER_DIR}/DjiVirtualCamera.h")

# Every peer includes MockNimBLE.h. The peer directory itself carries the
# public headers, and the nimble directory carries the mock transport.
set(SONY_LUMIX_DJI_PEER_INCLUDE_DIRS
    "${SONY_LUMIX_DJI_PEER_DIR}"
    "${_SONY_LUMIX_DJI_PEER_ROOT}/lib/testing/nimble")
