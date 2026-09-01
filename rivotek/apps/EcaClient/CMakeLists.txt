# ##############################################################################
# vendor/allwinnertech/rivotek/apps/EcaClient/CMakeLists.txt
# ##############################################################################

if(CONFIG_ECA_CLIENT_APP)
  set(ECA_CLIENT_SRCS
      core/eca_client_main.c
      core/eca_client_command_dispatcher.c
      core/eca_client_device_state.c
      core/eca_client_humidity_sensor.c
      core/eca_client_network.c
      core/eca_client_plan_observer.c
      core/eca_client_remote_broadcast.c
      core/eca_client_registration.c
      core/eca_client_time_observer.c
      core/eca_client_voice_keyword.c
      core/eca_client_model.c
      ui/eca_client_actions.c
      ui/eca_client_header.c
      ui/eca_client_health.c
      ui/eca_client_humidity.c
      ui/eca_client_ui.c
      ui/eca_client_ui_common.c
      ../../services/audio_service/src/rivotek_audio_service.c
      ../../services/time_service/src/time_impl.c
      ../../platform/src/rivotek_platform_default.c)

  set(ECA_CLIENT_INCLUDES
      ${CMAKE_CURRENT_LIST_DIR}/include
      ${CMAKE_CURRENT_LIST_DIR}/../../services/audio_service/include
      ${CMAKE_CURRENT_LIST_DIR}/../../services/time_service/include
      ${CMAKE_CURRENT_LIST_DIR}/../../platform/include
      ${NUTTX_APPS_DIR}/system/uorb)

  if(CONFIG_RIVOTEK_QRCODE_SERVICE)
    list(APPEND ECA_CLIENT_SRCS ../../services/qrcode_service/src/rvt_qrcode.c)
    list(APPEND ECA_CLIENT_INCLUDES
         ${CMAKE_CURRENT_LIST_DIR}/../../services/qrcode_service/include)
  endif()

  if(CONFIG_RIVOTEK_PROVISIONING_SERVICE)
    list(APPEND ECA_CLIENT_INCLUDES
         ${CMAKE_CURRENT_LIST_DIR}/../../services/provisioning_service/include)
  endif()

  nuttx_add_application(
    NAME
    ${CONFIG_ECA_CLIENT_APP_PROGNAME}
    SRCS
    ${ECA_CLIENT_SRCS}
    INCLUDE_DIRECTORIES
    ${ECA_CLIENT_INCLUDES}
    STACKSIZE
    ${CONFIG_ECA_CLIENT_APP_STACKSIZE}
    PRIORITY
    ${CONFIG_ECA_CLIENT_APP_PRIORITY})
endif()
