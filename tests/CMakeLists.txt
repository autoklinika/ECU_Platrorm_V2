add_executable(ecu_core_smoke
  core_smoke.cpp
)

target_link_libraries(ecu_core_smoke PRIVATE ECU::core)

set_target_properties(ecu_core_smoke PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(ecu_core_smoke PRIVATE /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ecu_core_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME ecu.core.smoke COMMAND ecu_core_smoke)
