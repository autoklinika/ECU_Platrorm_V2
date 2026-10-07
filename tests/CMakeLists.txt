function(ecu_strict_test target)
  target_link_libraries(${target} PRIVATE ECU::core)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ${target}
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
endfunction()

if(TARGET ECU::core_v2)
  add_executable(
    ecu_core_v2_foundation_tests
    core_v2_foundation_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_foundation_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_foundation_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_foundation_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_foundation_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.foundation
    COMMAND ecu_core_v2_foundation_tests
  )

  add_executable(
    ecu_core_v2_dut_neutral_shared_bus_tests
    core_v2_dut_neutral_shared_bus_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_dut_neutral_shared_bus_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_dut_neutral_shared_bus_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_dut_neutral_shared_bus_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_dut_neutral_shared_bus_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.dut_neutral.shared_bus
    COMMAND ecu_core_v2_dut_neutral_shared_bus_tests
  )

  add_executable(
    ecu_core_v2_runtime_foundation_tests
    core_v2_runtime_foundation_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_runtime_foundation_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_runtime_foundation_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_runtime_foundation_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_runtime_foundation_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.runtime.foundation
    COMMAND ecu_core_v2_runtime_foundation_tests
  )

  add_executable(
    ecu_core_v2_cyclic_can_actuator_runtime_tests
    core_v2_cyclic_can_actuator_runtime_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_cyclic_can_actuator_runtime_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_cyclic_can_actuator_runtime_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_cyclic_can_actuator_runtime_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_cyclic_can_actuator_runtime_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.actuation.cyclic_can
    COMMAND ecu_core_v2_cyclic_can_actuator_runtime_tests
  )

  add_executable(
    ecu_core_v2_j1939_network_tests
    core_v2_j1939_network_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_network_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_network_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_network_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_network_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.network
    COMMAND ecu_core_v2_j1939_network_tests
  )

  add_executable(
    ecu_core_v2_j1939_fd_cpg_tests
    core_v2_j1939_fd_cpg_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_fd_cpg_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_fd_cpg_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_fd_cpg_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_fd_cpg_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.fd_cpg
    COMMAND ecu_core_v2_j1939_fd_cpg_tests
  )

  add_executable(
    ecu_core_v2_j1939_fd_tp_tests
    core_v2_j1939_fd_tp_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_fd_tp_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_fd_tp_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_fd_tp_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_fd_tp_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.fd_tp
    COMMAND ecu_core_v2_j1939_fd_tp_tests
  )

  add_executable(
    ecu_core_v2_j1939_acknowledgment_tests
    core_v2_j1939_acknowledgment_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_acknowledgment_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_acknowledgment_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_acknowledgment_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_acknowledgment_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.acknowledgment
    COMMAND ecu_core_v2_j1939_acknowledgment_tests
  )

  add_executable(
    ecu_core_v2_j1939_request_tests
    core_v2_j1939_request_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_request_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_request_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_request_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_request_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.request
    COMMAND ecu_core_v2_j1939_request_tests
  )

  add_executable(
    ecu_core_v2_j1939_diagnostics_tests
    core_v2_j1939_diagnostics_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_diagnostics_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_diagnostics_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_diagnostics_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_diagnostics_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.diagnostics
    COMMAND ecu_core_v2_j1939_diagnostics_tests
  )

  add_executable(
    ecu_core_v2_j1939_tp_tests
    core_v2_j1939_tp_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_tp_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_tp_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_tp_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_tp_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.tp
    COMMAND ecu_core_v2_j1939_tp_tests
  )

  add_executable(
    ecu_core_v2_j1939_tp_tx_tests
    core_v2_j1939_tp_tx_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_tp_tx_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_tp_tx_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_tp_tx_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_tp_tx_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.tp_tx
    COMMAND ecu_core_v2_j1939_tp_tx_tests
  )

  add_executable(
    ecu_core_v2_j1939_tp_e2e_tests
    core_v2_j1939_tp_e2e_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_j1939_tp_e2e_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_j1939_tp_e2e_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_j1939_tp_e2e_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_j1939_tp_e2e_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.j1939.tp_e2e
    COMMAND ecu_core_v2_j1939_tp_e2e_tests
  )

  add_executable(
    ecu_core_v2_isobus_etp_tests
    core_v2_isobus_etp_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_isobus_etp_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_isobus_etp_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_isobus_etp_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_isobus_etp_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.isobus.etp
    COMMAND ecu_core_v2_isobus_etp_tests
  )

  add_executable(
    ecu_core_v2_isobus_working_set_tests
    core_v2_isobus_working_set_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_isobus_working_set_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_isobus_working_set_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_isobus_working_set_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_isobus_working_set_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.isobus.working_set
    COMMAND ecu_core_v2_isobus_working_set_tests
  )

  add_executable(
    ecu_core_v2_isobus_network_management_tests
    core_v2_isobus_network_management_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_isobus_network_management_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_isobus_network_management_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_isobus_network_management_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_isobus_network_management_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.isobus.network_management
    COMMAND ecu_core_v2_isobus_network_management_tests
  )

  add_executable(
    ecu_core_v2_isobus_working_set_lifecycle_tests
    core_v2_isobus_working_set_lifecycle_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_isobus_working_set_lifecycle_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_isobus_working_set_lifecycle_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_isobus_working_set_lifecycle_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_isobus_working_set_lifecycle_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.isobus.working_set_lifecycle
    COMMAND ecu_core_v2_isobus_working_set_lifecycle_tests
  )

  add_executable(
    ecu_core_v2_isotp_tests
    core_v2_isotp_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_isotp_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_isotp_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_isotp_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_isotp_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.isotp
    COMMAND ecu_core_v2_isotp_tests
  )

  add_executable(
    ecu_core_v2_uds_tests
    core_v2_uds_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_uds_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_uds_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_uds_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_uds_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.uds
    COMMAND ecu_core_v2_uds_tests
  )

  add_executable(
    ecu_core_v2_uds_isotp_e2e_tests
    core_v2_uds_isotp_e2e_tests.cpp
  )
  target_link_libraries(
    ecu_core_v2_uds_isotp_e2e_tests
    PRIVATE ECU::core_v2
  )
  set_target_properties(
    ecu_core_v2_uds_isotp_e2e_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_core_v2_uds_isotp_e2e_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_core_v2_uds_isotp_e2e_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.core_v2.uds_isotp_e2e
    COMMAND ecu_core_v2_uds_isotp_e2e_tests
  )

  add_custom_target(
    ecu_core_v2_tests
    DEPENDS
      ecu_core_v2_foundation_tests
      ecu_core_v2_dut_neutral_shared_bus_tests
      ecu_core_v2_runtime_foundation_tests
      ecu_core_v2_cyclic_can_actuator_runtime_tests
      ecu_core_v2_j1939_network_tests
      ecu_core_v2_j1939_fd_cpg_tests
      ecu_core_v2_j1939_fd_tp_tests
      ecu_core_v2_j1939_acknowledgment_tests
      ecu_core_v2_j1939_request_tests
      ecu_core_v2_j1939_diagnostics_tests
      ecu_core_v2_j1939_tp_tests
      ecu_core_v2_j1939_tp_tx_tests
      ecu_core_v2_j1939_tp_e2e_tests
      ecu_core_v2_isobus_etp_tests
      ecu_core_v2_isobus_working_set_tests
      ecu_core_v2_isobus_network_management_tests
      ecu_core_v2_isobus_working_set_lifecycle_tests
      ecu_core_v2_isotp_tests
      ecu_core_v2_uds_tests
      ecu_core_v2_uds_isotp_e2e_tests
  )
