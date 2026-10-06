add_library(ecu_core STATIC
  src/build_info.cpp
  src/can_types.cpp
  src/lifecycle.cpp
  src/resource_manager.cpp
  src/j1939_identifier.cpp
  src/isotp_types.cpp
  src/isotp_endpoint.cpp
  src/isotp_diagnostic_transport.cpp
  src/uds_types.cpp
  src/uds_services.cpp
  src/uds_client.cpp
)

add_library(ECU::core ALIAS ecu_core)

target_include_directories(ecu_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Bootstrap minimum only. The final product language level remains a project decision.
target_compile_features(ecu_core PUBLIC cxx_std_17)

set_target_properties(ecu_core PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(ecu_core PRIVATE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ecu_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
