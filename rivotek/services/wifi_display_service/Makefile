include $(APPDIR)/Make.defs
include $(APPDIR)/../contest2026_046_FirmNova/Common.mk

ifeq ($(CONFIG_RIVOTEK_WIFI_DISPLAY),y)

CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/services/common/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/services/map_service/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/include

CSRCS += src/rvt_discovery_server.c
CSRCS += src/rvt_udp_receiver.c
CSRCS += src/rvt_wifi_display_api.c

ifeq ($(CONFIG_RIVOTEK_WIFI_DISPLAY_PORT_OPENVELA_ALLWINNER_R528),y)
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/port/openvela_allwinner_r528
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/wireless/wapi/src
ifeq ($(CONFIG_RIVOTEK_WIFI_DISPLAY_R528_V4L2_MJPEG),y)
CFLAGS += -DRVT_R528_HAS_V4L2_MJPEG=1
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/libuapi/inc
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/libuapi/src/include
endif
ifeq ($(CONFIG_RIVOTEK_WIFI_DISPLAY_R528_CEDARC_MJPEG),y)
RVT_R528_CEDARC_LIB_DIR := $(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/multimedia/lib/libcedarc
RVT_R528_CEDARC_PATCHED_LIB_DIR := $(RVT_R528_CEDARC_LIB_DIR)/r528_weak
$(shell sh $(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/multimedia/r528_patch_cedarc_lib.sh "$(RVT_R528_CEDARC_LIB_DIR)" "$(OBJCOPY)" "$(CROSSDEV)ar")
CFLAGS += -DRVT_R528_HAS_CEDARC_MJPEG=1
CFLAGS += -DRVT_R528_HAS_CEDARC_H264=1
CFLAGS += -DRVT_WIFI_DISPLAY_RX_H264_RAW=1
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/libuapi/inc
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/libuapi/src/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/chips/r528/components/multimedia/src/libcedarx/external/include/libcedarc
EXTRA_LIBPATHS := -L$(RVT_R528_CEDARC_PATCHED_LIB_DIR) $(EXTRA_LIBPATHS)
EXTRA_LIBPATHS += -L$(RVT_R528_CEDARC_LIB_DIR)
EXTRA_LIBS += -lcedarc
endif
CFLAGS += -DRVT_TCP_ROLE_CLIENT=1
CFLAGS += -DRVT_SOCKET_AVOID_POLL=1
CFLAGS += -DRVT_TCP_CLIENT_ASYNC_CONNECT=1
CFLAGS += -DRVT_TCP_THREAD_STACK=16384
CFLAGS += -DRVT_DISCOVERY_THREAD_STACK=8192
CFLAGS += -DRVT_START_OTHER_CHANNELS_DELAY_MS=3000
CFLAGS += -DRVT_WIFI_DISPLAY_CMD_AS_SERVICE_TASK=1
CFLAGS += -DRVT_WIFI_DISPLAY_MAX_FRAME_SIZE=524288
CFLAGS += -DRVT_WIFI_DISPLAY_UDP_RECV_BUF_SIZE=524288
CSRCS += src/rvt_tcp_client.c
CSRCS += port/openvela_allwinner_r528/r528_display_port.c
CSRCS += port/openvela_allwinner_r528/rvt_wifi_display_network_control.c
CSRCS += port/openvela_allwinner_r528/rvt_wifi_display_osal.c
else
CSRCS += src/rvt_tcp_server.c
CFLAGS += ${INCDIR_PREFIX}$(CURDIR)/port/stub
CSRCS += port/stub/rvt_wifi_display_network_control.c
CSRCS += port/stub/wifi_display_capability_stub.c
CSRCS += port/openvela_allwinner_r528/rvt_wifi_display_osal.c
endif

PROGNAME += start_wifi_display
PROGNAME += stop_wifi_display
PROGNAME += wifi_display_screen_size
PROGNAME += wifi_display_rotation

PRIORITY += $(CONFIG_RIVOTEK_WIFI_DISPLAY_PRIORITY)
PRIORITY += $(CONFIG_RIVOTEK_WIFI_DISPLAY_PRIORITY)
PRIORITY += $(CONFIG_RIVOTEK_WIFI_DISPLAY_PRIORITY)
PRIORITY += $(CONFIG_RIVOTEK_WIFI_DISPLAY_PRIORITY)

STACKSIZE += $(CONFIG_RIVOTEK_WIFI_DISPLAY_STACKSIZE)
STACKSIZE += $(CONFIG_RIVOTEK_WIFI_DISPLAY_STACKSIZE)
STACKSIZE += $(CONFIG_RIVOTEK_WIFI_DISPLAY_STACKSIZE)
STACKSIZE += $(CONFIG_RIVOTEK_WIFI_DISPLAY_STACKSIZE)

MAINSRC += cli/start_wifi_display.c
MAINSRC += cli/stop_wifi_display.c
MAINSRC += cli/wifi_display_screen_size.c
MAINSRC += cli/wifi_display_rotation.c

endif

include $(APPDIR)/Application.mk
