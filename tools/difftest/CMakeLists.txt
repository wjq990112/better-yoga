# better-yoga differential layout harness — builds against an arbitrary Yoga
# source tree via -DYOGA_ROOT so the same source links fork or baseline.

cmake_minimum_required(VERSION 3.13...3.26)
project(difftest)

if(NOT DEFINED YOGA_ROOT)
  set(YOGA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()

include(${YOGA_ROOT}/cmake/project-defaults.cmake)
add_subdirectory(${YOGA_ROOT}/yoga ${CMAKE_CURRENT_BINARY_DIR}/yoga)

add_executable(difftest ${CMAKE_CURRENT_SOURCE_DIR}/DiffTest.cpp)
target_link_libraries(difftest yogacore)
target_include_directories(difftest PRIVATE ${YOGA_ROOT})
