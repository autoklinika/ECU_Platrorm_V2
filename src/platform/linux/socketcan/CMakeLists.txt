add_library(ecu_platform_linux_socketcan STATIC
  src/socketcan_adapter.cpp
  src/socketcan_codec.cpp
  src/socketcan_link_info.cpp
)

add_library(ECU::platform_linux_socketcan ALIAS ecu_platform_linux_socketcan)

target_include_directories(ecu_platform_linux_socketcan
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ecu_platform_linux_socketcan
  PUBLIC
    ECU::core
)

target_compile_features(ecu_platform_linux_socketcan PUBLIC cxx_std_17)

set_target_properties(ecu_platform_linux_socketcan PROPERTIES
  CXX_EXTENSIONS OFF
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    ecu_platform_linux_socketcan
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()
