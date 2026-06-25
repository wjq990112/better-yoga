# better-yoga layout microbenchmark.
#
# Builds against an arbitrary Yoga source tree via -DYOGA_ROOT=<path>, so the
# same source links either the fork or the pinned baseline yogacore. Defaults to
# the fork tree two levels up.

cmake_minimum_required(VERSION 3.13...3.26)
project(microbench)

if(NOT DEFINED YOGA_ROOT)
  set(YOGA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()

include(${YOGA_ROOT}/cmake/project-defaults.cmake)

add_subdirectory(${YOGA_ROOT}/yoga ${CMAKE_CURRENT_BINARY_DIR}/yoga)

add_executable(microbench ${CMAKE_CURRENT_SOURCE_DIR}/Microbench.cpp)
target_link_libraries(microbench yogacore)
target_include_directories(microbench PRIVATE ${YOGA_ROOT})
