# ##############################################################################
# apps/mlearning/tflite-micro/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_TFLITEMICRO)

  # ############################################################################
  # Config and Fetch TFLITE-MICRO
  # ############################################################################

  set(TFLITE_MICRO_DIR ${CMAKE_CURRENT_LIST_DIR}/tflite-micro)

  if(NOT EXISTS ${TFLITE_MICRO_DIR})
    set(TFLITE_MICRO_URL
        https://github.com/tensorflow/tflite-micro/archive/cfa4c91d1b36c37c7c104b9c664615e59f1abfe3.zip
    )
    FetchContent_Declare(
      tflite_micro_fetch
      URL ${TFLITE_MICRO_URL} SOURCE_DIR ${TFLITE_MICRO_DIR} BINARY_DIR
          ${CMAKE_BINARY_DIR}/apps/mlearning/tflite-micro/tflite-micro
      DOWNLOAD_NO_PROGRESS true
      TIMEOUT 30)

    FetchContent_GetProperties(tflite_micro_fetch)

    if(NOT tflite_micro_fetch_POPULATED)
      FetchContent_Populate(tflite_micro_fetch)

      # Apply the patch after fetching the content
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E echo "Applying patch to tflite-micro"
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
      execute_process(COMMAND patch -d ${TFLITE_MICRO_DIR} -p1 <
                              ${CMAKE_CURRENT_LIST_DIR}/tflite-micro.patch)
      execute_process(
        COMMAND patch -d ${TFLITE_MICRO_DIR} -p1 <
                ${CMAKE_CURRENT_LIST_DIR}/0001-dequantize-int8.patch)
      execute_process(
        COMMAND patch -d ${TFLITE_MICRO_DIR} -p1 <
                ${CMAKE_CURRENT_LIST_DIR}/0002-quantize-int8.patch)
      execute_process(COMMAND patch -d ${TFLITE_MICRO_DIR} -p1 <
                              ${CMAKE_CURRENT_LIST_DIR}/0003-mean-int8.patch)
    endif()
  endif()

  # ############################################################################
  # Flags
  # ############################################################################

  set(COMMON_FLAGS
      -DTF_LITE_STATIC_MEMORY
      -DTF_LITE_DISABLE_X86_NEON
      -DTF_LITE_STRIP_ERROR_STRINGS
      -Wno-sign-compare
      -Wno-unused-variable
      -Wno-undef
      -Wno-shadow
      -DTFLITE_WITH_STABLE_ABI=0
      -DTFLITE_USE_OPAQUE_DELEGATE=0
      -DTFLITE_SINGLE_ROUNDING=0
      -O3)

  if(CONFIG_MLEARNING_CMSIS_NN)
    list(APPEND COMMON_FLAGS -DCMSIS_NN)
  endif()

  # ############################################################################
  # Additional flags for TFLM tool and hardware operators
  if(CONFIG_TFLITEMICRO_TOOL)
    list(APPEND COMMON_FLAGS -DTFLITE_MODEL_COMPILER)
  endif()

  # ############################################################################
  # Sources
  # ############################################################################

  file(
    GLOB
    TFLITE_MICRO_SRCS
    ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/kernels/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/c/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/schema/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/core/c/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/kernels/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/kernels/internal/optimized/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/kernels/internal/reference/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/kernels/internal/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/core/api/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/arena_allocator/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/memory_planner/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/*.cc
    ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/tflite_bridge/*.cc)

  # Remove test file
  list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX ".*test.cc")
  list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX ".*test_common.cc")

  # Add Ethos-U NPU kernel support if enabled
  if(CONFIG_ETHOSU)
    file(GLOB ETHOS_U_SRCS
         ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/kernels/ethos_u/*.cc)

    # Remove generic kernels that have Ethos-U specific implementations Extract
    # filenames from Ethos-U directory
    set(ETHOS_U_FILENAMES)
    foreach(file ${ETHOS_U_SRCS})
      get_filename_component(filename ${file} NAME)
      list(APPEND ETHOS_U_FILENAMES ${filename})
    endforeach()

    # Remove files with same filename from generic kernels directory
    set(NEW_TFLITE_MICRO_SRCS)
    foreach(file ${TFLITE_MICRO_SRCS})
      get_filename_component(filename ${file} NAME)
      if(NOT filename IN_LIST ETHOS_U_FILENAMES)
        list(APPEND NEW_TFLITE_MICRO_SRCS ${file})
      endif()
    endforeach()

    # Add Ethos-U specific implementations
    list(APPEND NEW_TFLITE_MICRO_SRCS ${ETHOS_U_SRCS})
    set(TFLITE_MICRO_SRCS ${NEW_TFLITE_MICRO_SRCS})
  endif()

  if(CONFIG_MLEARNING_CMSIS_NN)
    list(APPEND COMMON_FLAGS -DCMSIS_NN)

    file(GLOB CMSIS_NN_SRCS
         ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/kernels/cmsis_nn/*.cc)

    # Remove the directory and get only filename
    set(CMSIS_NN_FILENAMES)
    foreach(file ${CMSIS_NN_SRCS})
      get_filename_component(filename ${file} NAME)
      list(APPEND CMSIS_NN_FILENAMES ${filename})
    endforeach()

    # Remove files with the same filename without the directory
    set(NEW_TFLITE_MICRO_SRCS)
    foreach(file ${TFLITE_MICRO_SRCS})
      get_filename_component(filename ${file} NAME)
      if(NOT filename IN_LIST CMSIS_NN_FILENAMES)
        list(APPEND NEW_TFLITE_MICRO_SRCS ${file})
      endif()
    endforeach()

    # 添加CMSIS_NN文件
    list(APPEND NEW_TFLITE_MICRO_SRCS ${CMSIS_NN_SRCS})
    set(TFLITE_MICRO_SRCS ${NEW_TFLITE_MICRO_SRCS})

    if(CONFIG_ARM_NEON)
      list(
        APPEND
        TFLITE_MICRO_SRCS
        ${TFLITE_MICRO_DIR}/../operators/neon/arm_convolve_s8.c
        ${TFLITE_MICRO_DIR}/../operators/neon/arm_nn_mat_mult_kernel_s8_s16.c
        ${TFLITE_MICRO_DIR}/../operators/neon/arm_q7_to_q15_with_offset.c
        ${TFLITE_MICRO_DIR}/../operators/neon/arm_elementwise_add_s8.c)
    endif()
  endif()

  # ############################################################################
  # Xtensa hardware operator support
  if(CONFIG_XTENSA_HIFI)
    # Add XTENSA flag to enable Xtensa-specific kernel variants This prevents
    # inline fallback implementations in headers
    list(APPEND COMMON_FLAGS -DXTENSA)

    # Add HIFI4 flag to enable HiFi4 DSP optimized implementations This enables
    # xa_nn_xxx function calls from nnlib-hifi4
    list(APPEND COMMON_FLAGS -DHIFI4)

    file(GLOB XTENSA_SRCS
         ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/kernels/xtensa/*.cc)

    # Exclude hifimini subdirectory files
    list(FILTER XTENSA_SRCS EXCLUDE REGEX ".*/hifimini/.*")

    # Remove any existing operator files from generic kernels or CMSIS-NN paths
    # that have Xtensa-optimized versions (to prevent symbol conflicts)
    foreach(file ${XTENSA_SRCS})
      get_filename_component(xtensa_filename ${file} NAME)
      # Exclude from generic kernels directory
      list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX
           ".*/tensorflow/lite/micro/kernels/${xtensa_filename}$")
      # Exclude from CMSIS-NN directory (if present)
      list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX
           ".*/tensorflow/lite/micro/kernels/cmsis_nn/${xtensa_filename}$")
    endforeach()

    # Add Xtensa optimized operator files
    list(APPEND TFLITE_MICRO_SRCS ${XTENSA_SRCS})

    # Add nnlib-hifi4 include directory for xa_nn_xxx function declarations
    set(NNLIB_HIFI4_ROOT_DIR
        ${CMAKE_CURRENT_LIST_DIR}/../nnlib-hifi4/nnlib-hifi4/xa_nnlib)
    if(EXISTS ${NNLIB_HIFI4_ROOT_DIR})
      list(APPEND TFLITE_MICRO_XTENSA_INCLUDE ${NNLIB_HIFI4_ROOT_DIR})
      list(APPEND TFLITE_MICRO_XTENSA_INCLUDE ${NNLIB_HIFI4_ROOT_DIR}/include)
      list(APPEND TFLITE_MICRO_XTENSA_INCLUDE
           ${NNLIB_HIFI4_ROOT_DIR}/include/nnlib)
    endif()
  endif()

  # ############################################################################
  # Beco hardware operator support
  if(CONFIG_BES_BECO_CONV)
    # When building the standalone tool, enable all third-party hardware
    # operators. Set BECO include directories for the tool - adaptive path
    # detection

    # Check for services_1700 directory first, then fallback to services
    if(EXISTS ${CMAKE_SOURCE_DIR}/../vendor/bes/framework/services_1700)
      set(BES_SERVICES_DIR
          ${CMAKE_SOURCE_DIR}/../vendor/bes/framework/services_1700)
    else()
      set(BES_SERVICES_DIR ${CMAKE_SOURCE_DIR}/../vendor/bes/framework/services)
    endif()

    set(BECO_INCLUDE_DIRS
        ${BES_SERVICES_DIR}/include/beco
        ${BES_SERVICES_DIR}/include/beco/beco_nn
        ${BES_SERVICES_DIR}/include/cmsis_dsp
        ${BES_SERVICES_DIR}/platform/cmsis/inc
        ${BES_SERVICES_DIR}/platform/hal
        ${CMAKE_CURRENT_LIST_DIR}/../cmsis-nn/cmsis-nn/Include)

    set(NEW_BECO_SRCS
        ${TFLITE_MICRO_DIR}/tensorflow/lite/micro/kernels/beco/conv.cc)

    # Remove any existing conv.cc introduced from the generic or CMSIS-NN paths
    get_filename_component(beconv_filename ${NEW_BECO_SRCS} NAME)
    list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX ".*/${beconv_filename}$")
    list(FILTER TFLITE_MICRO_SRCS EXCLUDE REGEX
         ".*/tensorflow/lite/micro/kernels/cmsis_nn/${beconv_filename}$")

    list(APPEND TFLITE_MICRO_SRCS ${NEW_BECO_SRCS})
  endif()

  # ############################################################################
  # Include Directory
  # ############################################################################

  set(INCDIR
      ${CMAKE_BINARY_DIR}/apps/math/gemmlowp/gemmlowp
      ${CMAKE_BINARY_DIR}/apps/math/ruy/ruy
      ${CMAKE_BINARY_DIR}/apps/math/kissfft/kissfft
      ${CMAKE_BINARY_DIR}/apps/math/tflite-micro/tflite-micro
      # Source directory of tflite-micro (contains tensorflow/* headers)
      ${TFLITE_MICRO_DIR})

  # Additional third-party headers required by TFLite Micro
  list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/../../math/gemmlowp/gemmlowp
       ${CMAKE_CURRENT_LIST_DIR}/../../math/ruy/ruy
       ${CMAKE_CURRENT_LIST_DIR}/../../system/flatbuffers/flatbuffers/include)

  # Append BECO include directories if they were collected
  if(DEFINED BECO_INCLUDE_DIRS)
    list(APPEND INCDIR ${BECO_INCLUDE_DIRS})
  endif()

  # Append Xtensa nnlib-hifi4 include directory if defined
  if(DEFINED TFLITE_MICRO_XTENSA_INCLUDE)
    list(APPEND INCDIR ${TFLITE_MICRO_XTENSA_INCLUDE})
  endif()

  if(CONFIG_MLEARNING_CMSIS_NN)
    list(APPEND INCDIR ${CMAKE_BINARY_DIR}/apps/mlearning/cmsis-nn/cmsis-nn)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/../cmsis-nn/cmsis-nn)
    list(APPEND INCDIR ${CMAKE_CURRENT_LIST_DIR}/../cmsis-nn/cmsis-nn/Include)
  endif()

  # ############################################################################
  # Library Configuration
  # ############################################################################

  nuttx_add_library(tflite_micro STATIC)
  target_compile_options(tflite_micro PRIVATE ${COMMON_FLAGS})
  target_sources(tflite_micro PRIVATE ${TFLITE_MICRO_SRCS})
  target_include_directories(tflite_micro PUBLIC ${INCDIR})

  # ############################################################################
  # Application Configuration
  # ############################################################################

  if(CONFIG_TFLITEMICRO_TOOL)
    nuttx_add_application(
      NAME
      tflm
      STACKSIZE
      ${CONFIG_TFLITEMICRO_TOOL_STACKSIZE}
      PRIORITY
      ${CONFIG_TFLITEMICRO_TOOL_PRIORITY}
      MODULE
      ${CONFIG_TFLITEMICRO_TOOL}
      SRCS
      tflm_tool.cc)
    target_include_directories(apps_tflm PUBLIC ${INCDIR})
    target_compile_options(apps_tflm PRIVATE ${COMMON_FLAGS})
  endif()

endif()
