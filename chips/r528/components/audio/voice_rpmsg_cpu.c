#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <hal_mem.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <openamp/rpmsg.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>

#define VOICE_RPMSG_EPT_NAME "voice_recognition"
#define VOICE_RPMSG_MAGIC 0x564F4943
#define VOICE_KEYWORD_EVENT_PIPE "/var/pipe/eca_voice_keyword"

#ifndef CONFIG_VOICE_RPMSG_DEBUG
#define CONFIG_VOICE_RPMSG_DEBUG 0
#endif

#define VOICE_LOG(level, fmt, ...) \
    do { \
        if (CONFIG_VOICE_RPMSG_DEBUG) { \
            syslog(level, "voice_rpmsg: " fmt, ##__VA_ARGS__); \
        } \
    } while (0)

typedef enum {
    VOICE_EVENT_SPEECH_START = 0x01,
    VOICE_EVENT_SPEECH_END = 0x02,
    VOICE_EVENT_KEYWORD_DETECTED = 0x03,
    VOICE_EVENT_ERROR = 0x04,
    VOICE_EVENT_STATUS = 0x05,
    VOICE_EVENT_STATS = 0x06
} voice_rpmsg_event_t;

typedef enum {
    VOICE_KEYWORD_SILENCE = 0,
    VOICE_KEYWORD_UNKNOWN = 1,
    VOICE_KEYWORD_YES = 2,
    VOICE_KEYWORD_NO = 3,
    VOICE_KEYWORD_WAKEUP = 4,
    VOICE_KEYWORD_CUSTOM = 5,
    VOICE_KEYWORD_AED = 6
} voice_keyword_t;

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t sequence;
    uint32_t timestamp;
} voice_rpmsg_header_t;

typedef struct {
    voice_rpmsg_header_t header;
    voice_rpmsg_event_t event;
    uint64_t timestamp;
    union {
        struct {
            voice_keyword_t keyword;
            float confidence;
        } keyword_data;
        struct {
            uint32_t duration_ms;
        } speech_data;
        struct {
            uint32_t error_code;
            char error_message[32];
        } error_data;
    };
} voice_rpmsg_event_msg_t;

static struct rpmsg_endpoint *g_voice_ept = NULL;
static bool g_led_is_on = false;

static void publish_keyword_event(const char *keyword, float confidence)
{
    char message[48];
    float confidence_percent = confidence;
    ssize_t written;
    int fd;
    int length;

    if (!(confidence_percent >= 0.0f)) {
        confidence_percent = 0.0f;
    } else if (confidence_percent <= 1.0f) {
        confidence_percent *= 100.0f;
    } else if (confidence_percent > 100.0f) {
        confidence_percent = 100.0f;
    }

    fd = open(VOICE_KEYWORD_EVENT_PIPE, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        VOICE_LOG(LOG_WARNING, "open keyword event pipe failed, errno=%d\n", errno);
        return;
    }

    length = snprintf(message, sizeof(message), "%s %d dsp\n", keyword,
            (int)(confidence_percent + 0.5f));
    if (length <= 0 || (size_t)length >= sizeof(message)) {
        VOICE_LOG(LOG_ERR, "format keyword event failed\n");
        close(fd);
        return;
    }

    written = write(fd, message, (size_t)length);
    if (written != length) {
        VOICE_LOG(LOG_WARNING, "write keyword event failed, ret=%ld errno=%d\n",
                (long)written, errno);
    } else {
        VOICE_LOG(LOG_INFO, "keyword event published: %s", message);
    }
    close(fd);
}

static void led_set_green(void)
{
    VOICE_LOG(LOG_INFO, "led_set_green called\n");
#ifdef CONFIG_LED_RGB_WS2812
    VOICE_LOG(LOG_INFO, "using WS2812 file interface\n");
    int fd = open("/dev/leds0", O_RDWR);
    if (fd >= 0) {
        VOICE_LOG(LOG_INFO, "opened /dev/leds0 successfully, fd=%d\n", fd);
        unsigned int green = 0x00FF00;
        VOICE_LOG(LOG_INFO, "setting LED color to green (0x%06X)\n", green);
        int ret = write(fd, &green, sizeof(int));
        VOICE_LOG(LOG_INFO, "write returned %d, expected %zu\n", ret, sizeof(int));
        if (ret != sizeof(int)) {
            VOICE_LOG(LOG_ERR, "write failed, errno=%d\n", errno);
        }
        close(fd);
        VOICE_LOG(LOG_INFO, "closed /dev/leds0\n");
    } else {
        VOICE_LOG(LOG_ERR, "failed to open /dev/leds0, errno=%d\n", errno);
    }
#else
    VOICE_LOG(LOG_INFO, "using LEDC interface\n");
    hal_ledc_init();
    unsigned int rgb_data = 0x00FF00;
    int ret = sunxi_set_led_brightness(0, rgb_data);
    VOICE_LOG(LOG_INFO, "sunxi_set_led_brightness(0, 0x%06X) returned %d\n", rgb_data, ret);
#endif
    g_led_is_on = true;
    VOICE_LOG(LOG_INFO, "Green LED ON\n");
}

