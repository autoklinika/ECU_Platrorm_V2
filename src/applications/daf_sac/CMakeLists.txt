# First concrete ECU application: no legacy Core, no hardware or GUI dependency.
add_library(ecu_app_daf_sac STATIC
  src/application.cpp
)
add_library(ECU::app_daf_sac ALIAS ecu_app_daf_sac)

target_include_directories(ecu_app_daf_sac PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(ecu_app_daf_sac
  PUBLIC ECU::bench ECU::dut_profile ECU::daf_sac_profile ECU::core_v2
)
target_compile_features(ecu_app_daf_sac PUBLIC cxx_std_17)
set_target_properties(ecu_app_daf_sac PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(ecu_app_daf_sac PRIVATE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ecu_app_daf_sac PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
