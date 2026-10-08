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

foreach(target ecu_application_api ecu_api_http)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()