static void led_set_off(void)
{
    VOICE_LOG(LOG_INFO, "led_set_off called\n");
#ifdef CONFIG_LED_RGB_WS2812
    int fd = open("/dev/leds0", O_RDWR);
    if (fd >= 0) {
        unsigned int off = 0x000000;
        int ret = write(fd, &off, sizeof(int));
        VOICE_LOG(LOG_INFO, "write OFF returned %d\n", ret);
        close(fd);
    }
#else
    hal_ledc_init();
    unsigned int rgb_data = 0x000000;
    sunxi_set_led_brightness(0, rgb_data);
#endif
    g_led_is_on = false;
    VOICE_LOG(LOG_INFO, "LED OFF\n");
}

static void led_set_red(void)
{
    VOICE_LOG(LOG_INFO, "led_set_red called\n");
#ifdef CONFIG_LED_RGB_WS2812
    int fd = open("/dev/leds0", O_RDWR);
    if (fd >= 0) {
        unsigned int red = 0xFF0000;
        int ret = write(fd, &red, sizeof(int));
        VOICE_LOG(LOG_INFO, "write RED returned %d\n", ret);
        close(fd);
    } else {
        VOICE_LOG(LOG_ERR, "failed to open /dev/leds0, errno=%d\n", errno);
    }
#else
    hal_ledc_init();
    unsigned int rgb_data = 0xFF0000;
    sunxi_set_led_brightness(0, rgb_data);
#endif
    g_led_is_on = true;
    VOICE_LOG(LOG_INFO, "Red LED ON\n");
}

static void *led_blink_green_task(void *arg)
{
    (void)arg;
    VOICE_LOG(LOG_INFO, "LED green blink task started (2s)\n");
    led_set_green();
    usleep(2000000);
    led_set_off();
    VOICE_LOG(LOG_INFO, "LED green blink task completed\n");
    return NULL;
}

static void *led_blink_green_short_task(void *arg)
{
    (void)arg;
    VOICE_LOG(LOG_INFO, "LED green short blink task started (1s)\n");
    led_set_green();
    usleep(1000000);
    led_set_off();
    VOICE_LOG(LOG_INFO, "LED green short blink task completed\n");
    return NULL;
}

static void *led_blink_red_task(void *arg)
{
    (void)arg;
    VOICE_LOG(LOG_INFO, "LED red blink task started (2s)\n");
    led_set_red();
    usleep(2000000);
    led_set_off();
    VOICE_LOG(LOG_INFO, "LED red blink task completed\n");
    return NULL;
}

