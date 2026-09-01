#ifndef __VENDOR_ALLWINNERTECH_APPS_AUDIOSERVICE_AUDIO_FOCUS_MANAGER_H
#define __VENDOR_ALLWINNERTECH_APPS_AUDIOSERVICE_AUDIO_FOCUS_MANAGER_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_FOCUS_IDLE  0
#define AUDIO_FOCUS_AI    1
#define AUDIO_FOCUS_MUSIC 2

/* Android-like focus change events */
#define AUDIO_FOCUS_CHANGE_GAIN                     1
#define AUDIO_FOCUS_CHANGE_LOSS                    -1
#define AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT          -2
#define AUDIO_FOCUS_CHANGE_LOSS_TRANSIENT_CAN_DUCK -3

/* Focus gain request hints */
#define AUDIO_FOCUS_GAIN            1
#define AUDIO_FOCUS_GAIN_TRANSIENT  2

typedef void (*audio_focus_lost_cb_t)(void);
typedef void (*audio_focus_change_cb_t)(int change);

int audio_focus_manager_init(void);
int audio_focus_request(int type);
int audio_focus_request_ex(int type, int gain_type);
int audio_focus_release(int type);
int audio_focus_get_owner(void);
int audio_focus_register_lost_cb(audio_focus_lost_cb_t cb);
int audio_focus_register_change_cb(audio_focus_change_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
