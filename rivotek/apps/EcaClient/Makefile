############################################################################
# vendor/allwinnertech/rivotek/apps/EcaClient/Makefile
############################################################################

include $(APPDIR)/Make.defs

RIVOTEK_DIR = ../..
RIVOTEK_PLATFORM = $(RIVOTEK_DIR)/platform
RIVOTEK_SERVICES = $(RIVOTEK_DIR)/services
QRCODE_SERVICE = $(RIVOTEK_SERVICES)/qrcode_service
PROVISIONING_SERVICE = $(RIVOTEK_SERVICES)/provisioning_service
TIME_SERVICE = $(RIVOTEK_SERVICES)/time_service
ECA_CLIENT_CORE = ./core
ECA_CLIENT_UI = ./ui
ECA_CLIENT_INCLUDE = ./include

MAINSRC = eca_client_main.c
CSRCS = \
	eca_client_ai_agent.c \
	eca_client_command_dispatcher.c \
	eca_client_device_state.c \
	eca_client_humidity_sensor.c \
	eca_client_network.c \
	eca_client_plan_observer.c \
	eca_client_remote_broadcast.c \
	eca_client_registration.c \
	eca_client_time_observer.c \
	eca_client_voice_keyword.c \
	eca_client_model.c \
	eca_client_actions.c \
	eca_client_header.c \
	eca_client_health.c \
	eca_client_humidity.c \
	eca_client_ui.c \
	eca_client_ui_common.c \
	rivotek_platform_default.c

VPATH += :$(ECA_CLIENT_CORE)
VPATH += :$(ECA_CLIENT_UI)
VPATH += :$(RIVOTEK_PLATFORM)/src
ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
VPATH += :$(QRCODE_SERVICE)/src
endif
ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
VPATH += :$(PROVISIONING_SERVICE)/src
VPATH += :$(PROVISIONING_SERVICE)/port/openvela_allwinner_r528
endif

ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
CSRCS += rvt_qrcode.c
endif
ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
CSRCS += rvt_provisioning_protocol.c
CSRCS += rvt_provisioning_service.c
CSRCS += rvt_provisioning_wifi_r528.c
endif

CFLAGS += $(RIVOTEK_COMPONENT_CFLAGS)
CFLAGS += ${INCDIR_PREFIX}$(RIVOTEK_PLATFORM)/include
CFLAGS += ${INCDIR_PREFIX}$(ECA_CLIENT_INCLUDE)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/system/uorb/
ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
CFLAGS += ${INCDIR_PREFIX}$(QRCODE_SERVICE)/include
endif
ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
CFLAGS += ${INCDIR_PREFIX}$(PROVISIONING_SERVICE)/include
endif
ifdef CONFIG_RIVOTEK_COMPONENT_TIME
CFLAGS += ${INCDIR_PREFIX}$(TIME_SERVICE)/include
endif

PROGNAME = $(CONFIG_ECA_CLIENT_APP_PROGNAME)
PRIORITY = $(CONFIG_ECA_CLIENT_APP_PRIORITY)
STACKSIZE = $(CONFIG_ECA_CLIENT_APP_STACKSIZE)
MODULE = $(CONFIG_ECA_CLIENT_APP)

include $(APPDIR)/Application.mk

clean::
	$(Q) find . $(ECA_CLIENT_CORE) $(ECA_CLIENT_UI) $(RIVOTEK_PLATFORM)/src \
		$(QRCODE_SERVICE)/src -maxdepth 1 -type f \
		\( -name "*.vendor.allwinnertech.rivotek.apps.EcaClient.o" \
		-o -name "*.vendor.allwinnertech.rivotek.apps.EcaClient_1.o" \
		-o -name "*.o" -o -name ".built" -o -name ".depend" \
		-o -name "Make.dep" \) -delete
