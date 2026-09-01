#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_BT_MUSIC_STATE_SYNC_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_BT_MUSIC_STATE_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#define BT_MUSIC_STATE_MQ_NAME "/bt_music_state"
#define BT_MUSIC_STATE_MQ_MAXMSG 8

#define BT_MUSIC_TITLE_MAX 128
#define BT_MUSIC_ARTIST_MAX 128
#define BT_MUSIC_ALBUM_MAX 128
#define BT_MUSIC_DEVICE_NAME_MAX 64

typedef struct {
    bool valid;
    bool connected;
    bool playing;
    char title[BT_MUSIC_TITLE_MAX];
    char artist[BT_MUSIC_ARTIST_MAX];
    char album[BT_MUSIC_ALBUM_MAX];
    char device_name[BT_MUSIC_DEVICE_NAME_MAX];
    uint32_t duration_ms;
    uint32_t position_ms;
} bt_music_shared_state_t;

static inline void bt_music_state_reset(bt_music_shared_state_t *state)
{
    if (state != NULL) {
        state->valid = false;
        state->connected = false;
        state->playing = false;
        state->title[0] = '\0';
        state->artist[0] = '\0';
        state->album[0] = '\0';
        state->device_name[0] = '\0';
        state->duration_ms = 0;
        state->position_ms = 0;
    }
}

static inline int bt_music_state_read(bt_music_shared_state_t *state)
{
    (void)state;
    return -1;
}

static inline int bt_music_ctrl_send_cmd(const char *cmd)
{
    (void)cmd;
    return -1;
}

#endif
