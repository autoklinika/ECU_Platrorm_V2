cmake_minimum_required(VERSION 3.25)

project(
  ECUPlatformV2
  DESCRIPTION "ECU Platform V2"
  LANGUAGES CXX
)

option(ECU_BUILD_TESTS "Build ECU Platform V2 tests" ON)

add_subdirectory(src/core)

if(ECU_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
