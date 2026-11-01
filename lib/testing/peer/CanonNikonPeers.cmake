# Canon and Nikon virtual camera peers for the host harness.
#
# The four peers model the Canon Smart and Remote roles and the Nikon Smart and
# Remote roles on top of NimBLEMockPeer. They are plain C++17 translation units
# with no radio dependency, so the host test harness and the simulator link the
# same sources. They are not part of the production firmware build.
#
# Usage:
#   include(${HOST_PEER_DIR}/CanonNikonPeers.cmake)
#   target_sources(furble_host_camera PRIVATE
#                  ${CANON_NIKON_PEER_SOURCES}
#                  ${CANON_NIKON_PEER_DEPENDENCY_SOURCES})
#   target_include_directories(furble_host_camera
#                              PRIVATE ${CANON_NIKON_PEER_INCLUDE_DIRS})
#
# The including target still owns MockNimBLE.cpp and any production sources it
# needs. CANON_NIKON_PEER_DEPENDENCY_SOURCES carries the one shared library the
# peers use directly: the Nikon Smart peer computes the LsSec Blowfish CBC-MAC,
# so it compiles lib/blowfish/Blowfish.cpp.
#
# This fragment only declares variables. It creates no target and changes no
# global settings, so a target that includes it opts in to each list
# explicitly.

include_guard(GLOBAL)

set(CANON_NIKON_PEER_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The peers live at <root>/lib/testing/peer, so the repository root is three
# levels up. Only Blowfish needs it; the mock header is a sibling directory.
get_filename_component(_CANON_NIKON_PEER_ROOT
                       "${CANON_NIKON_PEER_DIR}/../../.." ABSOLUTE)

set(CANON_NIKON_PEER_HEADERS
    "${CANON_NIKON_PEER_DIR}/CanonSmartVirtualCamera.h"
    "${CANON_NIKON_PEER_DIR}/CanonRemoteVirtualCamera.h"
    "${CANON_NIKON_PEER_DIR}/NikonRemoteVirtualCamera.h"
    "${CANON_NIKON_PEER_DIR}/NikonSmartVirtualCamera.h")

set(CANON_NIKON_PEER_SOURCES
    "${CANON_NIKON_PEER_DIR}/CanonSmartVirtualCamera.cpp"
    "${CANON_NIKON_PEER_DIR}/CanonRemoteVirtualCamera.cpp"
    "${CANON_NIKON_PEER_DIR}/NikonRemoteVirtualCamera.cpp"
    "${CANON_NIKON_PEER_DIR}/NikonSmartVirtualCamera.cpp")

# MockNimBLE.h is included by every peer, and Blowfish.h is included by the
# Nikon Smart peer. Both are listed here so an including target does not have to
# repeat them.
set(CANON_NIKON_PEER_INCLUDE_DIRS
    "${CANON_NIKON_PEER_DIR}"
    "${CANON_NIKON_PEER_DIR}/../nimble"
    "${_CANON_NIKON_PEER_ROOT}/lib/blowfish")

set(CANON_NIKON_PEER_DEPENDENCY_SOURCES
    "${_CANON_NIKON_PEER_ROOT}/lib/blowfish/Blowfish.cpp")
