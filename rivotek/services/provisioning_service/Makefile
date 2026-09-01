include $(APPDIR)/Make.defs

ifeq ($(CONFIG_RIVOTEK_PROVISIONING_SERVICE),y)

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/include
ifeq ($(CONFIG_RIVOTEK_CONNECTIVITY_SERVICE),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/services/connectivity_service/wifi/include
endif
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/wireless/wapi/src

CSRCS += src/rvt_provisioning_protocol.c
CSRCS += src/rvt_provisioning_service.c
CSRCS += port/openvela_allwinner_r528/rvt_provisioning_wifi_r528.c

VPATH += :cli
VPATH += :src
VPATH += :port/openvela_allwinner_r528

PROGNAME += start_provisioning_wifi
PROGNAME += stop_provisioning

PRIORITY += $(CONFIG_RIVOTEK_PROVISIONING_SERVICE_PRIORITY)
PRIORITY += $(CONFIG_RIVOTEK_PROVISIONING_SERVICE_PRIORITY)

STACKSIZE += $(CONFIG_RIVOTEK_PROVISIONING_SERVICE_STACKSIZE)
STACKSIZE += $(CONFIG_RIVOTEK_PROVISIONING_SERVICE_STACKSIZE)

MAINSRC += start_provisioning_wifi.c
MAINSRC += stop_provisioning.c

endif

include $(APPDIR)/Application.mk
