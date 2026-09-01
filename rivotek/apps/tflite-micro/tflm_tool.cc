/****************************************************************************
 * apps/mlearning/tflite-micro/tflm_tool.cc
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <fstream>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler.h"

#define ALIGNMENT   16

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void usage(void)
{
  printf("\nUtility to use tflite micro on nuttx.\n"
    "[ -C       ] Compile tflite model into c++ codes.\n"
    "[ -E       ] Do once evaluation (for profiling).\n"
    "[ -i <str> ] Readable model file path.\n"
    "[ -o <str> ] Writable c++ file path.\n"
    "[ -p <str> ] Prefix of compiled code.\n"
    "[ -a <int> ] Arena size (mempool).\n"
    "[ -h       ] Print this message.\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern "C" int main(int argc, FAR char* argv[])
{
  const char* modelFileName = NULL;
  const char* codeFileName = NULL;
  const char* prefix = "NXAI";
  bool need_compile = false;
  bool need_invoke = false;
  int arenaSize = 1024 * 500;

  uint8_t* model_buf_original = NULL;
  uint8_t* arena_buf_original = NULL;
  int ret = -1;

  int ch;
  while ((ch = getopt(argc, argv, "CEhi:o:p:a:")) != EOF)
    {
      switch (ch)
        {
          case 'C':
            need_compile = true;
            need_invoke = false;
            break;
          case 'E':
            need_invoke = true;
            need_compile = false;
            break;
          case 'p':
            prefix = optarg;
            break;
          case 'i':
            modelFileName = optarg;
            break;
          case 'o':
            codeFileName = optarg;
            break;
          case 'a':
            arenaSize = atoi(optarg);
            if (arenaSize < 1024) {
              arenaSize = 1024 * 500;
            }
            printf("Arena size set to: %d\n", arenaSize);
            break;
          case 'h':
          default:
            usage();
            return -1;
        }
    }

  if (!modelFileName || !codeFileName)
    {
      usage();
      return -1;
    }

  printf("Loading model: %s\n", modelFileName);

  int fd = open(modelFileName, O_RDONLY);
  if (fd < 0) {
    printf("Error: Cannot open model file: %s\n", modelFileName);
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    printf("Error: Cannot stat model file\n");
    close(fd);
    return -1;
  }

  size_t modelSize = st.st_size;
  if (modelSize == 0) {
    printf("Error: Model file is empty\n");
    close(fd);
    return -1;
  }

  printf("Model file size: %zu bytes\n", modelSize);

  size_t model_buf_size = modelSize + ALIGNMENT;
  model_buf_original = (uint8_t*)malloc(model_buf_size);
  if (!model_buf_original) {
    printf("Error: Failed to allocate memory for model\n");
    close(fd);
    return -1;
  }

  uintptr_t model_ptr_val = (uintptr_t)model_buf_original;
  if (model_ptr_val % ALIGNMENT != 0) {
    model_ptr_val += (ALIGNMENT - (model_ptr_val % ALIGNMENT));
  }
  uint8_t* model_data = (uint8_t*)model_ptr_val;

  printf("Model buffer: 0x%p -> aligned 0x%p\n",
         model_buf_original, model_data);

  ssize_t bytes_read = read(fd, model_data, modelSize);
  close(fd);

  if (bytes_read != (ssize_t)modelSize) {
    printf("Error: Failed to read model file (read %zd of %zu bytes)\n",
           bytes_read, modelSize);
    free(model_buf_original);
    return -1;
  }

  printf("Model loaded: %zd bytes\n", bytes_read);

  tflite::MicroMutableOpResolver<28> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddQuantize();
  resolver.AddDequantize();
  resolver.AddMean();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddDepthwiseConv2D();
  resolver.AddCast();
  resolver.AddStridedSlice();
  resolver.AddConcatenation();
  resolver.AddMul();
  resolver.AddAdd();
  resolver.AddDiv();
  resolver.AddMinimum();
  resolver.AddMaximum();
  resolver.AddWindow();
  resolver.AddFftAutoScale();
  resolver.AddRfft();
  resolver.AddEnergy();
  resolver.AddFilterBank();
  resolver.AddFilterBankSquareRoot();
  resolver.AddFilterBankSpectralSubtraction();
  resolver.AddPCAN();
  resolver.AddFilterBankLog();

  printf("Operators registered\n");

  size_t arena_buf_size = arenaSize + ALIGNMENT;
  arena_buf_original = (uint8_t*)malloc(arena_buf_size);
  if (!arena_buf_original) {
    printf("Error: Failed to allocate arena\n");
    free(model_buf_original);
    return -1;
  }

  uintptr_t arena_ptr_val = (uintptr_t)arena_buf_original;
  if (arena_ptr_val % ALIGNMENT != 0) {
    arena_ptr_val += (ALIGNMENT - (arena_ptr_val % ALIGNMENT));
  }
  uint8_t* arena_data = (uint8_t*)arena_ptr_val;

  printf("Arena: 0x%p -> aligned 0x%p (size: %d)\n",
         arena_buf_original, arena_data, arenaSize);

  {
    tflite::MicroProfiler profiler;
    tflite::MicroInterpreter interpreter(tflite::GetModel(model_data),
      resolver, arena_data, arenaSize, nullptr,
      reinterpret_cast<tflite::MicroProfilerInterface*>(&profiler));

    printf("Interpreter created\n");

    TfLiteStatus status = interpreter.AllocateTensors();
    if (status != kTfLiteOk) {
      printf("Error: Failed to allocate tensors (status=%d)\n", status);
    } else {
      printf("Tensors allocated successfully\n");

      if (need_invoke)
        {
          printf("Initializing input tensors...\n");
          for (int i = 0; i < interpreter.inputs_size(); i++) {
            TfLiteTensor* input = interpreter.input(i);
            if (input != NULL && input->data.data != NULL) {
              memset(input->data.data, 0, input->bytes);
              printf("  Input[%d]: %zu bytes\n", i, input->bytes);
            }
          }

          printf("Running inference...\n");
          status = interpreter.Invoke();
          if (status != kTfLiteOk) {
            printf("Error: Inference failed (status=%d)\n", status);
          } else {
            printf("Inference completed\n");
            profiler.LogCsv();
            profiler.LogTicksPerTagCsv();
            ret = 0;
          }
        }

      if (need_compile)
        {
#ifdef TFLITE_MODEL_COMPILER
          std::ofstream ofs(codeFileName, std::ios::trunc);
          if (ofs.is_open()) {
            interpreter.Compile(ofs, prefix);
            ofs.close();
            ret = 0;
          } else {
            printf("Error: Cannot open output file: %s\n", codeFileName);
          }
#else
          printf("Not supported compiling %s.\n", prefix);
#endif
        }
    }
  }

  if (ret == 0) {
    printf("nxai done!\n");
  }

  if (model_buf_original) {
    free(model_buf_original);
  }
  if (arena_buf_original) {
    free(arena_buf_original);
  }
  return ret;
}
