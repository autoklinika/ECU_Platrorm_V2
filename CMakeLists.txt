cmake_minimum_required(VERSION 3.25)

project(
  ECUPlatformV2
  DESCRIPTION "ECU Platform V2"
  LANGUAGES CXX
)

option(ECU_BUILD_TESTS "Build ECU Platform V2 tests" ON)

add_subdirectory(src/core)
add_subdirectory(src/ecu/sac)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(src/platform/linux/socketcan)
endif()

if(ECU_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
