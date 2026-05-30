cmake_minimum_required(VERSION 3.16)

# Pull in the hivekit shared component from the repo root
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hivekit-pir-c6)
