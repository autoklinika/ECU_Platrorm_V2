cmake_minimum_required(VERSION 3.25)

project(
  ECUPlatformV2
  DESCRIPTION "ECU Platform V2"
  LANGUAGES CXX
)

option(ECU_BUILD_TESTS "Build ECU Platform V2 tests" ON)
option(ECU_BUILD_LEGACY_CORE "Build legacy Core" ON)
option(ECU_BUILD_CORE_V2 "Build rebuilt TRUCK/AGRI/OHV Core V2" ON)
option(ECU_BUILD_BENCH_RUNTIME "Build DUT-neutral Bench Runtime" ON)
option(ECU_BUILD_DUT_PROFILE "Build Stage 3 DUT Profile foundation" OFF)
option(ECU_BUILD_DAF_SAC_PROFILE "Build Stage 3.3 DAF SAC proof profile" OFF)
option(ECU_BUILD_SAC_MODULE "Build SAC ECU module" ON)
option(
  ECU_BUILD_LINUX_SOCKETCAN
  "Build Linux SocketCAN platform adapter"
  ON
)

if(ECU_BUILD_LEGACY_CORE)
  add_subdirectory(src/core)
endif()

if(
  (
    ECU_BUILD_SAC_MODULE
    OR (
      ECU_BUILD_LINUX_SOCKETCAN
      AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
    )
  )
  AND NOT TARGET ECU::core
)
  message(FATAL_ERROR
    "Enabled legacy-dependent adapters require ECU::core; enable ECU_BUILD_LEGACY_CORE or disable the applicable adapters")
endif()

if(ECU_BUILD_CORE_V2)
  add_subdirectory(src/core_v2)
endif()

if(ECU_BUILD_BENCH_RUNTIME)
  if(NOT TARGET ECU::core_v2)
    message(FATAL_ERROR
      "Bench Runtime requires ECU::core_v2; enable ECU_BUILD_CORE_V2 or disable ECU_BUILD_BENCH_RUNTIME")
  endif()
  add_subdirectory(src/bench)
endif()

if(ECU_BUILD_DUT_PROFILE)
  if(NOT TARGET ECU::bench)
    message(FATAL_ERROR
      "DUT Profile foundation requires ECU::bench; enable ECU_BUILD_BENCH_RUNTIME or disable ECU_BUILD_DUT_PROFILE")
  endif()
  add_subdirectory(src/dut_profile)
endif()

if(ECU_BUILD_DAF_SAC_PROFILE)
  if(NOT TARGET ECU::dut_profile)
    message(FATAL_ERROR
      "DAF SAC proof profile requires ECU::dut_profile; enable ECU_BUILD_DUT_PROFILE or disable ECU_BUILD_DAF_SAC_PROFILE")
  endif()
  add_subdirectory(src/dut_profiles/daf_sac)
endif()

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