endif()

if(TARGET ECU::core)
  add_executable(ecu_core_smoke core_smoke.cpp)
  ecu_strict_test(ecu_core_smoke)
  add_test(NAME ecu.core.smoke COMMAND ecu_core_smoke)

  add_executable(ecu_can_contract_tests can_contract_tests.cpp)
  ecu_strict_test(ecu_can_contract_tests)
  add_test(NAME ecu.contract.can COMMAND ecu_can_contract_tests)

  add_executable(ecu_isotp_core_tests isotp_core_tests.cpp)
  ecu_strict_test(ecu_isotp_core_tests)
  add_test(NAME ecu.protocol.isotp COMMAND ecu_isotp_core_tests)

  add_executable(ecu_uds_core_tests uds_core_tests.cpp)
  ecu_strict_test(ecu_uds_core_tests)
  add_test(NAME ecu.protocol.uds COMMAND ecu_uds_core_tests)

  add_executable(ecu_core_foundation_tests core_foundation_tests.cpp)
  ecu_strict_test(ecu_core_foundation_tests)
  add_test(NAME ecu.core.foundation COMMAND ecu_core_foundation_tests)

  add_executable(ecu_j1939_core_tests j1939_core_tests.cpp)
  ecu_strict_test(ecu_j1939_core_tests)
  add_test(
    NAME ecu.protocol.j1939.identifier
    COMMAND ecu_j1939_core_tests
  )

  add_executable(
    ecu_core_runtime_registry_tests
    core_runtime_registry_tests.cpp
  )
  ecu_strict_test(ecu_core_runtime_registry_tests)
  add_test(
    NAME ecu.core.runtime_registry
    COMMAND ecu_core_runtime_registry_tests
  )

  add_executable(
    ecu_core_observability_simulation_tests
    core_observability_simulation_tests.cpp
  )
  ecu_strict_test(ecu_core_observability_simulation_tests)
  add_test(
    NAME ecu.core.observability_simulation
    COMMAND ecu_core_observability_simulation_tests
  )

  if(TARGET ECU::sac)
    add_executable(ecu_sac_module_tests sac_module_tests.cpp)
    ecu_strict_test(ecu_sac_module_tests)
    target_link_libraries(ecu_sac_module_tests PRIVATE ECU::sac)
    add_test(NAME ecu.sac.module COMMAND ecu_sac_module_tests)

    add_executable(
      ecu_sac_read_services_tests
      sac_read_services_tests.cpp
    )
    ecu_strict_test(ecu_sac_read_services_tests)
    target_link_libraries(ecu_sac_read_services_tests PRIVATE ECU::sac)
    add_test(
      NAME ecu.sac.read_services
      COMMAND ecu_sac_read_services_tests
    )

    add_executable(
      ecu_sac_controller_tests
      sac_controller_tests.cpp
    )
    ecu_strict_test(ecu_sac_controller_tests)
    target_link_libraries(ecu_sac_controller_tests PRIVATE ECU::sac)
    add_test(
      NAME ecu.sac.controller
      COMMAND ecu_sac_controller_tests
    )
  endif()

  if(TARGET ECU::platform_linux_socketcan)
    add_executable(
      ecu_socketcan_codec_tests
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

    add_executable(
      ecu_socketcan_link_probe
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

    add_executable(
      ecu_socketcan_adapter_probe
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

    if(TARGET ECU::sac)
      add_executable(
        ecu_sac_uds_read_probe
        sac_uds_read_probe.cpp
      )
      target_link_libraries(
        ecu_sac_uds_read_probe
        PRIVATE
          ECU::platform_linux_socketcan
          ECU::core
      )
      set_target_properties(
        ecu_sac_uds_read_probe
        PROPERTIES CXX_EXTENSIONS OFF
      )

      if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
          ecu_sac_uds_read_probe
          PRIVATE -Wall -Wextra -Wpedantic -Werror
        )
      endif()
    endif()
  endif()
endif()


if(TARGET ECU::bench)
  add_executable(
    ecu_bench_session_tests
    bench_session_tests.cpp
  )
  target_link_libraries(
    ecu_bench_session_tests
    PRIVATE ECU::bench
  )
  set_target_properties(
    ecu_bench_session_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_bench_session_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_bench_session_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.bench.session
    COMMAND ecu_bench_session_tests
  )

  add_executable(
    ecu_bench_observability_tests
    bench_observability_tests.cpp
  )
  target_link_libraries(
    ecu_bench_observability_tests
    PRIVATE ECU::bench
  )
  set_target_properties(
    ecu_bench_observability_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_bench_observability_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_bench_observability_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.bench.observability
    COMMAND ecu_bench_observability_tests
  )

  add_executable(
    ecu_bench_config_validation_tests
    bench_config_validation_tests.cpp
  )
  target_link_libraries(
    ecu_bench_config_validation_tests
    PRIVATE ECU::bench
  )
  set_target_properties(
    ecu_bench_config_validation_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_bench_config_validation_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_bench_config_validation_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.bench.config_validation
    COMMAND ecu_bench_config_validation_tests
  )

  add_executable(
    ecu_bench_host_service_tests
    bench_host_service_tests.cpp
  )
  target_link_libraries(
    ecu_bench_host_service_tests
    PRIVATE ECU::bench
  )
  set_target_properties(
    ecu_bench_host_service_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_bench_host_service_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_bench_host_service_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.bench.host_service
    COMMAND ecu_bench_host_service_tests
  )

  add_custom_target(
    ecu_bench_tests
    DEPENDS
      ecu_bench_session_tests
      ecu_bench_observability_tests
      ecu_bench_config_validation_tests
      ecu_bench_host_service_tests
  )
endif()

if(TARGET ECU::dut_profile)
  add_executable(
    ecu_dut_profile_foundation_tests
    dut_profile_foundation_tests.cpp
  )
  target_link_libraries(
    ecu_dut_profile_foundation_tests
    PRIVATE ECU::dut_profile
  )
  set_target_properties(
    ecu_dut_profile_foundation_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_dut_profile_foundation_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_dut_profile_foundation_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.dut_profile.foundation
    COMMAND ecu_dut_profile_foundation_tests
  )

  add_executable(
    ecu_dut_profile_runtime_tests
    dut_profile_runtime_tests.cpp
  )
  target_link_libraries(
    ecu_dut_profile_runtime_tests
    PRIVATE ECU::dut_profile
  )
  set_target_properties(
    ecu_dut_profile_runtime_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_dut_profile_runtime_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_dut_profile_runtime_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.dut_profile.runtime
    COMMAND ecu_dut_profile_runtime_tests
  )

  add_executable(
    ecu_dut_profile_registry_tests
    dut_profile_registry_tests.cpp
  )
  target_link_libraries(
    ecu_dut_profile_registry_tests
    PRIVATE ECU::dut_profile
  )
  set_target_properties(
    ecu_dut_profile_registry_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_dut_profile_registry_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_dut_profile_registry_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.dut_profile.registry
    COMMAND ecu_dut_profile_registry_tests
  )

  add_custom_target(
    ecu_dut_profile_tests
    DEPENDS
      ecu_dut_profile_foundation_tests
      ecu_dut_profile_runtime_tests
      ecu_dut_profile_registry_tests
  )
endif()


if(TARGET ECU::daf_sac_profile)
  add_executable(
    ecu_daf_sac_profile_tests
    daf_sac_profile_tests.cpp
  )
  target_link_libraries(
    ecu_daf_sac_profile_tests
    PRIVATE ECU::daf_sac_profile
  )
  set_target_properties(
    ecu_daf_sac_profile_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(MSVC)
    target_compile_options(
      ecu_daf_sac_profile_tests
      PRIVATE /W4 /WX
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_daf_sac_profile_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(
    NAME ecu.dut_profile.daf_sac
    COMMAND ecu_daf_sac_profile_tests
  )

  add_custom_target(
    ecu_daf_sac_profile_tests_target
    DEPENDS ecu_daf_sac_profile_tests
  )
endif()


if(TARGET ECU::platform_linux_v2)
  add_executable(
    ecu_linux_v2_platform_tests
    linux_v2_platform_tests.cpp
  )
  target_link_libraries(
    ecu_linux_v2_platform_tests
    PRIVATE ECU::platform_linux_v2
  )
  set_target_properties(
    ecu_linux_v2_platform_tests
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_linux_v2_platform_tests
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(
    NAME ecu.platform.linux_v2
    COMMAND ecu_linux_v2_platform_tests
  )
  add_custom_target(
    ecu_linux_v2_platform_tests_target
    DEPENDS ecu_linux_v2_platform_tests
  )
endif()


if(
  TARGET ECU::daf_sac_profile
  AND TARGET ECU::platform_linux_v2
)
  add_executable(
    ecu_daf_sac_core_v2_probe
    daf_sac_core_v2_physical_probe.cpp
  )
  target_link_libraries(
    ecu_daf_sac_core_v2_probe
    PRIVATE
      ECU::daf_sac_profile
      ECU::platform_linux_v2
  )
  set_target_properties(
    ecu_daf_sac_core_v2_probe
    PROPERTIES CXX_EXTENSIONS OFF
  )
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ecu_daf_sac_core_v2_probe
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
endif()
