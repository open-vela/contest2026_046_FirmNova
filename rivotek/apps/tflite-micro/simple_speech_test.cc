/****************************************************************************
 * apps/mlearning/tflite-micro/simple_speech_test.cc
 *
 * Simple test for micro_speech model with LED control
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include <cstdint>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#define ARENA_SIZE  (1024 * 128)
#define ALIGNMENT   16

#define CATEGORY_SILENCE   0
#define CATEGORY_UNKNOWN   1
#define CATEGORY_YES       2
#define CATEGORY_NO        3

#define LED_COLOR_RED      0xFF0000
#define LED_COLOR_GREEN    0x00FF00
#define LED_COLOR_BLUE     0x0000FF
#define LED_COLOR_YELLOW   0xFFFF00
#define LED_COLOR_WHITE    0xFFFFFF
#define LED_COLOR_OFF      0x000000

#define LED_DEVICE_PATH    "/dev/leds0"

#define CONFIDENCE_THRESHOLD  50.0f

static int g_led_fd = -1;
static int g_led_available = 1;

static void usage(void)
{
  printf("\nKeyword Wake-up Test Utility\n"
    "Usage: speech_test [options]\n"
    "Options:\n"
    "  -m <path>   Model file path (default: /data/micro_speech_quantized.tflite)\n"
    "  -l          Enable LED control (default: enabled)\n"
    "  -n          Disable LED control\n"
    "  -c <count>  Loop count (0=infinite, default: 5)\n"
    "  -t <ms>     Interval between tests (ms, default: 2000)\n"
    "  -s          Run single test (no loop)\n"
    "  -h          Show this help message\n");
}

static const char* get_category_label(int index)
{
  static const char* labels[] = {"silence", "unknown", "yes", "no"};
  if (index >= 0 && index < 4) {
    return labels[index];
  }
  return "unknown";
}

static int led_init(void)
{
  if (g_led_fd >= 0) {
    return 0;
  }

  g_led_fd = open(LED_DEVICE_PATH, O_RDWR);
  if (g_led_fd < 0) {
    printf("Warning: Cannot open LED device %s (errno=%d)\n", 
           LED_DEVICE_PATH, errno);
    printf("LED control disabled\n");
    g_led_available = 0;
    return -1;
  }

  printf("LED device opened: %s\n", LED_DEVICE_PATH);
  return 0;
}

static int led_set_color(unsigned int color)
{
  if (!g_led_available) {
    return -1;
  }

  if (g_led_fd < 0) {
    if (led_init() < 0) {
      return -1;
    }
  }

  ssize_t written = write(g_led_fd, &color, sizeof(unsigned int));
  if (written != sizeof(unsigned int)) {
    printf("Warning: Failed to write LED color (errno=%d)\n", errno);
    return -1;
  }
  return 0;
}

static void led_on(void)
{
  printf("LED: ON (WHITE)\n");
  led_set_color(LED_COLOR_WHITE);
}

static void led_off(void)
{
  printf("LED: OFF\n");
  led_set_color(LED_COLOR_OFF);
}

static void led_blink(int times, int delay_ms, unsigned int color)
{
  printf("LED: Blinking %d times\n", times);
  for (int i = 0; i < times; i++) {
    led_set_color(color);
    usleep(delay_ms * 500);
    led_set_color(LED_COLOR_OFF);
    usleep(delay_ms * 500);
  }
}

static void led_cleanup(void)
{
  if (g_led_fd >= 0) {
    led_set_color(LED_COLOR_OFF);
    close(g_led_fd);
    g_led_fd = -1;
  }
}

static void handle_keyword(int category, float confidence, int use_led)
{
  printf("\n========================================\n");
  printf("Keyword detected: %s (%.1f%%)\n",
         get_category_label(category), confidence);
  printf("========================================\n");

  if (confidence < CONFIDENCE_THRESHOLD) {
    printf("Confidence too low (<%.1f%%), ignoring\n",
           CONFIDENCE_THRESHOLD);
    if (use_led && g_led_available) {
      led_set_color(LED_COLOR_BLUE);
      usleep(500000);
      led_set_color(LED_COLOR_OFF);
    }
    return;
  }

  switch (category) {
    case CATEGORY_YES:
      printf("\n>>> Wake-up: \"YES\" detected - Turning ON LED\n");
      if (use_led && g_led_available) {
        led_blink(3, 200, LED_COLOR_GREEN);
        led_on();
      }
      break;

    case CATEGORY_NO:
      printf("\n>>> Wake-up: \"NO\" detected - Turning OFF LED\n");
      if (use_led && g_led_available) {
        led_blink(2, 200, LED_COLOR_RED);
        led_off();
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
        usleep(200000);
        led_set_color(LED_COLOR_OFF);
      }
      break;

    default:
      printf("\n>>> Unknown category: %d\n", category);
      break;
  }
}

static void simulate_input_pattern(int pattern, int8_t* input_data, size_t input_size)
{
  memset(input_data, 0, input_size);
  
  switch (pattern) {
    case 0:
      printf("Simulating: SILENCE (all zeros)\n");
      break;

    case 1:
      printf("Simulating: \"YES\" pattern\n");
      for (size_t i = 0; i < input_size; i += 40) {
        input_data[i] = (int8_t)(127 * 0.3f);
        if (i + 1 < input_size) input_data[i + 1] = (int8_t)(127 * 0.2f);
      }
      break;

    case 2:
      printf("Simulating: \"NO\" pattern\n");
      for (size_t i = 10; i < input_size; i += 50) {
        input_data[i] = (int8_t)(127 * 0.4f);
        if (i + 1 < input_size) input_data[i + 1] = (int8_t)(127 * 0.3f);
        if (i + 2 < input_size) input_data[i + 2] = (int8_t)(127 * 0.2f);
      }
      break;

    case 3:
      printf("Simulating: Random noise (UNKNOWN)\n");
      for (size_t i = 0; i < input_size; i++) {
        input_data[i] = (int8_t)((rand() % 100) - 50);
      }
      break;

    default:
      printf("Simulating: Random pattern\n");
      for (size_t i = 0; i < input_size; i++) {
        input_data[i] = (int8_t)((rand() % 127) - 63);
      }
      break;
  }
}

extern "C" int main(int argc, FAR char* argv[])
{
  const char* model_path = "/data/micro_speech_quantized.tflite";
  uint8_t* model_buf_original = NULL;
  uint8_t* arena_buf_original = NULL;
  int ret = -1;
  int use_led = 1;
  int loop_count = 5;
  int interval_ms = 2000;
  int single_test = 0;

  int ch;
  while ((ch = getopt(argc, argv, "m:lnc:t:sh")) != EOF)
    {
      switch (ch)
        {
          case 'm':
            model_path = optarg;
            break;
          case 'l':
            use_led = 1;
            break;
          case 'n':
            use_led = 0;
            break;
          case 'c':
            loop_count = atoi(optarg);
            break;
          case 't':
            interval_ms = atoi(optarg);
            break;
          case 's':
            single_test = 1;
            break;
          case 'h':
          default:
            usage();
            return 0;
        }
    }

  printf("\n========================================\n");
  printf("  Keyword Wake-up Test with LED Control\n");
  printf("========================================\n");
  printf("Model: %s\n", model_path);
  printf("LED Control: %s\n", use_led ? "Enabled" : "Disabled");
  if (!single_test) {
    printf("Loop Count: %d\n", loop_count);
    printf("Interval: %d ms\n", interval_ms);
  }
  printf("========================================\n\n");

  if (use_led) {
    led_init();
    if (g_led_available) {
      printf("Testing LED...\n");
      led_set_color(LED_COLOR_RED);
      usleep(200000);
      led_set_color(LED_COLOR_GREEN);
      usleep(200000);
      led_set_color(LED_COLOR_BLUE);
      usleep(200000);
      led_set_color(LED_COLOR_OFF);
      printf("LED test completed\n");
    }
  }

  printf("Loading model: %s\n", model_path);

  int fd = open(model_path, O_RDONLY);
  if (fd < 0) {
    printf("Error: Cannot open model file: %s\n", model_path);
    if (use_led) led_cleanup();
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    printf("Error: Cannot stat model file\n");
    close(fd);
    if (use_led) led_cleanup();
    return -1;
  }

  size_t model_size = st.st_size;
  if (model_size == 0) {
    printf("Error: Model file is empty\n");
    close(fd);
    if (use_led) led_cleanup();
    return -1;
  }

  printf("Model file size: %zu bytes\n", model_size);

  size_t model_buf_size = model_size + ALIGNMENT;
  model_buf_original = (uint8_t*)malloc(model_buf_size);
  if (!model_buf_original) {
    printf("Error: Failed to allocate memory for model\n");
    close(fd);
    if (use_led) led_cleanup();
    return -1;
  }

  uintptr_t model_ptr_val = (uintptr_t)model_buf_original;
  if (model_ptr_val % ALIGNMENT != 0) {
    model_ptr_val += (ALIGNMENT - (model_ptr_val % ALIGNMENT));
  }
  uint8_t* model_data = (uint8_t*)model_ptr_val;

  printf("Model buffer: 0x%p -> aligned 0x%p\n",
         model_buf_original, model_data);

  ssize_t bytes_read = read(fd, model_data, model_size);
  close(fd);

  if (bytes_read != (ssize_t)model_size) {
    printf("Error: Failed to read model file (read %zd of %zu bytes)\n",
           bytes_read, model_size);
    free(model_buf_original);
    if (use_led) led_cleanup();
    return -1;
  }

  printf("Model loaded: %zd bytes\n", bytes_read);

  tflite::MicroMutableOpResolver<20> resolver;
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddDepthwiseConv2D();
  resolver.AddSoftmax();
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddQuantize();
  resolver.AddDequantize();
  resolver.AddMean();
  resolver.AddCast();
  resolver.AddStridedSlice();
  resolver.AddConcatenation();
  resolver.AddMul();
  resolver.AddAdd();
  resolver.AddDiv();
  resolver.AddMinimum();
  resolver.AddMaximum();

  printf("Operators registered\n");

  size_t arena_buf_size = ARENA_SIZE + ALIGNMENT;
  arena_buf_original = (uint8_t*)malloc(arena_buf_size);
  if (!arena_buf_original) {
    printf("Error: Failed to allocate arena\n");
    free(model_buf_original);
    if (use_led) led_cleanup();
    return -1;
  }

  uintptr_t arena_ptr_val = (uintptr_t)arena_buf_original;
  if (arena_ptr_val % ALIGNMENT != 0) {
    arena_ptr_val += (ALIGNMENT - (arena_ptr_val % ALIGNMENT));
  }
  uint8_t* arena_data = (uint8_t*)arena_ptr_val;

  printf("Arena: 0x%p -> aligned 0x%p (size: %d)\n",
         arena_buf_original, arena_data, ARENA_SIZE);

  {
    tflite::MicroInterpreter interpreter(
        tflite::GetModel(model_data),
        resolver,
        arena_data,
        ARENA_SIZE);

    printf("Interpreter created\n");

    TfLiteStatus status = interpreter.AllocateTensors();
    if (status != kTfLiteOk) {
      printf("Error: Failed to allocate tensors (status=%d)\n", status);
    } else {
      printf("Tensors allocated successfully\n");

      TfLiteTensor* input = interpreter.input(0);
      TfLiteTensor* output = interpreter.output(0);

      if (input) {
        printf("Input tensor: type=%d, bytes=%zu, dims=%d\n",
               input->type, input->bytes, input->dims->size);
        for (int i = 0; i < input->dims->size; i++) {
          printf("  dim[%d] = %d\n", i, input->dims->data[i]);
        }
      }

      if (output) {
        printf("Output tensor: type=%d, bytes=%zu, dims=%d\n",
               output->type, output->bytes, output->dims->size);
      }

      if (!input || !output || !input->data.data || !output->data.data) {
        printf("Error: Invalid input/output tensors\n");
      } else {
        size_t input_size = input->bytes;
        int8_t* input_data = (int8_t*)input->data.data;

        if (single_test) {
          printf("\n--- Running single test ---\n");
          memset(input_data, 0, input_size);
          printf("Input initialized with zeros\n");

          printf("Running inference...\n");
          status = interpreter.Invoke();
          if (status != kTfLiteOk) {
            printf("Error: Inference failed (status=%d)\n", status);
          } else {
            printf("Inference completed successfully\n");
            int max_index = 0;
            int8_t max_prob = output->data.int8[0];
            for (int i = 0; i < 4; i++) {
              if (output->data.int8[i] > max_prob) {
                max_prob = output->data.int8[i];
                max_index = i;
              }
            }
            float confidence = (max_prob / 127.0f) * 100.0f;
            handle_keyword(max_index, confidence, use_led);
            ret = 0;
          }
        } else {
          printf("\n--- Starting loop test ---\n");
          printf("Test patterns: SILENCE -> YES -> NO -> UNKNOWN -> RANDOM\n");
          printf("\n");

          for (int loop = 0; loop < loop_count || loop_count == 0; loop++) {
            if (loop_count > 0) {
              printf("\n========================================\n");
              printf("Loop %d/%d\n", loop + 1, loop_count);
              printf("========================================\n");
            } else {
              printf("\n========================================\n");
              printf("Loop %d (infinite)\n", loop + 1);
              printf("========================================\n");
            }

            int pattern = loop % 5;
            simulate_input_pattern(pattern, input_data, input_size);

            printf("Running inference...\n");
            status = interpreter.Invoke();
            if (status != kTfLiteOk) {
              printf("Error: Inference failed (status=%d)\n", status);
              continue;
            }

            int max_index = 0;
            int8_t max_prob = output->data.int8[0];
            
            printf("\nOutput probabilities:\n");
            for (int i = 0; i < 4; i++) {
              int8_t prob = output->data.int8[i];
              float percent = (prob / 127.0f) * 100.0f;
              printf("  [%d] %s: %d (%.1f%%)%s\n",
                     i, get_category_label(i), prob, percent,
                     (i == max_index) ? " <-- max" : "");
              if (prob > max_prob) {
                max_prob = prob;
                max_index = i;
              }
            }

            float confidence = (max_prob / 127.0f) * 100.0f;
            handle_keyword(max_index, confidence, use_led);

            ret = 0;

            if (loop_count > 0 && loop == loop_count - 1) {
              break;
            }

            printf("\nWaiting %d ms before next test...\n", interval_ms);
            usleep(interval_ms * 1000);
          }

          printf("\n========================================\n");
          printf("Test completed!\n");
          printf("========================================\n");
        }
      }
    }
  }

  if (use_led) {
    printf("\nCleaning up - Turning off LED\n");
    led_cleanup();
  }

  if (model_buf_original) {
    free(model_buf_original);
  }
  if (arena_buf_original) {
    free(arena_buf_original);
  }
  return ret;
}
