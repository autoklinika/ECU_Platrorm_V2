add_library(ecu_core_v2 STATIC
  src/can_types.cpp
  src/can_bus_runtime.cpp
  src/deadline_watchdog.cpp
)

add_library(ECU::core_v2 ALIAS ecu_core_v2)

target_include_directories(ecu_core_v2
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(ecu_core_v2 PUBLIC cxx_std_17)

set_target_properties(ecu_core_v2 PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(
    ecu_core_v2
    PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /GR-
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    ecu_core_v2
    PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Werror
      -fno-exceptions
      -fno-rtti
  )
endif()
