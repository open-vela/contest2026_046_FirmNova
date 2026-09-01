############################################################################
# apps/examples/helloxx/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################
include $(APPDIR)/Make.defs
include $(APPDIR)/../contest2026_046_FirmNova/Common.mk

RIVOTEK_DIR = ../..
SCOOTERDEMO_APP_DIR = .
SCOOTER_CORE = $(SCOOTERDEMO_APP_DIR)/core
SCOOTER_APP_INCLUDE = $(SCOOTERDEMO_APP_DIR)/include
SCOOTER_UI_SRC = $(SCOOTERDEMO_APP_DIR)/ui
SCOOTER_DRIVER_DEMO = $(SCOOTERDEMO_APP_DIR)/drivers/driver_demo
SCOOTER_PLATFORM = $(RIVOTEK_DIR)/platform
SCOOTER_BT_EXTERNAL = $(RIVOTEK_DIR)/integrations/bluetooth_external
SCOOTER_SERVICES = $(RIVOTEK_DIR)/services
QRCODE_SERVICE = $(SCOOTER_SERVICES)/qrcode_service
PROVISIONING_SERVICE = $(SCOOTER_SERVICES)/provisioning_service
RIVOTEK_COMPONENTS_INCLUDE_BASE ?= $(APPDIR)/../contest2026_046_FirmNova/chips/r528/components

VPATH += :$(SCOOTER_CORE)
VPATH += :$(SCOOTER_UI_SRC)
VPATH += :$(SCOOTER_DRIVER_DEMO)/src
VPATH += :$(SCOOTER_PLATFORM)/src
VPATH += :$(SCOOTER_BT_EXTERNAL)/src
ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
VPATH += :$(QRCODE_SERVICE)/src
endif
ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
VPATH += :$(PROVISIONING_SERVICE)/src
endif

MAINSRC = scooterdemo.c

CSRCS = \
	boot_demo.c \
	driver_demo.c \
	rivotek_platform_default.c \
	scooterdemo_page_manager.c \
	theme_impl.c \
	scooterdemo_page_navigation.c \
	scooterdemo_page_music.c \
	scooterdemo_page_settings.c \
	scooterdemo_settings_subpage_system.c \
	scooterdemo_settings_subpage_tire.c \
	scooterdemo_settings_subpage_parking.c \
	scooterdemo_settings_subpage_vehicle.c \
	scooterdemo_settings_subpage_theme.c \
	scooterdemo_page_ai.c

CFLAGS += $(RIVOTEK_COMPONENT_CFLAGS)

ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
CSRCS += rvt_qrcode.c
CFLAGS += ${INCDIR_PREFIX}$(QRCODE_SERVICE)/include
endif

ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
CFLAGS += ${INCDIR_PREFIX}$(PROVISIONING_SERVICE)/include
ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
CSRCS += rvt_provisioning_qrcode_bridge.c
endif
endif

ifeq ($(CONFIG_EXTERNAL_BLUETOOTH_MODULE),y)
CSRCS += bt_music_impl_external.c
endif

CFLAGS += ${INCDIR_PREFIX}$(SCOOTER_APP_INCLUDE)
CFLAGS += ${INCDIR_PREFIX}$(SCOOTER_DRIVER_DEMO)/include
CFLAGS += ${INCDIR_PREFIX}$(SCOOTER_PLATFORM)/include

ifdef CONFIG_BT_A2DP
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/apps/bt_instance
endif

ifdef CONFIG_AUDIOSERVICE
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_046_FirmNova/rivotek/services/audioservice
endif

ifdef CONFIG_COMPONENTS_WEATHER
CFLAGS += ${INCDIR_PREFIX}$(RIVOTEK_COMPONENTS_INCLUDE_BASE)/weather_service/include
endif

PROGNAME = $(CONFIG_SCOOTERDEMO_APP_PROGNAME)
PRIORITY = $(CONFIG_SCOOTERDEMO_APP_PRIORITY)
STACKSIZE = $(CONFIG_SCOOTERDEMO_APP_STACKSIZE)
MODULE = $(CONFIG_SCOOTERDEMO_APP)

include $(APPDIR)/Application.mk

clean::
	$(Q) find . $(SCOOTER_CORE) $(SCOOTER_BT_EXTERNAL)/src \
		$(SCOOTER_DRIVER_DEMO)/src \
		$(SCOOTER_PLATFORM)/src \
		$(QRCODE_SERVICE)/src \
		$(PROVISIONING_SERVICE)/src \
		$(SCOOTER_UI_SRC) -maxdepth 1 -type f \
		\( -name "*.vendor.allwinnertech.rivotek.apps.scooterdemo.o" \
		-o -name "*.vendor.allwinnertech.rivotek.apps.scooterdemo_1.o" \
		-o -name "*.o" -o -name ".built" -o -name ".depend" \
		-o -name "Make.dep" \) -delete
