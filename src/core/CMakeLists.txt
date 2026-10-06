add_library(ecu_core STATIC
  src/build_info.cpp
)

add_library(ECU::core ALIAS ecu_core)

target_include_directories(ecu_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Stage E bootstrap minimum only. This is not a frozen product-language decision.
target_compile_features(ecu_core PUBLIC cxx_std_17)

set_target_properties(ecu_core PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(ecu_core PRIVATE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ecu_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
