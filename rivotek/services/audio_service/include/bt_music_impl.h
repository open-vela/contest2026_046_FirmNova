#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_BT_MUSIC_IMPL_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_BT_MUSIC_IMPL_H

#include <stdbool.h>
#include <stdint.h>

#define SCOOTERDEMO_BT_MUSIC_DEVICE_NAME_MAX 64
#define SCOOTERDEMO_BT_MUSIC_TITLE_MAX 128
#define SCOOTERDEMO_BT_MUSIC_ARTIST_MAX 128

typedef struct {
    int (*ensure_ready)(void *ctx);
    int (*connect_toggle)(void *ctx);
    int (*toggle_play)(void *ctx);
    int (*stop)(void *ctx);
    int (*play_prev)(void *ctx);
    int (*play_next)(void *ctx);
    int (*request_metadata)(void *ctx);
} rivotek_audio_component_ops_t;

typedef rivotek_audio_component_ops_t rivotek_bt_music_ops_t;
typedef rivotek_audio_component_ops_t scooterdemo_bt_music_ops_t;

void rivotek_audio_component_init(void);
int rivotek_audio_component_register_ops(const rivotek_audio_component_ops_t *ops,
                                         void *ctx);
int rivotek_audio_component_ensure_ready(void);
int rivotek_audio_component_connect_toggle(void);
int rivotek_audio_component_toggle_play(void);
int rivotek_audio_component_stop(void);
int rivotek_audio_component_play_prev(void);
int rivotek_audio_component_play_next(void);
int rivotek_audio_component_request_metadata(void);
void rivotek_audio_component_get_title_text(char *buffer, uint32_t buffer_size);
void rivotek_audio_component_get_artist_text(char *buffer, uint32_t buffer_size);
bool rivotek_audio_component_is_connected(void);
bool rivotek_audio_component_is_playing(void);
bool rivotek_audio_component_has_metadata(void);
uint32_t rivotek_audio_component_get_duration_ms(void);

static inline void rivotek_audio_service_init(void)
{
    rivotek_audio_component_init();
}

static inline int rivotek_audio_service_register_ops(const rivotek_bt_music_ops_t *ops,
                                                     void *ctx)
{
    return rivotek_audio_component_register_ops(ops, ctx);
}

static inline int rivotek_audio_service_ensure_ready(void)
{
    return rivotek_audio_component_ensure_ready();
}

static inline int rivotek_audio_service_connect_toggle(void)
{
    return rivotek_audio_component_connect_toggle();
}

static inline int rivotek_audio_service_toggle_play(void)
{
    return rivotek_audio_component_toggle_play();
}

static inline int rivotek_audio_service_stop(void)
{
    return rivotek_audio_component_stop();
}

static inline int rivotek_audio_service_play_prev(void)
{
    return rivotek_audio_component_play_prev();
}

static inline int rivotek_audio_service_play_next(void)
{
    return rivotek_audio_component_play_next();
}

static inline int rivotek_audio_service_request_metadata(void)
{
    return rivotek_audio_component_request_metadata();
}

static inline void rivotek_audio_service_get_title_text(char *buffer, uint32_t buffer_size)
{
    rivotek_audio_component_get_title_text(buffer, buffer_size);
}

static inline void rivotek_audio_service_get_artist_text(char *buffer, uint32_t buffer_size)
{
    rivotek_audio_component_get_artist_text(buffer, buffer_size);
}

static inline bool rivotek_audio_service_is_connected(void)
{
    return rivotek_audio_component_is_connected();
}

static inline bool rivotek_audio_service_is_playing(void)
{
    return rivotek_audio_component_is_playing();
}

static inline bool rivotek_audio_service_has_metadata(void)
{
    return rivotek_audio_component_has_metadata();
}

static inline uint32_t rivotek_audio_service_get_duration_ms(void)
{
    return rivotek_audio_component_get_duration_ms();
}

static inline void scooterdemo_bt_music_init(void)
{
    rivotek_audio_component_init();
}

static inline int scooterdemo_bt_music_register_ops(const scooterdemo_bt_music_ops_t *ops,
                                                    void *ctx)
{
    return rivotek_audio_component_register_ops(ops, ctx);
}

static inline int scooterdemo_bt_music_ensure_ready(void)
{
    return rivotek_audio_component_ensure_ready();
}

static inline int scooterdemo_bt_music_connect_toggle(void)
{
    return rivotek_audio_component_connect_toggle();
}

static inline int scooterdemo_bt_music_toggle_play(void)
{
    return rivotek_audio_component_toggle_play();
}

static inline int scooterdemo_bt_music_stop(void)
{
    return rivotek_audio_component_stop();
}

static inline int scooterdemo_bt_music_play_prev(void)
{
    return rivotek_audio_component_play_prev();
}

static inline int scooterdemo_bt_music_play_next(void)
{
    return rivotek_audio_component_play_next();
}

static inline int scooterdemo_bt_music_request_metadata(void)
{
    return rivotek_audio_component_request_metadata();
}

static inline void scooterdemo_bt_music_get_title_text(char *buffer, uint32_t buffer_size)
{
    rivotek_audio_component_get_title_text(buffer, buffer_size);
}

static inline void scooterdemo_bt_music_get_artist_text(char *buffer, uint32_t buffer_size)
{
    rivotek_audio_component_get_artist_text(buffer, buffer_size);
}

static inline bool scooterdemo_bt_music_is_connected(void)
{
    return rivotek_audio_component_is_connected();
}

static inline bool scooterdemo_bt_music_is_playing(void)
{
    return rivotek_audio_component_is_playing();
}

static inline bool scooterdemo_bt_music_has_metadata(void)
{
    return rivotek_audio_component_has_metadata();
}

static inline uint32_t scooterdemo_bt_music_get_duration_ms(void)
{
    return rivotek_audio_component_get_duration_ms();
}

#endif
