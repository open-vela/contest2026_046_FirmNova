############################################################################
# vendor/allwinnertech/apps/tflite-micro/Makefile
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

CSRCS        :=
CXXEXT       := .cc
CXXSRCS      :=
COMMON_FLAGS :=
TFLM_UNPACK  := tflite-micro
TFLM_VER     := cfa4c91d1b36c37c7c104b9c664615e59f1abfe3
TFLM_DIR     := $(APPDIR)/../contest2026_046_FirmNova/rivotek/apps/tflite-micro/$(TFLM_UNPACK)

tflite-micro.zip:
	$(Q) curl -L https://github.com/tensorflow/tflite-micro/archive/$(TFLM_VER).zip -o tflite-micro.zip
	$(Q) unzip -o tflite-micro.zip
	$(Q) mv tflite-micro-$(TFLM_VER) $(TFLM_UNPACK)
	$(Q) patch -d $(TFLM_UNPACK) -p1 < tflite-micro.patch
	$(Q) patch -d $(TFLM_UNPACK) -p1 < 0001-dequantize-int8.patch
	$(Q) patch -d $(TFLM_UNPACK) -p1 < 0002-quantize-int8.patch
	$(Q) patch -d $(TFLM_UNPACK) -p1 < 0003-mean-int8.patch

ifeq ($(wildcard $(TFLM_UNPACK)),)
context:: tflite-micro.zip
distclean::
	$(call DELDIR, $(TFLM_UNPACK))
	$(call DELFILE, tflite-micro.zip)
endif

COMMON_FLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
COMMON_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
COMMON_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
COMMON_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
COMMON_FLAGS += -Wno-shadow
COMMON_FLAGS += -Wno-sign-compare
COMMON_FLAGS += -Wno-undef
COMMON_FLAGS += -Wno-unused-variable
COMMON_FLAGS += -DTFLITE_EMULATE_FLOAT
COMMON_FLAGS += -DTF_LITE_DISABLE_X86_NEON
ifneq ($(CONFIG_VELA_TFLITEMICRO_DEBUG),)
COMMON_FLAGS += -DTF_LITE_SHOW_MEMORY_USE
COMMON_FLAGS += -DTF_LITE_USE_CTIME
else
COMMON_FLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif

CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/c/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/core/c/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/core/api/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/optimized/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/kernels/internal/reference/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/arena_allocator/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/kernels/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/memory_planner/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/tflite_bridge/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/schema/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/signal/src/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/signal/src/kiss_fft_wrappers/*.cc)
CXXSRCS += $(wildcard $(TFLM_DIR)/signal/micro/kernels/*.cc)
CXXSRCS := $(filter-out %test.cc, $(CXXSRCS))

ifneq ($(CONFIG_ETHOSU),)
CXXSRCS += $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/kernels/ethos_u/*.cc)
endif

ifneq ($(CONFIG_MLEARNING_CMSIS_NN),)
COMMON_FLAGS += -DCMSIS_NN
COMMON_FLAGS += ${INCDIR_PREFIX}$(APPDIR)/mlearning/cmsis-nn/cmsis-nn

CMSIS_NN_SRCS := $(wildcard $(TFLM_DIR)/tensorflow/lite/micro/kernels/cmsis_nn/*.cc)
UNNEEDED_SRCS := $(addprefix $(TFLM_DIR)/tensorflow/lite/micro/kernels/, $(notdir $(CMSIS_NN_SRCS)))
CXXSRCS := $(filter-out $(UNNEEDED_SRCS), $(CXXSRCS))
CXXSRCS += $(CMSIS_NN_SRCS)

ifneq ($(CONFIG_ARM_NEON),)
CSRCS += operators/neon/arm_convolve_s8.c
CSRCS += operators/neon/arm_nn_mat_mult_kernel_s8_s16.c
CSRCS += operators/neon/arm_q7_to_q15_with_offset.c
CSRCS += operators/neon/arm_elementwise_add_s8.c
endif
endif

-include $(TFLM_DIR)/tensorflow/lite/micro/nuttx/Makefile

ifneq ($(CONFIG_VELA_TFLITEMICRO_TOOL),)
MAINSRC   = tflm_tool.cc
PROGNAME  = vela_tflm
PRIORITY  = $(CONFIG_VELA_TFLITEMICRO_TOOL_PRIORITY)
STACKSIZE = $(CONFIG_VELA_TFLITEMICRO_TOOL_STACKSIZE)
MODULE    = $(CONFIG_VELA_TFLITEMICRO_TOOL)
endif

ifneq ($(CONFIG_VELA_TFLITEMICRO_SPEECH_TEST),)
MAINSRC   += simple_speech_test.cc
PROGNAME  += vela_speech_test
PRIORITY  += $(CONFIG_VELA_TFLITEMICRO_SPEECH_TEST_PRIORITY)
STACKSIZE += $(CONFIG_VELA_TFLITEMICRO_SPEECH_TEST_STACKSIZE)
MODULE    += $(CONFIG_VELA_TFLITEMICRO_SPEECH_TEST)
endif

ifneq ($(CONFIG_VELA_TFLITEMICRO_SPEECH_RECOGNITION),)
MAINSRC   += speech_recognition.cc
PROGNAME  += vela_speech_recog
PRIORITY  += $(CONFIG_VELA_TFLITEMICRO_SPEECH_RECOGNITION_PRIORITY)
STACKSIZE += $(CONFIG_VELA_TFLITEMICRO_SPEECH_RECOGNITION_STACKSIZE)
MODULE    += $(CONFIG_VELA_TFLITEMICRO_SPEECH_RECOGNITION)
endif

CFLAGS   += ${COMMON_FLAGS}
CXXFLAGS += ${COMMON_FLAGS}

include $(APPDIR)/Application.mk
