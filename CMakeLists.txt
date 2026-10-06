cmake_minimum_required(VERSION 3.25)

project(
  ECUPlatformV2
  DESCRIPTION "ECU Platform V2"
  LANGUAGES CXX
)

option(ECU_BUILD_TESTS "Build ECU Platform V2 tests" ON)
option(ECU_BUILD_SAC_MODULE "Build SAC ECU module" ON)
option(
  ECU_BUILD_LINUX_SOCKETCAN
  "Build Linux SocketCAN platform adapter"
  ON
)

add_subdirectory(src/core)

if(ECU_BUILD_SAC_MODULE)
  add_subdirectory(src/ecu/sac)
endif()

if(
  ECU_BUILD_LINUX_SOCKETCAN
  AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
)
  add_subdirectory(src/platform/linux/socketcan)
endif()

if(ECU_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
