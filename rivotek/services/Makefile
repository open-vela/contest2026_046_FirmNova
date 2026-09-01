############################################################################
# vendor/allwinnertech/rivotek/services/Makefile
############################################################################

include $(APPDIR)/Make.defs
include $(APPDIR)/../contest2026_046_FirmNova/Common.mk

RIVOTEK_DIR = ..
RIVOTEK_PLATFORM_DIR = $(RIVOTEK_DIR)/platform

ifdef CONFIG_RIVOTEK_COMPONENT_DISPLAY
CFLAGS += ${INCDIR_PREFIX}./display_service/include
endif

ifdef CONFIG_RIVOTEK_COMPONENT_INPUT
VPATH += :./input_service/src
DEPPATH += --dep-path ./input_service/src
CSRCS += key_processor.c buttonProcessor.c
CFLAGS += ${INCDIR_PREFIX}./input_service/include
CFLAGS += ${INCDIR_PREFIX}$(RIVOTEK_PLATFORM_DIR)/include
endif

ifdef CONFIG_RIVOTEK_COMPONENT_TIME
VPATH += :./time_service/src
DEPPATH += --dep-path ./time_service/src
CSRCS += time_impl.c
CFLAGS += ${INCDIR_PREFIX}./time_service/include
endif

ifdef CONFIG_RIVOTEK_COMPONENT_WEATHER
VPATH += :./weather_service/src
DEPPATH += --dep-path ./weather_service/src
CSRCS += weather_impl.c
CFLAGS += ${INCDIR_PREFIX}./weather_service/include
endif

ifdef CONFIG_RIVOTEK_COMPONENT_AUDIO
CFLAGS += ${INCDIR_PREFIX}./audio_service/include
VPATH += :./audio_service/src
DEPPATH += --dep-path ./audio_service/src
CSRCS += rivotek_audio_service.c
ifneq ($(CONFIG_EXTERNAL_BLUETOOTH_MODULE),y)
CSRCS += bt_music_impl.c
endif
endif

include $(APPDIR)/Application.mk

clean::
	$(Q) find . ./input_service/src ./time_service/src ./weather_service/src \
		./audio_service/src -maxdepth 1 -type f \
		\( -name "*.vendor.allwinnertech.rivotek.services.o" \
		-o -name "*.vendor.allwinnertech.rivotek.services_1.o" \
		-o -name "*.vendor.allwinnertech.rivotek.components.o" \
		-o -name "*.vendor.allwinnertech.rivotek.components_1.o" \
		-o -name "*.o" -o -name ".built" -o -name ".depend" \
		-o -name "Make.dep" \) -delete
