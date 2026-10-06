add_library(ecu_sac STATIC
  src/sac_identification.cpp
  src/sac_runtime.cpp
)

add_library(ECU::sac ALIAS ecu_sac)

target_include_directories(ecu_sac
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ecu_sac
  PUBLIC
    ECU::core
)

target_compile_features(ecu_sac PUBLIC cxx_std_17)

set_target_properties(ecu_sac PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(ecu_sac PRIVATE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    ecu_sac
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()
