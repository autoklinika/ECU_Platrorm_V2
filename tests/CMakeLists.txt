add_executable(ecu_core_smoke
  core_smoke.cpp
)

target_link_libraries(ecu_core_smoke PRIVATE ECU::core)

set_target_properties(ecu_core_smoke PROPERTIES
  CXX_EXTENSIONS OFF
)

add_executable(ecu_can_contract_tests
  can_contract_tests.cpp
)

target_link_libraries(ecu_can_contract_tests PRIVATE ECU::core)

set_target_properties(ecu_can_contract_tests PROPERTIES
  CXX_EXTENSIONS OFF
)

foreach(target ecu_core_smoke ecu_can_contract_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

add_test(NAME ecu.core.smoke COMMAND ecu_core_smoke)
add_test(NAME ecu.contract.can COMMAND ecu_can_contract_tests)

if(TARGET ECU::platform_linux_socketcan)
  add_executable(ecu_socketcan_codec_tests
    socketcan_codec_tests.cpp
  )
  target_link_libraries(
    ecu_socketcan_codec_tests
    PRIVATE ECU::platform_linux_socketcan
  )
  set_target_properties(
    ecu_socketcan_codec_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )

  add_executable(ecu_socketcan_link_probe
    socketcan_link_probe.cpp
  )
  target_link_libraries(
    ecu_socketcan_link_probe
    PRIVATE ECU::platform_linux_socketcan
  )
  set_target_properties(
    ecu_socketcan_link_probe
    PROPERTIES CXX_EXTENSIONS OFF
  )

  add_executable(ecu_socketcan_adapter_probe
    socketcan_adapter_probe.cpp
  )
  target_link_libraries(
    ecu_socketcan_adapter_probe
    PRIVATE ECU::platform_linux_socketcan
  )
  set_target_properties(
    ecu_socketcan_adapter_probe
    PROPERTIES CXX_EXTENSIONS OFF
  )

  foreach(
      target
      ecu_socketcan_codec_tests
      ecu_socketcan_link_probe
      ecu_socketcan_adapter_probe
  )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(
        ${target}
        PRIVATE -Wall -Wextra -Wpedantic -Werror
      )
    endif()
  endforeach()

  add_test(
    NAME ecu.platform.linux.socketcan.codec
    COMMAND ecu_socketcan_codec_tests
  )
endif()
