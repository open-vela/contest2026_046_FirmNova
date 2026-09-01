include $(APPDIR)/Make.defs

ifeq ($(CONFIG_RIVOTEK_CONNECTIVITY_SERVICE),y)

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/wifi/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/wireless/wapi/src

ifeq ($(CONFIG_RIVOTEK_WIFI_DISPLAY),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/services/wifi_display_service/include
CFLAGS += -DRVT_CONNECTIVITY_WITH_WIFI_DISPLAY=1
endif

CSRCS += wifi/src/rvt_wifi_autoconnect.c

PROGNAME += start_wifi

PRIORITY += $(CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_PRIORITY)

STACKSIZE += $(CONFIG_RIVOTEK_CONNECTIVITY_SERVICE_STACKSIZE)

MAINSRC += wifi/cli/start_wifi.c

endif

include $(APPDIR)/Application.mk