static int voice_rpmsg_ept_callback(struct rpmsg_endpoint *ept, void *data,
        size_t len, uint32_t src, void *priv)
{
    voice_rpmsg_event_msg_t *event_msg = (voice_rpmsg_event_msg_t *)data;

    if (len < sizeof(voice_rpmsg_header_t)) {
        VOICE_LOG(LOG_ERR, "message too short (%zu bytes)\n", len);
        return -1;
    }

    if (event_msg->header.magic != VOICE_RPMSG_MAGIC) {
        VOICE_LOG(LOG_ERR, "invalid magic (0x%lx)\n",
               (unsigned long)event_msg->header.magic);
        return -1;
    }

    VOICE_LOG(LOG_INFO, "received event=%d, len=%zu\n",
            event_msg->event, len);

    switch (event_msg->event) {
        case VOICE_EVENT_KEYWORD_DETECTED:
            VOICE_LOG(LOG_INFO, "keyword detected: %d, confidence=%.2f\n",
                    event_msg->keyword_data.keyword, event_msg->keyword_data.confidence);
            {
                pthread_t tid;
                void *(*task)(void *) = NULL;

                if (event_msg->keyword_data.keyword == VOICE_KEYWORD_YES) {
                    publish_keyword_event("yes", event_msg->keyword_data.confidence);
                    task = led_blink_green_task;
                } else if (event_msg->keyword_data.keyword == VOICE_KEYWORD_NO) {
                    publish_keyword_event("no", event_msg->keyword_data.confidence);
                    task = led_blink_green_short_task;
                } else if (event_msg->keyword_data.keyword == VOICE_KEYWORD_AED) {
                    publish_keyword_event("异常声音上报", event_msg->keyword_data.confidence);
                    task = led_blink_red_task;
                }

                if (task != NULL) {
                    if (pthread_create(&tid, NULL, task, NULL) != 0) {
                        VOICE_LOG(LOG_ERR, "failed to create LED blink task\n");
                    } else {
                        pthread_detach(tid);
                    }
                }
            }
            break;
        case VOICE_EVENT_SPEECH_START:
            VOICE_LOG(LOG_INFO, "speech started\n");
            break;
        case VOICE_EVENT_SPEECH_END:
            VOICE_LOG(LOG_INFO, "speech ended, duration=%lums\n",
                    (unsigned long)event_msg->speech_data.duration_ms);
            break;
        case VOICE_EVENT_ERROR:
            VOICE_LOG(LOG_ERR, "error code=%lu, message=%s\n",
                    (unsigned long)event_msg->error_data.error_code,
                    event_msg->error_data.error_message);
            break;
        default:
            VOICE_LOG(LOG_INFO, "unknown event=%d\n", event_msg->event);
            break;
    }

    return 0;
}

static void voice_rpmsg_ept_release(struct rpmsg_endpoint *ept)
{
    VOICE_LOG(LOG_INFO, "endpoint released\n");
    if (g_voice_ept == ept) {
        g_voice_ept = NULL;
    }
}

static bool voice_rpmsg_ns_match(struct rpmsg_device *rdev,
        void *priv_, const char *name, uint32_t dest)
{
    return !strncmp(name, VOICE_RPMSG_EPT_NAME, strlen(VOICE_RPMSG_EPT_NAME));
}

static void voice_rpmsg_ns_unbind(struct rpmsg_endpoint *ept)
{
    VOICE_LOG(LOG_INFO, "unbinding\n");
    rpmsg_destroy_ept(ept);
}

static void voice_rpmsg_ns_bind(struct rpmsg_device *rdev,
        void *priv_, const char *name, uint32_t dest)
{
    struct rpmsg_endpoint *ept;
    int ret;

    VOICE_LOG(LOG_INFO, "binding to %s\n", name);

    ept = hal_malloc(sizeof(struct rpmsg_endpoint));
    if (ept == NULL) {
        VOICE_LOG(LOG_ERR, "failed to allocate endpoint\n");
        return;
    }

    memset(ept, 0, sizeof(struct rpmsg_endpoint));
    ept->priv = NULL;
    ept->release_cb = voice_rpmsg_ept_release;

    ret = rpmsg_create_ept(ept, rdev, name,
            RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
            voice_rpmsg_ept_callback, voice_rpmsg_ns_unbind);
    if (ret != 0) {
        VOICE_LOG(LOG_ERR, "failed to create endpoint (ret=%d)\n", ret);
        hal_free(ept);
        return;
    }

    ept->dest_addr = dest;
    g_voice_ept = ept;

    VOICE_LOG(LOG_INFO, "endpoint created successfully\n");
}

static void voice_rpmsg_destroy(FAR struct rpmsg_device *rdev, FAR void *priv)
{
    if (g_voice_ept != NULL) {
        rpmsg_destroy_ept(g_voice_ept);
        g_voice_ept = NULL;
    }
}

void voice_rpmsg_init(void)
{
    VOICE_LOG(LOG_INFO, "initializing, binding to %s\n", VOICE_RPMSG_EPT_NAME);
    rpmsg_register_callback(NULL, NULL, voice_rpmsg_destroy,
            voice_rpmsg_ns_match, voice_rpmsg_ns_bind);
}

void voice_rpmsg_deinit(void)
{
    VOICE_LOG(LOG_INFO, "deinitializing\n");
    rpmsg_unregister_callback(NULL, NULL, voice_rpmsg_destroy,
            voice_rpmsg_ns_match, voice_rpmsg_ns_bind);
}
