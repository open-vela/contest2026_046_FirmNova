/****************************************************************************
 * vendor/allwinnertech/apps/tflite-micro/speech_recognition.cc
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

#include <nuttx/config.h>
#include <nuttx/audio/audio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include <cstdint>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "system/nxrecorder.h"

#define AUDIO_SAMPLE_RATE    16000
#define WINDOW_DURATION_MS   30
#define WINDOW_SIZE          (AUDIO_SAMPLE_RATE * WINDOW_DURATION_MS / 1000)
#define FEATURE_SIZE         40
#define FEATURE_COUNT        49

#define ARENA_SIZE_AUDIO_PREPROCESSOR   (1024 * 128)
#define ARENA_SIZE_MICRO_SPEECH         (1024 * 128)

#define AUDIO_DEVICE_PATH    "/dev/audio/pcm0c"
#define LED_DEVICE_PATH      "/dev/leds0"
#define AUDIO_PIPE_PATH      "/var/pipe/speech_audio"
#define KEYWORD_EVENT_PIPE   "/var/pipe/eca_voice_keyword"

#define ALIGNMENT   16

#define LED_COLOR_RED      0xFF0000
#define LED_COLOR_GREEN    0x00FF00
#define LED_COLOR_BLUE     0x0000FF
#define LED_COLOR_YELLOW   0xFFFF00
#define LED_COLOR_WHITE    0xFFFFFF
#define LED_COLOR_OFF      0x000000

#define CATEGORY_SILENCE   0
#define CATEGORY_UNKNOWN   1
#define CATEGORY_YES       2
#define CATEGORY_NO        3

#define CONFIDENCE_THRESHOLD  60.0f
#define COOLDOWN_MS          3000

#define AUDIO_CHANNELS    1
#define AUDIO_BPS         16
#define AUDIO_CHMAP       1

typedef int16_t audio_sample_t;

static int g_led_fd = -1;
static int g_led_available = 1;
static uint64_t g_last_trigger_time = 0;

static FAR struct nxrecorder_s *g_recorder = NULL;
static int g_audio_pipe_fd = -1;
static volatile bool g_recorder_running = false;

static uint64_t get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void usage(void)
{
  printf("\nSpeech Recognition Utility for NuttX.\n"
    "Usage: vela_speech_recog [options]\n"
    "Options:\n"
    "  -p <path>   Audio preprocessor model path\n"
    "  -s <path>   Micro speech model path\n"
    "  -d <path>   Audio device path\n"
    "  -m <mode>   Mode: 'real' or 'sim' (default: sim)\n"
    "  -l          Enable LED control (default: enabled)\n"
    "  -n          Disable LED control\n"
    "  -h          Show this help message\n"
    "\n"
    "Modes:\n"
    "  sim  - Simulation mode (uses predefined test patterns)\n"
    "  real - Real audio input mode (from /dev/audio/pcm0c)\n");
}

static const char* get_category_label(int index)
{
  static const char* labels[] = {"silence", "unknown", "yes", "no"};
  if (index >= 0 && index < 4) {
    return labels[index];
  }
  return "unknown";
}

static int load_model(const char* path, uint8_t** model_buf_original, 
                      uint8_t** model_data, size_t* model_size)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("Error: Cannot open model file: %s (errno=%d)\n", path, errno);
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    printf("Error: Cannot stat model file: %s\n", path);
    close(fd);
    return -1;
  }

  *model_size = st.st_size;
  if (*model_size == 0) {
    printf("Error: Model file is empty: %s\n", path);
    close(fd);
    return -1;
  }

  printf("Model file size: %zu bytes\n", *model_size);

  size_t alloc_size = *model_size + ALIGNMENT;
  *model_buf_original = (uint8_t*)malloc(alloc_size);
  if (!*model_buf_original) {
    printf("Error: Failed to allocate memory for model\n");
    close(fd);
    return -1;
  }

  uintptr_t model_ptr_val = (uintptr_t)*model_buf_original;
  if (model_ptr_val % ALIGNMENT != 0) {
    model_ptr_val += (ALIGNMENT - (model_ptr_val % ALIGNMENT));
  }
  *model_data = (uint8_t*)model_ptr_val;

  printf("Model buffer: 0x%p -> aligned 0x%p\n",
         *model_buf_original, *model_data);

  ssize_t bytes_read = read(fd, *model_data, *model_size);
  close(fd);

  if (bytes_read != (ssize_t)*model_size) {
    printf("Error: Failed to read model file (read %zd of %zu bytes)\n",
           bytes_read, *model_size);
    free(*model_buf_original);
    *model_buf_original = NULL;
    *model_data = NULL;
    return -1;
  }

  printf("Model loaded: %zd bytes\n", bytes_read);
  return 0;
}

static void led_init(void)
{
  if (g_led_fd >= 0) {
    return;
  }

  g_led_fd = open(LED_DEVICE_PATH, O_RDWR);
  if (g_led_fd < 0) {
    printf("Warning: Cannot open LED device %s (errno=%d)\n", 
           LED_DEVICE_PATH, errno);
    printf("LED control disabled\n");
    g_led_available = 0;
    return;
  }

  printf("LED device opened: %s\n", LED_DEVICE_PATH);
}

static int led_set_color(unsigned int color)
{
  if (!g_led_available) {
    return -1;
  }

  if (g_led_fd < 0) {
    led_init();
    if (g_led_fd < 0) {
      return -1;
    }
  }

  ssize_t written = write(g_led_fd, &color, sizeof(unsigned int));
  if (written != sizeof(unsigned int)) {
    return -1;
  }
  return 0;
}

static void led_cleanup(void)
{
  if (g_led_fd >= 0) {
    led_set_color(LED_COLOR_OFF);
    close(g_led_fd);
    g_led_fd = -1;
  }
}

static void led_test_sequence(void)
{
  if (!g_led_available) return;
  
  printf("LED Test: RED\n");
  led_set_color(LED_COLOR_RED);
  usleep(500000);
  
  printf("LED Test: GREEN\n");
  led_set_color(LED_COLOR_GREEN);
  usleep(500000);
  
  printf("LED Test: BLUE\n");
  led_set_color(LED_COLOR_BLUE);
  usleep(500000);
  
  printf("LED Test: OFF\n");
  led_set_color(LED_COLOR_OFF);
  usleep(200000);
}

static void generate_test_pattern(int pattern, int8_t* features, int rows, int cols)
{
  memset(features, 0, rows * cols);
  
  switch (pattern) {
    case 0:
      printf("\n[TEST] Pattern: SILENCE (all zeros)\n");
      for (int i = 0; i < rows * cols; i++) {
        features[i] = -128;
      }
      break;
      
    case 1:
      printf("\n[TEST] Pattern: YES\n");
      for (int i = 0; i < rows * cols; i++) {
        features[i] = (int8_t)(-100 + (rand() % 40));
      }
      for (int i = 5 * cols; i < 30 * cols; i += 2) {
        features[i] = (int8_t)(50 + (rand() % 60));
      }
      break;
      
    case 2:
      printf("\n[TEST] Pattern: NO\n");
      for (int i = 0; i < rows * cols; i++) {
        features[i] = (int8_t)(-100 + (rand() % 40));
      }
      for (int i = 10 * cols; i < 35 * cols; i += 2) {
        features[i] = (int8_t)(60 + (rand() % 50));
      }
      break;
      
    case 3:
      printf("\n[TEST] Pattern: UNKNOWN (noise)\n");
      for (int i = 0; i < rows * cols; i++) {
        features[i] = (int8_t)((rand() % 200) - 100);
      }
      break;
      
    default:
      printf("\n[TEST] Pattern: RANDOM\n");
      for (int i = 0; i < rows * cols; i++) {
        features[i] = (int8_t)((rand() % 256) - 128);
      }
      break;
  }
}

static int should_trigger(int category, float confidence)
{
  if (category != CATEGORY_YES && category != CATEGORY_NO) {
    return 0;
  }
  
  if (confidence < CONFIDENCE_THRESHOLD) {
    printf("Confidence too low (%.1f%% < %.1f%%)\n", 
           confidence, CONFIDENCE_THRESHOLD);
    return 0;
  }
  
  uint64_t now = get_time_ms();
  if (now - g_last_trigger_time < COOLDOWN_MS) {
    printf("Cooldown active (remaining: %lld ms)\n", 
           (long long)(COOLDOWN_MS - (now - g_last_trigger_time)));
    return 0;
  }
  
  g_last_trigger_time = now;
  return 1;
}

static void publish_keyword_event(int category, float confidence)
{
  char msg[48];
  int fd;
  int len;

  if (category != CATEGORY_YES && category != CATEGORY_NO) {
    return;
  }

  fd = open(KEYWORD_EVENT_PIPE, O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    return;
  }

  len = snprintf(msg, sizeof(msg), "%s %d dsp\n",
                 category == CATEGORY_YES ? "yes" : "no",
                 (int)(confidence + 0.5f));
  if (len > 0) {
    write(fd, msg, len);
  }

  close(fd);
}

static void handle_keyword(int category, float confidence, int use_led)
{
  printf("\n========================================\n");
  printf("Keyword detected: %s (%.1f%%)\n",
         get_category_label(category), confidence);
  printf("========================================\n");

  if (!should_trigger(category, confidence)) {
    if (use_led && g_led_available && confidence >= 30.0f) {
      led_set_color(LED_COLOR_BLUE);
      usleep(200000);
      led_set_color(LED_COLOR_OFF);
    }
    return;
  }

  switch (category) {
    case CATEGORY_YES:
      printf("\n>>> Wake-up: \"YES\" detected - LED ON (GREEN)\n");
      publish_keyword_event(category, confidence);
      if (use_led && g_led_available) {
        for (int i = 0; i < 3; i++) {
          led_set_color(LED_COLOR_GREEN);
          usleep(200000);
          led_set_color(LED_COLOR_OFF);
          usleep(200000);
        }
        led_set_color(LED_COLOR_GREEN);
      }
      break;

    case CATEGORY_NO:
      printf("\n>>> Wake-up: \"NO\" detected - LED OFF (RED)\n");
      publish_keyword_event(category, confidence);
      if (use_led && g_led_available) {
        for (int i = 0; i < 2; i++) {
          led_set_color(LED_COLOR_RED);
          usleep(200000);
          led_set_color(LED_COLOR_OFF);
          usleep(200000);
        }
        led_set_color(LED_COLOR_OFF);
      }
      break;

    case CATEGORY_SILENCE:
      printf("\n>>> Silence detected\n");
      if (use_led && g_led_available) {
        led_set_color(LED_COLOR_OFF);
      }
      break;

    case CATEGORY_UNKNOWN:
      printf("\n>>> Unknown sound detected\n");
      if (use_led && g_led_available) {
        led_set_color(LED_COLOR_YELLOW);
        usleep(300000);
        led_set_color(LED_COLOR_OFF);
      }
      break;

    default:
      printf("\n>>> Unknown category: %d\n", category);
      break;
  }
}

static int audio_init(const char *audio_device)
{
  struct stat st;
  int ret;

  printf("Initializing audio recorder...\n");

  g_recorder = nxrecorder_create();
  if (!g_recorder) {
    printf("Error: Failed to create recorder\n");
    return -1;
  }

  if (stat("/var/pipe", &st) < 0) {
    printf("Warning: /var/pipe not found, creating...\n");
    mkdir("/var/pipe", 0777);
  }

  unlink(AUDIO_PIPE_PATH);
  ret = mkfifo(AUDIO_PIPE_PATH, 0666);
  if (ret < 0 && errno != EEXIST) {
    printf("Error: mkfifo failed (errno=%d)\n", errno);
    nxrecorder_release(g_recorder);
    g_recorder = NULL;
    return -1;
  }
  printf("Audio FIFO created: %s\n", AUDIO_PIPE_PATH);

  g_audio_pipe_fd = open(AUDIO_PIPE_PATH, O_RDONLY | O_NONBLOCK);
  if (g_audio_pipe_fd < 0) {
    printf("Error: Failed to open pipe for reading (errno=%d)\n", errno);
    nxrecorder_release(g_recorder);
    g_recorder = NULL;
    unlink(AUDIO_PIPE_PATH);
    return -1;
  }
  printf("Audio pipe opened for reading (non-blocking)\n");

  nxrecorder_setdevice(g_recorder, audio_device);
  printf("Audio device set: %s\n", audio_device);

  ret = nxrecorder_recordinternal(g_recorder,
                                   AUDIO_PIPE_PATH,
                                   AUDIO_FMT_PCM,
                                   AUDIO_CHANNELS,
                                   AUDIO_BPS,
                                   AUDIO_SAMPLE_RATE,
                                   AUDIO_CHMAP);
  if (ret != OK) {
    printf("Error: Failed to start recorder (ret=%d)\n", ret);
    close(g_audio_pipe_fd);
    g_audio_pipe_fd = -1;
    nxrecorder_release(g_recorder);
    g_recorder = NULL;
    unlink(AUDIO_PIPE_PATH);
    return -1;
  }

  printf("Recorder started\n");
  g_recorder_running = true;
  return 0;
}

static void audio_cleanup(void)
{
  printf("Cleaning up audio...\n");

  if (g_audio_pipe_fd >= 0) {
    close(g_audio_pipe_fd);
    g_audio_pipe_fd = -1;
  }

  if (g_recorder) {
    nxrecorder_stop(g_recorder);
    usleep(100000);
    nxrecorder_release(g_recorder);
    g_recorder = NULL;
  }

  unlink(AUDIO_PIPE_PATH);
  g_recorder_running = false;
  printf("Audio cleanup complete\n");
}

static ssize_t audio_read_blocking(int16_t *buffer, size_t samples)
{
  size_t bytes_needed = samples * sizeof(int16_t);
  size_t bytes_read = 0;
  int retry_count = 0;

  while (bytes_read < bytes_needed && retry_count < 1000) {
    ssize_t n = read(g_audio_pipe_fd, 
                     (uint8_t*)buffer + bytes_read, 
                     bytes_needed - bytes_read);
    
    if (n > 0) {
      bytes_read += n;
      retry_count = 0;
    } else if (n == 0) {
      usleep(1000);
      retry_count++;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(1000);
        retry_count++;
      } else {
        printf("Error: Audio read failed (errno=%d)\n", errno);
        return -1;
      }
    }
  }

  if (retry_count >= 1000) {
    return 0;
  }

  return bytes_read;
}

extern "C" int main(int argc, FAR char* argv[])
{
  const char* preprocessor_path = "/data/audio_preprocessor_int8.tflite";
  const char* speech_path = "/data/micro_speech_quantized.tflite";
  const char* audio_device = AUDIO_DEVICE_PATH;
  const char* mode = "sim";
  int use_led = 1;
  int use_real_audio = 0;
  int ret = -1;

  int ch;
  while ((ch = getopt(argc, argv, "p:s:d:m:lnh")) != EOF)
    {
      switch (ch)
        {
          case 'p':
            preprocessor_path = optarg;
            break;
          case 's':
            speech_path = optarg;
            break;
          case 'd':
            audio_device = optarg;
            break;
          case 'm':
            mode = optarg;
            if (strcmp(mode, "real") == 0) {
              use_real_audio = 1;
            } else {
              use_real_audio = 0;
            }
            break;
          case 'l':
            use_led = 1;
            break;
          case 'n':
            use_led = 0;
            break;
          case 'h':
          default:
            usage();
            return 0;
        }
    }

  printf("\n========================================\n");
  printf("  Speech Recognition\n");
  printf("========================================\n");
  printf("Mode: %s\n", use_real_audio ? "Real Audio" : "Simulation");
  printf("Audio device: %s\n", audio_device);
  printf("Preprocessor model: %s\n", preprocessor_path);
  printf("Speech model: %s\n", speech_path);
  printf("LED Control: %s\n", use_led ? "Enabled" : "Disabled");
  printf("Confidence Threshold: %.1f%%\n", CONFIDENCE_THRESHOLD);
  printf("Cooldown: %d ms\n", COOLDOWN_MS);
  printf("========================================\n\n");

  if (use_led) {
    led_init();
    if (g_led_available) {
      printf("Testing LED colors...\n");
      led_test_sequence();
      printf("LED test completed\n\n");
    }
  }

  uint8_t* preprocessor_buf_original = NULL;
  uint8_t* preprocessor_data = NULL;
  size_t preprocessor_size = 0;

  uint8_t* speech_buf_original = NULL;
  uint8_t* speech_data = NULL;
  size_t speech_size = 0;

  uint8_t* preprocessor_arena_original = NULL;
  uint8_t* speech_arena_original = NULL;

  printf("Loading models...\n\n");

  if (load_model(preprocessor_path, &preprocessor_buf_original, 
                 &preprocessor_data, &preprocessor_size) < 0) {
    printf("Error: Failed to load preprocessor model\n");
    led_cleanup();
    return -1;
  }

  printf("\n");

  if (load_model(speech_path, &speech_buf_original, 
                 &speech_data, &speech_size) < 0) {
    printf("Error: Failed to load speech model\n");
    if (preprocessor_buf_original) free(preprocessor_buf_original);
    led_cleanup();
    return -1;
  }

  printf("\nRegistering operators...\n");

  {
    tflite::MicroMutableOpResolver<20> preprocessor_resolver;
    preprocessor_resolver.AddReshape();
    preprocessor_resolver.AddCast();
    preprocessor_resolver.AddStridedSlice();
    preprocessor_resolver.AddConcatenation();
    preprocessor_resolver.AddMul();
    preprocessor_resolver.AddAdd();
    preprocessor_resolver.AddDiv();
    preprocessor_resolver.AddMinimum();
    preprocessor_resolver.AddMaximum();
    preprocessor_resolver.AddWindow();
    preprocessor_resolver.AddFftAutoScale();
    preprocessor_resolver.AddRfft();
    preprocessor_resolver.AddEnergy();
    preprocessor_resolver.AddFilterBank();
    preprocessor_resolver.AddFilterBankSquareRoot();
    preprocessor_resolver.AddFilterBankSpectralSubtraction();
    preprocessor_resolver.AddPCAN();
    preprocessor_resolver.AddFilterBankLog();

    tflite::MicroMutableOpResolver<8> speech_resolver;
    speech_resolver.AddReshape();
    speech_resolver.AddFullyConnected();
    speech_resolver.AddDepthwiseConv2D();
    speech_resolver.AddSoftmax();

    printf("Allocating arenas...\n");

    size_t arena_size = ARENA_SIZE_AUDIO_PREPROCESSOR + ALIGNMENT;
    preprocessor_arena_original = (uint8_t*)malloc(arena_size);
    if (!preprocessor_arena_original) {
      printf("Error: Failed to allocate preprocessor arena\n");
      if (preprocessor_buf_original) free(preprocessor_buf_original);
      if (speech_buf_original) free(speech_buf_original);
      led_cleanup();
      return -1;
    }

    uintptr_t arena_ptr = (uintptr_t)preprocessor_arena_original;
    if (arena_ptr % ALIGNMENT != 0) {
      arena_ptr += (ALIGNMENT - (arena_ptr % ALIGNMENT));
    }
    uint8_t* preprocessor_arena = (uint8_t*)arena_ptr;

    printf("Preprocessor arena: 0x%p -> aligned 0x%p (size: %d)\n",
           preprocessor_arena_original, preprocessor_arena, 
           ARENA_SIZE_AUDIO_PREPROCESSOR);

    arena_size = ARENA_SIZE_MICRO_SPEECH + ALIGNMENT;
    speech_arena_original = (uint8_t*)malloc(arena_size);
    if (!speech_arena_original) {
      printf("Error: Failed to allocate speech arena\n");
      if (preprocessor_buf_original) free(preprocessor_buf_original);
      if (speech_buf_original) free(speech_buf_original);
      if (preprocessor_arena_original) free(preprocessor_arena_original);
      led_cleanup();
      return -1;
    }

    arena_ptr = (uintptr_t)speech_arena_original;
    if (arena_ptr % ALIGNMENT != 0) {
      arena_ptr += (ALIGNMENT - (arena_ptr % ALIGNMENT));
    }
    uint8_t* speech_arena = (uint8_t*)arena_ptr;

    printf("Speech arena: 0x%p -> aligned 0x%p (size: %d)\n",
           speech_arena_original, speech_arena, ARENA_SIZE_MICRO_SPEECH);

    {
      printf("\nCreating interpreters...\n");

      tflite::MicroInterpreter preprocessor_interpreter(
          tflite::GetModel(preprocessor_data),
          preprocessor_resolver,
          preprocessor_arena,
          ARENA_SIZE_AUDIO_PREPROCESSOR);

      tflite::MicroInterpreter speech_interpreter(
          tflite::GetModel(speech_data),
          speech_resolver,
          speech_arena,
          ARENA_SIZE_MICRO_SPEECH);

      printf("Allocating tensors...\n");

      TfLiteStatus status = preprocessor_interpreter.AllocateTensors();
      if (status != kTfLiteOk) {
        printf("Error: Failed to allocate preprocessor tensors (status=%d)\n", status);
        if (preprocessor_buf_original) free(preprocessor_buf_original);
        if (speech_buf_original) free(speech_buf_original);
        if (preprocessor_arena_original) free(preprocessor_arena_original);
        if (speech_arena_original) free(speech_arena_original);
        led_cleanup();
        return -1;
      }

      status = speech_interpreter.AllocateTensors();
      if (status != kTfLiteOk) {
        printf("Error: Failed to allocate speech tensors (status=%d)\n", status);
        if (preprocessor_buf_original) free(preprocessor_buf_original);
        if (speech_buf_original) free(speech_buf_original);
        if (preprocessor_arena_original) free(preprocessor_arena_original);
        if (speech_arena_original) free(speech_arena_original);
        led_cleanup();
        return -1;
      }

      printf("Interpreters initialized successfully\n\n");

      TfLiteTensor* speech_input = speech_interpreter.input(0);
      TfLiteTensor* speech_output = speech_interpreter.output(0);
      
      if (!speech_input || !speech_output) {
        printf("Error: Cannot get speech tensors\n");
        if (preprocessor_buf_original) free(preprocessor_buf_original);
        if (speech_buf_original) free(speech_buf_original);
        if (preprocessor_arena_original) free(preprocessor_arena_original);
        if (speech_arena_original) free(speech_arena_original);
        led_cleanup();
        return -1;
      }

      printf("Speech model input size: %d bytes\n", speech_input->bytes);
      printf("Speech model output size: %d bytes\n", speech_output->bytes);

      if (use_real_audio) {
        printf("\nInitializing audio recorder...\n");
        if (audio_init(audio_device) < 0) {
          printf("Error: Failed to initialize audio, switching to simulation mode...\n");
          use_real_audio = 0;
        } else {
          printf("Audio recorder initialized successfully\n");
        }
      }

      printf("\nReady to detect keywords...\n");
      if (use_real_audio) {
        printf("Say 'yes' or 'no' near the microphone\n");
      } else {
        printf("Running in SIMULATION mode\n");
        printf("Test patterns: SILENCE -> YES -> NO -> UNKNOWN -> RANDOM\n");
      }
      printf("Press Ctrl+C to exit\n\n");

      srand(time(NULL));

      int8_t features[FEATURE_COUNT][FEATURE_SIZE] = {0};
      int loop_count = 0;
      int test_pattern = 0;

      while (true)
        {
          if (use_real_audio) {
            audio_sample_t audio_buffer[WINDOW_SIZE];
            
            ssize_t nread = audio_read_blocking(audio_buffer, WINDOW_SIZE);
            
            if (nread < 0) {
              printf("Error: Audio read failed\n");
              break;
            }
            
            if (nread == 0) {
              usleep(10000);
              continue;
            }

            TfLiteTensor* preprocessor_input = preprocessor_interpreter.input(0);
            if (!preprocessor_input) {
              printf("Error: Cannot get preprocessor input tensor\n");
              break;
            }

            int16_t* input_data = reinterpret_cast<int16_t*>(preprocessor_input->data.data);
            memcpy(input_data, audio_buffer, WINDOW_SIZE * sizeof(audio_sample_t));

            status = preprocessor_interpreter.Invoke();
            if (status != kTfLiteOk) {
              printf("Error: Preprocessor inference failed\n");
              break;
            }

            TfLiteTensor* preprocessor_output = preprocessor_interpreter.output(0);
            if (!preprocessor_output) {
              printf("Error: Cannot get preprocessor output tensor\n");
              break;
            }

            int feature_index = loop_count % FEATURE_COUNT;
            memcpy(features[feature_index], preprocessor_output->data.int8, FEATURE_SIZE);

            if (feature_index == FEATURE_COUNT - 1) {
              memcpy(speech_input->data.int8, features, sizeof(features));
              
              status = speech_interpreter.Invoke();
              if (status != kTfLiteOk) {
                printf("Error: Speech inference failed\n");
                break;
              }

              int max_index = 0;
              int8_t max_prob = speech_output->data.int8[0];
              for (int i = 1; i < 4; i++) {
                if (speech_output->data.int8[i] > max_prob) {
                  max_prob = speech_output->data.int8[i];
                  max_index = i;
                }
              }

              float prob_percent = (max_prob / 127.0f) * 100.0f;
              handle_keyword(max_index, prob_percent, use_led);
            }
          } else {
            generate_test_pattern(test_pattern, (int8_t*)features, 
                                  FEATURE_COUNT, FEATURE_SIZE);
            memcpy(speech_input->data.int8, features, sizeof(features));

            status = speech_interpreter.Invoke();
            if (status != kTfLiteOk) {
              printf("Error: Speech inference failed\n");
              break;
            }

            int max_index = 0;
            int8_t max_prob = speech_output->data.int8[0];
            
            printf("Output probabilities:\n");
            for (int i = 0; i < 4; i++) {
              int8_t prob = speech_output->data.int8[i];
              float percent = (prob / 127.0f) * 100.0f;
              printf("  [%d] %s: %d (%.1f%%)%s\n",
                     i, get_category_label(i), prob, percent,
                     (i == max_index) ? " <-- max" : "");
              if (prob > max_prob) {
                max_prob = prob;
                max_index = i;
              }
            }

            float prob_percent = (max_prob / 127.0f) * 100.0f;
            handle_keyword(max_index, prob_percent, use_led);

            test_pattern = (test_pattern + 1) % 5;
            
            printf("\nWaiting 3 seconds...\n");
            usleep(3000000);
          }

          loop_count++;
        }

      ret = 0;
    }
  }

  printf("\nCleaning up...\n");

  if (use_real_audio) {
    audio_cleanup();
  }

  if (preprocessor_arena_original) {
    free(preprocessor_arena_original);
  }
  if (speech_arena_original) {
    free(speech_arena_original);
  }

  if (preprocessor_buf_original) {
    free(preprocessor_buf_original);
  }
  if (speech_buf_original) {
    free(speech_buf_original);
  }

  led_cleanup();

  printf("Speech recognition stopped\n");
  return ret;
}
