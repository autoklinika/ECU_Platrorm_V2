add_library(ecu_application_api STATIC
  src/router.cpp
  src/projections.cpp
)
add_library(ECU::application_api ALIAS ecu_application_api)
target_include_directories(ecu_application_api PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(ecu_application_api PUBLIC ECU::bench ECU::dut_profile)
target_compile_features(ecu_application_api PUBLIC cxx_std_17)
set_target_properties(ecu_application_api PROPERTIES CXX_EXTENSIONS OFF)

execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE ECU_API_BUILD_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT ECU_API_BUILD_SHA MATCHES "^[0-9a-fA-F]+$")
  set(ECU_API_BUILD_SHA "unknown")
endif()
target_compile_definitions(ecu_application_api PRIVATE
  ECU_API_BUILD_REVISION="${ECU_API_BUILD_SHA}")

if(WIN32 OR UNIX)
  add_executable(ecu_api_http
  src/http_transport.cpp
  src/main.cpp
)
target_link_libraries(ecu_api_http PRIVATE ECU::application_api)
target_compile_features(ecu_api_http PRIVATE cxx_std_17)
set_target_properties(ecu_api_http PROPERTIES CXX_EXTENSIONS OFF)

if(WIN32)
  target_link_libraries(ecu_api_http PRIVATE ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET ECU::platform_linux_v2)
  target_sources(ecu_api_http PRIVATE src/linux_read_model.cpp)
  target_link_libraries(ecu_api_http PRIVATE ECU::platform_linux_v2)
  target_compile_definitions(ecu_api_http PRIVATE ECU_API_HAVE_LINUX_LINK=1)
endif()
endif() # HTTP sockets only on supported host OSes.

set(_ecu_api_targets ecu_application_api)
if(TARGET ecu_api_http)
  list(APPEND _ecu_api_targets ecu_api_http)
endif()

# Optional DUT-specific projections are separate from generic API contracts.
# This library only converts completed application snapshots; no physical I/O.
if(TARGET ECU::app_daf_sac)
  add_library(ecu_api_daf_sac_projection STATIC src/daf_sac_projection.cpp)
  add_library(ECU::api_daf_sac_projection ALIAS ecu_api_daf_sac_projection)
  target_include_directories(ecu_api_daf_sac_projection PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  target_link_libraries(ecu_api_daf_sac_projection
    PUBLIC ECU::application_api ECU::app_daf_sac)
  target_compile_features(ecu_api_daf_sac_projection PUBLIC cxx_std_17)
  set_target_properties(ecu_api_daf_sac_projection PROPERTIES CXX_EXTENSIONS OFF)
  list(APPEND _ecu_api_targets ecu_api_daf_sac_projection)
endif()

foreach(target IN LISTS _ecu_api_targets)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()
