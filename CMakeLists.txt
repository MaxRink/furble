cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(FURBLE_NO_DISPLAY)
  # ESP-IDF expands component requirements in a separate CMake process. Export
  # the profile there as well so the early pass sees the same source gate.
  idf_build_set_property(FURBLE_NO_DISPLAY "${FURBLE_NO_DISPLAY}")
endif()
project(furble)
