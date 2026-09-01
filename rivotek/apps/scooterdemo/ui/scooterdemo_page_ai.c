/****************************************************************************
 * scooterdemo_page_ai.c
 *
 * AI Assistant overlay – listens to doubao_demo via POSIX mqueue,
 * shows avatar, and drives page transitions with fade animations.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <lvgl/lvgl.h>
#include "scooterdemo_page_ai.h"
#include "scooterdemo_pages.h"
#include "theme_impl.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define AI_MQ_NAME          "doubao_chat"
#define AI_MQ_MAXMSG        16
#define AI_MQ_MSGSIZE       512

#define AI_AVATAR_SIZE      250         /* px, before any scaling       */
#define AI_AVATAR_PATH      "/resource/imgs/ai_avatar.png"
#define AI_NAV_MAP_PATH     "/resource/imgs/nav_map_beijing.png"
#define AI_DIALOG_BG_PATH   "/resource/imgs/Dialog_box_mini.png"
#define AI_SELFCHECK_PATH   "/resource/imgs/self_check.png"

/* Theme-specific 30 % battery bottom-bar images (green / blue / orange) */
static const char *s_bottom_30_paths[] = {
    "/resource/imgs/Group_g_30_battery.png",
    "/resource/imgs/Group_b_30_battery.png",
    "/resource/imgs/Group_o_30_battery.png",
};

#define AI_CMD_MQ_NAME      "doubao_cmd"
#define AI_CMD_MQ_MAXMSG    8

#define FADE_DURATION_MS    300
#define DIALOG_W            321
#define DIALOG_H            200
#define DIALOG_PAD          12
#define DIALOG_SHOW_MS      1000        /* visible duration before fade  */
#define AI_SESSION_IDLE_TIMEOUT_MS 20000

/* Ring-buffer depth for messages coming from the mqueue reader thread. */
#define MSG_RING_SIZE       4
#define MSG_TEXT_MAX        256

/* ------------------------------------------------------------------ */
/*  AI command identifiers (local)                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    AI_CMD_NONE = 0,
    AI_CMD_SHOW_AVATAR,     /* GPIO woke up AI – show avatar on home   */
    AI_CMD_IDLE_SYNC,       /* __IDLE__ – doubao entered idle           */
    AI_CMD_NAV_HOME,        /* "导航回家"                               */
    AI_CMD_EXIT_NAV,        /* "退出导航" / "退下"                      */
    AI_CMD_TTS_DONE,        /* __TTS_DONE__ – voice playback finished  */
    AI_CMD_SELFCHECK,       /* "车辆自检中" – vehicle self-check       */
    AI_CMD_CONDITION_REMIND, /* "我记下了" – condition reminder reply    */
    AI_CMD_EXIT_AI,         /* "下次见" – exit AI assistant entirely   */
    AI_CMD_GENERIC_TEXT,    /* any other AI text (show avatar briefly)  */
} ai_cmd_t;

/* ------------------------------------------------------------------ */
/*  Module state                                                       */
/* ------------------------------------------------------------------ */

/* Ring buffer for inter-thread messaging (reader thread → LVGL thread) */
typedef struct {
    ai_cmd_t cmd;
    char     text[MSG_TEXT_MAX];
} ai_msg_t;

static ai_msg_t  s_ring[MSG_RING_SIZE];
static volatile int s_ring_wr;          /* written by reader thread     */
static volatile int s_ring_rd;          /* read by LVGL thread          */

/* LVGL objects */
static lv_obj_t  *s_screen;
static lv_obj_t  *s_home_cont;
static lv_obj_t  *s_nav_cont;
static int32_t    s_disp_w;
static int32_t    s_disp_h;

static lv_obj_t  *s_avatar_img;        /* AI avatar overlay             */
static lv_obj_t  *s_nav_map_img;       /* full-screen nav map image     */
static bool       s_avatar_visible;
static bool       s_avatar_hiding;
static bool       s_on_nav_page;       /* currently showing nav map?    */
static bool       s_session_active;     /* wakeup/listening until exit   */
static char       s_nav_map_path[128] = AI_NAV_MAP_PATH;

/* Self-check overlay */
static lv_obj_t  *s_selfcheck_img;     /* self_check.png overlay        */
static bool       s_on_selfcheck_page; /* currently showing self-check? */

/* Dialog bubble */
static lv_obj_t  *s_dialog_cont;       /* dialog container              */
static lv_obj_t  *s_dialog_bg_img;     /* Dialog_box.png background     */
static lv_obj_t  *s_dialog_label;      /* text label inside dialog      */
static lv_font_t *s_font_16;           /* 16px font for dialog text     */
static bool       s_dialog_visible;

static lv_obj_t  *s_bottom_img;        /* battery bar image (from main) */
static lv_obj_t  *s_percent_label;     /* battery % label (from main)   */
static scooterdemo_ai_page_switch_cb_t s_page_switch_cb;
static void *s_page_switch_user_data;
static bool       s_wakeup_session_active;

/* Pending action: which page transition to perform after TTS done */
typedef enum {
    PENDING_NONE = 0,
    PENDING_NAV_HOME,
    PENDING_EXIT_NAV,
    PENDING_SELFCHECK,          /* wait for TTS "车辆自检中" done */
    PENDING_SELFCHECK_RESULT,   /* wait for TTS result done      */
    PENDING_CONDITION_REMIND,   /* wait for condition-remind TTS done */
    PENDING_EXIT_AI,            /* wait for TTS "下次见" done    */
} pending_action_t;

static pending_action_t s_pending_action;

/* Safety timer: auto-hide avatar if __TTS_DONE__ never arrives (e.g.
 * user long-presses OK but does not speak, or network failure).       */
static lv_timer_t *s_avatar_safety_timer;
static lv_timer_t *s_session_idle_timer;

static pthread_t  s_reader_tid;
static bool       s_reader_running;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static inline int ring_next(int idx)
{
    return (idx + 1) % MSG_RING_SIZE;
}

static void set_session_active(bool active);
static void restart_session_idle_timer(void);

static bool ai_overlay_busy(void)
{
    return s_wakeup_session_active ||
           s_pending_action != PENDING_NONE ||
           s_dialog_visible ||
           s_avatar_visible ||
           s_avatar_hiding ||
           s_on_selfcheck_page;
}

/** Classify an incoming AI text into a command. */
static ai_cmd_t classify_text(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return AI_CMD_NONE;
    }

    /* Session state sync from doubao_client */
    if (strcmp(text, "__IDLE__") == 0) {
            return AI_CMD_IDLE_SYNC;
    }

    /* TTS playback finished signal from doubao_demo */
    if (strcmp(text, "__TTS_DONE__") == 0) {
        return AI_CMD_TTS_DONE;
    }

    /*
     * Keywords produced by the ASR-intercept layer in event_processor.cxx.
     * We look for the *reply* text here, since doubao_chat carries the AI
     * reply, not the raw ASR.  The intercept layer sends VERBATIM replies
     * whose exact text we control.
     */

    /* "好的，这就导航回家" – navigate home */
    if (strstr(text, "\xe8\xbf\x99\xe5\xb0\xb1\xe5\xaf\xbc\xe8\x88\xaa"
                     "\xe5\x9b\x9e\xe5\xae\xb6") != NULL) {
        return AI_CMD_NAV_HOME;
    }

    /* "好的主人，有需要再召唤我！" – exit / dismiss */
    if (strstr(text, "\xe6\x9c\x89\xe9\x9c\x80\xe8\xa6\x81\xe5\x86\x8d"
                     "\xe5\x8f\xac\xe5\x94\xa4\xe6\x88\x91") != NULL) {
        return AI_CMD_EXIT_NAV;
    }

    /* "好的主人，下次见。" – exit AI assistant entirely */
    if (strstr(text, "\xe4\xb8\x8b\xe6\xac\xa1\xe8\xa7\x81") != NULL) {
        return AI_CMD_EXIT_AI;
    }

    /* "车辆自检中" – vehicle self-check */
    if (strstr(text, "\xe8\xbd\xa6\xe8\xbe\x86\xe8\x87\xaa\xe6\xa3\x80"
                     "\xe4\xb8\xad") != NULL) {
        return AI_CMD_SELFCHECK;
    }

    /* "我记下了" – condition reminder reply */
    if (strstr(text, "\xe6\x88\x91\xe8\xae\xb0\xe4\xb8\x8b\xe4\xba\x86") != NULL) {
        return AI_CMD_CONDITION_REMIND;
    }

    return AI_CMD_GENERIC_TEXT;
}

/* ------------------------------------------------------------------ */
/*  mqueue reader thread                                               */
/* ------------------------------------------------------------------ */

static void *ai_mq_reader_thread(void *arg)
{
    struct mq_attr attr;
    char buf[AI_MQ_MSGSIZE];
    mqd_t mq;

    (void)arg;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = AI_MQ_MAXMSG;
    attr.mq_msgsize = AI_MQ_MSGSIZE;

    mq = mq_open(AI_MQ_NAME, O_RDONLY | O_CREAT, 0666, &attr);
    if (mq == (mqd_t)-1) {
        printf("[AI] mq_open(%s) failed: %d\n", AI_MQ_NAME, errno);
        return NULL;
    }

    printf("[AI] mqueue reader started\n");

    while (s_reader_running) {
        ssize_t n = mq_receive(mq, buf, sizeof(buf), NULL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            printf("[AI] mq_receive error: %d\n", errno);
            break;
        }

        buf[n < (ssize_t)sizeof(buf) ? n : (ssize_t)sizeof(buf) - 1] = '\0';

        ai_cmd_t cmd = classify_text(buf);
        if (cmd == AI_CMD_NONE) {
            continue;
        }

        /* Push into ring buffer (lock-free single-producer). */
        int next = ring_next(s_ring_wr);
        if (next == s_ring_rd) {
            /* Ring full – drop oldest. */
            s_ring_rd = ring_next(s_ring_rd);
        }

        s_ring[s_ring_wr].cmd = cmd;
        strncpy(s_ring[s_ring_wr].text, buf, MSG_TEXT_MAX - 1);
        s_ring[s_ring_wr].text[MSG_TEXT_MAX - 1] = '\0';
        s_ring_wr = next;
    }

    mq_close(mq);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  LVGL fade helpers                                                  */
/* ------------------------------------------------------------------ */

static void fade_in(lv_obj_t *obj, uint32_t duration_ms)
{
    lv_anim_delete(obj, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_obj_set_style_opa(obj, LV_OPA_0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
}

static void fade_out_done_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)lv_anim_get_user_data(a);
    if (obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void avatar_fade_out_done_cb(lv_anim_t *a)
{
    s_avatar_hiding = false;
    fade_out_done_cb(a);
}

static void fade_out(lv_obj_t *obj, uint32_t duration_ms)
{
    lv_anim_delete(obj, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_user_data(&a, obj);
    lv_anim_set_completed_cb(&a, fade_out_done_cb);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ */
/*  Dialog bubble management                                           */
/* ------------------------------------------------------------------ */

static void create_dialog(void)
{
    if (s_dialog_cont != NULL) {
        return;
    }

    /* Container – sized to the dialog image, no default bg/border */
    s_dialog_cont = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_dialog_cont);
    lv_obj_set_size(s_dialog_cont, DIALOG_W, DIALOG_H);
    lv_obj_set_style_bg_opa(s_dialog_cont, LV_OPA_0, 0);
    lv_obj_set_scrollbar_mode(s_dialog_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_dialog_cont, LV_OBJ_FLAG_HIDDEN);

    /* Background image (Dialog_box.png) */
    s_dialog_bg_img = lv_img_create(s_dialog_cont);
    lv_img_set_src(s_dialog_bg_img, AI_DIALOG_BG_PATH);
    lv_obj_set_size(s_dialog_bg_img, DIALOG_W, DIALOG_H);
    lv_obj_align(s_dialog_bg_img, LV_ALIGN_CENTER, 0, 0);

    /* Text label */
    s_dialog_label = lv_label_create(s_dialog_cont);
    lv_obj_set_size(s_dialog_label, DIALOG_W - DIALOG_PAD * 2,
                                    LV_SIZE_CONTENT);
    lv_obj_align(s_dialog_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(s_dialog_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_dialog_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_dialog_label, lv_color_hex(0xFFFFFF), 0);
    if (s_font_16) {
        lv_obj_set_style_text_font(s_dialog_label, s_font_16, 0);
    }
    lv_label_set_text(s_dialog_label, "");

    s_dialog_visible = false;
}

/** Fade-out callback: hide dialog after animation completes. */
static void dialog_fade_out_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_dialog_cont) {
        lv_obj_add_flag(s_dialog_cont, LV_OBJ_FLAG_HIDDEN);
    }
    s_dialog_visible = false;
}

static void dismiss_dialog(void)
{
    if (s_dialog_cont == NULL || !s_dialog_visible) {
        return;
    }

    /* Cancel any pending animation on the dialog container. */
    lv_anim_delete(s_dialog_cont, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);

    lv_obj_add_flag(s_dialog_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_dialog_cont, LV_OPA_COVER, 0);
    s_dialog_visible = false;
}

static void show_dialog(const char *text)
{
    if (s_dialog_cont == NULL) {
        create_dialog();
    }

    /* If already visible, cancel previous auto-hide animation. */
    if (s_dialog_visible) {
        lv_anim_delete(s_dialog_cont, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    }

    lv_label_set_text(s_dialog_label, text);

    /* Position relative to avatar – left-bottom of dialog at avatar right-top */
    lv_obj_set_style_opa(s_dialog_cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dialog_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_dialog_cont);
    lv_obj_align_to(s_dialog_cont, s_avatar_img,
                    LV_ALIGN_OUT_RIGHT_TOP, -120, -10);

    s_dialog_visible = true;

    /* Dialog stays visible until __TTS_DONE__ triggers the timed hide. */
}

/* ------------------------------------------------------------------ */
/*  Avatar management                                                  */
/* ------------------------------------------------------------------ */

static void create_avatar(void)
{
    if (s_avatar_img != NULL) {
        return;
    }

    s_avatar_img = lv_img_create(s_screen);
    lv_img_set_src(s_avatar_img, AI_AVATAR_PATH);
    lv_obj_set_size(s_avatar_img, AI_AVATAR_SIZE, AI_AVATAR_SIZE);
    lv_obj_align(s_avatar_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN);
    s_avatar_visible = false;
    s_avatar_hiding = false;
}

static void show_avatar(void)
{
    if (s_avatar_img == NULL) {
        create_avatar();
    }

    lv_obj_move_foreground(s_avatar_img);

    if (s_avatar_visible &&
        !s_avatar_hiding &&
        !lv_obj_has_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (s_avatar_hiding) {
        lv_anim_delete(s_avatar_img, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_obj_clear_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_avatar_img, LV_OPA_COVER, 0);
        s_avatar_hiding = false;
        s_avatar_visible = true;
        return;
    }

    s_avatar_hiding = false;
    fade_in(s_avatar_img, FADE_DURATION_MS);
    s_avatar_visible = true;
}

static void start_avatar_fade_out(void)
{
    if (s_avatar_img == NULL) {
        return;
    }

    lv_anim_delete(s_avatar_img, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_avatar_img);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a, FADE_DURATION_MS);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_user_data(&a, s_avatar_img);
    lv_anim_set_completed_cb(&a, avatar_fade_out_done_cb);
    lv_anim_start(&a);

    s_avatar_visible = false;
    s_avatar_hiding = true;
}

static void force_hide_avatar(void)
{
    if (s_avatar_img == NULL) {
        return;
    }

    lv_anim_delete(s_avatar_img, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_obj_add_flag(s_avatar_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_avatar_img, LV_OPA_COVER, 0);
    s_avatar_visible = false;
    s_avatar_hiding = false;
}

static void hide_avatar(void)
{
    if (s_avatar_img == NULL || !s_avatar_visible) {
        return;
    }

    start_avatar_fade_out();
}

/* ------------------------------------------------------------------ */
/*  Navigation map overlay                                             */
/* ------------------------------------------------------------------ */

static void create_nav_map(void)
{
    if (s_nav_map_img != NULL) {
        return;
    }

    s_nav_map_img = lv_img_create(s_nav_cont);
    lv_img_set_src(s_nav_map_img, s_nav_map_path);
    lv_obj_set_size(s_nav_map_img, s_disp_w, s_disp_h);
    lv_obj_align(s_nav_map_img, LV_ALIGN_CENTER, 0, 0);
    /* Start hidden – will be shown when AI says "navigate home". */
    lv_obj_add_flag(s_nav_map_img, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */
/*  Self-check overlay                                                 */
/* ------------------------------------------------------------------ */

static void create_selfcheck_img(void)
{
    if (s_selfcheck_img != NULL) {
        return;
    }

    s_selfcheck_img = lv_img_create(s_screen);
    lv_img_set_src(s_selfcheck_img, AI_SELFCHECK_PATH);
    lv_obj_set_size(s_selfcheck_img, s_disp_w, s_disp_h);
    lv_obj_align(s_selfcheck_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_selfcheck_img, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------ */
/*  Reverse mqueue: send command to doubao_demo                        */
/* ------------------------------------------------------------------ */

static void send_doubao_cmd(const char *cmd)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = AI_CMD_MQ_MAXMSG;
    attr.mq_msgsize = 512;

    mqd_t mq = mq_open(AI_CMD_MQ_NAME, O_WRONLY | O_CREAT | O_NONBLOCK,
                        0666, &attr);
    if (mq == (mqd_t)-1) {
        printf("[AI] mq_open(%s) write failed: %d\n", AI_CMD_MQ_NAME, errno);
        return;
    }

    mq_send(mq, cmd, strlen(cmd) + 1, 0);
    mq_close(mq);
    printf("[AI] sent cmd: %.60s\n", cmd);
}

/* ------------------------------------------------------------------ */
/*  Self-check timer chain                                             */
/* ------------------------------------------------------------------ */

/* Forward declarations */
static void selfcheck_show_img_timer_cb(lv_timer_t *t);
static void selfcheck_result_timer_cb(lv_timer_t *t);
static void selfcheck_back_home_timer_cb(lv_timer_t *t);
static void selfcheck_fallback_timer_cb(lv_timer_t *t);
static void avatar_safety_timer_cb(lv_timer_t *t);

/* Safety: auto-hide avatar when __TTS_DONE__ never arrives (e.g. user
 * long-pressed OK but remained silent, or network/audio failure).     */
static void avatar_safety_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_avatar_safety_timer = NULL;

    printf("[AI] avatar safety timeout: auto-hiding avatar\n");
    dismiss_dialog();
    hide_avatar();

    /* 会话超时兜底：释放 UI 侧会话占用，允许下一次长按再次唤醒。 */
    s_pending_action = PENDING_NONE;
    s_wakeup_session_active = false;

    /* 与 doubao_demo 状态对齐（超时/异常场景下确保回 Idle）。 */
    send_doubao_cmd("IDLE");
}

static void set_session_active(bool active)
{
    s_session_active = active;
    if (!active && s_session_idle_timer != NULL) {
        lv_timer_delete(s_session_idle_timer);
        s_session_idle_timer = NULL;
    }
}

static void session_idle_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_session_idle_timer = NULL;

    if (!s_session_active) {
        return;
    }

    printf("[AI] session idle timeout: auto exit assistant\n");
    dismiss_dialog();
    hide_avatar();
    if (s_on_nav_page) {
        if (s_page_switch_cb != NULL) {
            s_page_switch_cb(SCOOTERDEMO_PAGE_HOME, s_page_switch_user_data);
        } else {
            fade_out(s_nav_cont, FADE_DURATION_MS);
            lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
            fade_in(s_home_cont, FADE_DURATION_MS);
        }
        s_on_nav_page = false;
    }
    send_doubao_cmd("IDLE");
    set_session_active(false);
}

static void restart_session_idle_timer(void)
{
    if (!s_session_active) {
        return;
    }

    if (s_session_idle_timer != NULL) {
        lv_timer_delete(s_session_idle_timer);
    }

    s_session_idle_timer = lv_timer_create(session_idle_timer_cb,
                                           AI_SESSION_IDLE_TIMEOUT_MS,
                                           NULL);
    lv_timer_set_repeat_count(s_session_idle_timer, 1);
}

/* Fallback: if TTS_DONE never arrives, force return home after 15s */
static void selfcheck_fallback_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    if (s_on_selfcheck_page &&
        s_pending_action == PENDING_SELFCHECK_RESULT) {
        printf("[AI] selfcheck: fallback timeout, forcing return home\n");
        s_pending_action = PENDING_NONE;
        if (s_selfcheck_img) {
            fade_out(s_selfcheck_img, FADE_DURATION_MS);
        }
        s_on_selfcheck_page = false;
        if (s_home_cont) {
            lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
            fade_in(s_home_cont, FADE_DURATION_MS);
        }
        dismiss_dialog();
        hide_avatar();
    }
}

/** Step 1 timer: TTS "车辆自检中" finished → wait 3s → show self_check.png */
static void selfcheck_show_img_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    /* Hide dialog and avatar first */
    dismiss_dialog();
    hide_avatar();

    /* Hide home container to stop idle GIF rendering (avoids flicker) */
    if (s_home_cont) {
        fade_out(s_home_cont, FADE_DURATION_MS);
    }

    /* Show self-check image full-screen */
    if (s_selfcheck_img == NULL) {
        create_selfcheck_img();
    }
    lv_obj_clear_flag(s_selfcheck_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_selfcheck_img);
    fade_in(s_selfcheck_img, FADE_DURATION_MS);
    s_on_selfcheck_page = true;

    printf("[AI] selfcheck: showing self_check.png, sending result TTS\n");

    /* Show avatar + dialog for result text locally (not relying on
     * server round-trip which may fail on WS reconnect). */
    show_avatar();
    show_dialog(
        "\xe5\x85\xb1\xe6\xa3\x80\xe6\xb5\x8b" "10"
        "\xe9\xa1\xb9\xef\xbc\x8c" "9"
        "\xe9\xa1\xb9\xe6\xad\xa3\xe5\xb8\xb8\xef\xbc\x8c" "1"
        "\xe9\xa1\xb9\xe5\xbc\x82\xe5\xb8\xb8\xef\xbc\x8c"
        "\xe5\x90\x8e\xe8\xbd\xae\xe8\x83\x8e\xe5\x8e\x8b\xe4\xb8\x8d"
        "\xe8\xb6\xb3\xef\xbc\x8c\xe4\xb8\xbb\xe4\xba\xba\xe8\xaf\xb7"
        "\xe5\x8f\x8a\xe6\x97\xb6\xe5\x85\x85\xe6\xb0\x94\xe5\x93\xa6");

    /* Send result text to doubao_demo for TTS playback.
     * "共检测10项，9项正常，1项异常，后轮胎压不足，主人请及时充气哦"
     * Use TTS_WAKE because the session might have gone idle. */
    send_doubao_cmd(
        "TTS_WAKE:"
        "\xe5\x85\xb1\xe6\xa3\x80\xe6\xb5\x8b" "10"
        "\xe9\xa1\xb9\xef\xbc\x8c" "9"
        "\xe9\xa1\xb9\xe6\xad\xa3\xe5\xb8\xb8\xef\xbc\x8c" "1"
        "\xe9\xa1\xb9\xe5\xbc\x82\xe5\xb8\xb8\xef\xbc\x8c"
        "\xe5\x90\x8e\xe8\xbd\xae\xe8\x83\x8e\xe5\x8e\x8b\xe4\xb8\x8d"
        "\xe8\xb6\xb3\xef\xbc\x8c\xe4\xb8\xbb\xe4\xba\xba\xe8\xaf\xb7"
        "\xe5\x8f\x8a\xe6\x97\xb6\xe5\x85\x85\xe6\xb0\x94\xe5\x93\xa6");

    s_pending_action = PENDING_SELFCHECK_RESULT;

    /* Fallback: force return home if __TTS_DONE__ never arrives (15s) */
    lv_timer_create(selfcheck_fallback_timer_cb, 15000, NULL);
}

/** Step 2: result TTS done → wait 2s → hide self_check.png, return home */
static void selfcheck_back_home_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    if (s_selfcheck_img) {
        fade_out(s_selfcheck_img, FADE_DURATION_MS);
    }
    s_on_selfcheck_page = false;

    /* Ensure home container is visible */
    if (s_home_cont) {
        lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
        fade_in(s_home_cont, FADE_DURATION_MS);
    }

    /* Hide avatar and dialog if still visible */
    dismiss_dialog();
    hide_avatar();

    printf("[AI] selfcheck: done, returning to home\n");
}

/* ------------------------------------------------------------------ */
/*  Condition-reminder timer: swap battery image + send TTS            */
/* ------------------------------------------------------------------ */

static void condition_remind_battery_timer_cb(lv_timer_t *t)
{
    lv_timer_delete(t);

    /* Swap bottom bar image to theme-specific 30 % battery variant */
    if (s_bottom_img) {
        int idx = rivotek_theme_component_get_active_index();
        if (idx < 0 || idx >= 3) idx = 0;
        lv_img_set_src(s_bottom_img, s_bottom_30_paths[idx]);
        printf("[AI] condition-remind: swapped bottom img → 30%% battery (theme %d)\n", idx);
    }

    /* Update percent label to 30 % */
    if (s_percent_label) {
        lv_label_set_text(s_percent_label, "30%");
    }

    /* Show avatar + dialog for battery text locally. */
    show_avatar();
    show_dialog(
        "\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c"
        "\xe7\x94\xb5\xe9\x87\x8f\xe5\xb7\xb2\xe4\xb8\x8d\xe8\xb6\xb3"
        "30%"
        "\xe5\x96\xbd\xef\xbc\x8c"
        "\xe8\xaf\xb7\xe8\xae\xb0\xe5\xbe\x97\xe5\x8f\x8a\xe6\x97\xb6"
        "\xe5\x85\x85\xe7\x94\xb5\xe5\x93\xa6\xef\xbc\x81");

    /* Send battery-low voice reminder to doubao_demo.
     * "主人，电量已不足30%喽，请记得及时充电哦！" */
    send_doubao_cmd(
        "TTS_WAKE:"
        "\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c"
        "\xe7\x94\xb5\xe9\x87\x8f\xe5\xb7\xb2\xe4\xb8\x8d\xe8\xb6\xb3"
        "30%"
        "\xe5\x96\xbd\xef\xbc\x8c"
        "\xe8\xaf\xb7\xe8\xae\xb0\xe5\xbe\x97\xe5\x8f\x8a\xe6\x97\xb6"
        "\xe5\x85\x85\xe7\x94\xb5\xe5\x93\xa6\xef\xbc\x81");
}

/* ------------------------------------------------------------------ */
/*  Page transition actions                                            */
/* ------------------------------------------------------------------ */

/** Execute the deferred page transition (called after dialog hides). */
static void execute_pending_transition(void)
{
    pending_action_t act = s_pending_action;
    s_pending_action = PENDING_NONE;

    switch (act) {
    case PENDING_NAV_HOME: {
        if (s_nav_map_img == NULL) {
            create_nav_map();
        }
        lv_obj_clear_flag(s_nav_map_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_nav_map_img);

        if (s_page_switch_cb != NULL) {
            s_page_switch_cb(SCOOTERDEMO_PAGE_NAVIGATION, s_page_switch_user_data);
        } else {
            fade_out(s_home_cont, FADE_DURATION_MS);
            lv_obj_clear_flag(s_nav_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_nav_cont);
            fade_in(s_nav_cont, FADE_DURATION_MS);
        }

        s_on_nav_page = true;
        break;
    }
    case PENDING_EXIT_NAV: {
        if (s_on_nav_page) {
            if (s_page_switch_cb != NULL) {
                s_page_switch_cb(SCOOTERDEMO_PAGE_HOME, s_page_switch_user_data);
            } else {
                fade_out(s_nav_cont, FADE_DURATION_MS);
                lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
                fade_in(s_home_cont, FADE_DURATION_MS);
            }
            s_on_nav_page = false;
        }
        break;
    }
    case PENDING_SELFCHECK: {
        /* TTS "车辆自检中" finished → 3 s delay → show self_check.png */
        lv_timer_create(selfcheck_show_img_timer_cb, 3000, NULL);
        break;
    }
    case PENDING_SELFCHECK_RESULT: {
        /* TTS result finished → 2 s delay → hide image, return home */
        lv_timer_create(selfcheck_back_home_timer_cb, 2000, NULL);
        break;
    }
    case PENDING_CONDITION_REMIND: {
        /* Condition-remind TTS finished → 3 s delay → swap battery img + TTS */
        lv_timer_create(condition_remind_battery_timer_cb, 3000, NULL);
        break;
    }
    case PENDING_EXIT_AI: {
        /* "下次见" TTS finished → tell doubao_demo to go Idle */
        if (s_on_nav_page) {
            if (s_page_switch_cb != NULL) {
                s_page_switch_cb(SCOOTERDEMO_PAGE_HOME, s_page_switch_user_data);
            } else {
                fade_out(s_nav_cont, FADE_DURATION_MS);
                lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
                fade_in(s_home_cont, FADE_DURATION_MS);
            }
            s_on_nav_page = false;
        }
        s_wakeup_session_active = false;
        send_doubao_cmd("IDLE");
        set_session_active(false);
        printf("[AI] exit-ai: sent IDLE to doubao_demo\n");
        break;
    }
    default:
        break;
    }
}

/** Callback: dialog fade-out finished → hide avatar with delay. */
static void dialog_hide_then_avatar_cb(lv_anim_t *a)
{
    (void)a;
    /* Hide dialog */
    if (s_dialog_cont) {
        lv_obj_add_flag(s_dialog_cont, LV_OBJ_FLAG_HIDDEN);
    }
    s_dialog_visible = false;

    /* Execute any pending page transition */
    execute_pending_transition();

    /* Now fade out avatar */
    if (s_avatar_img && s_avatar_visible) {
        start_avatar_fade_out();
    }
}

static void action_navigate_home(const char *text)
{
    /* Show avatar + dialog; page transition deferred until TTS done. */
    show_avatar();
    show_dialog(text);
    s_pending_action = PENDING_NAV_HOME;
}

static void action_exit_nav(const char *text)
{
    /* Show avatar + dialog; page transition deferred until TTS done. */
    show_avatar();
    show_dialog(text);
    s_pending_action = PENDING_EXIT_NAV;
}

static void action_exit_ai(const char *text)
{
    /* Show avatar + dialog; session teardown deferred until TTS done. */
    show_avatar();
    show_dialog(text);
    s_pending_action = PENDING_EXIT_AI;
}

static void action_generic_text(const char *text)
{
    /* Show avatar + dialog; hide deferred until TTS done. */
    show_avatar();
    show_dialog(text);
    /* 不清除 s_pending_action，避免 VERBATIM 回声覆盖导航意图 */
}

static void action_selfcheck(const char *text)
{
    /* Show avatar + dialog "车辆自检中..."; transition deferred until TTS done */
    show_avatar();
    show_dialog(text);
    s_pending_action = PENDING_SELFCHECK;
}

static void action_condition_remind(const char *text)
{
    /* Show avatar + dialog "我记下了…"; battery swap deferred until TTS done */
    show_avatar();
    show_dialog(text);
    s_pending_action = PENDING_CONDITION_REMIND;
}

/* doubao_client 已进入 Idle：强制复位 UI 会话占用，确保可再次长按唤醒。 */
static void action_idle_sync(void)
{
    if (s_avatar_safety_timer) {
        lv_timer_delete(s_avatar_safety_timer);
        s_avatar_safety_timer = NULL;
    }

    s_pending_action = PENDING_NONE;
    s_wakeup_session_active = false;

    dismiss_dialog();
    force_hide_avatar();

    printf("[AI] idle sync: cleared assistant active flags\n");
}

/**
 * TTS playback finished (__TTS_DONE__).
 * Sequence: dialog stays 1 s → dialog fades out → avatar fades out.
 */
static void action_tts_done(void)
{
    /* TTS_DONE arrived – cancel safety timer if still pending. */
    if (s_avatar_safety_timer) {
        lv_timer_delete(s_avatar_safety_timer);
        s_avatar_safety_timer = NULL;
    }

    if (!s_dialog_visible && !s_avatar_visible) {
        /* Nothing visible – just run pending transitions silently. */
        execute_pending_transition();
        return;
    }

    if (s_dialog_visible) {
        /* Dialog visible → keep 1 s, then fade out. */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_dialog_cont);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_0);
        lv_anim_set_time(&a, FADE_DURATION_MS);
        lv_anim_set_delay(&a, DIALOG_SHOW_MS);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_anim_set_completed_cb(&a, dialog_hide_then_avatar_cb);
        lv_anim_start(&a);
    } else {
        /* No dialog but avatar is visible – just fade avatar. */
        execute_pending_transition();
        if (s_avatar_visible) {
            start_avatar_fade_out();
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void scooterdemo_ai_init(lv_obj_t *screen,
                         lv_obj_t *home_cont,
                         lv_obj_t *nav_cont,
                         int32_t disp_w,
                         int32_t disp_h,
                         scooterdemo_ai_page_switch_cb_t page_switch_cb,
                         void *page_switch_user_data)
{
    s_screen    = screen;
    s_home_cont = home_cont;
    s_nav_cont  = nav_cont;
    s_disp_w    = disp_w;
    s_disp_h    = disp_h;
    s_page_switch_cb = page_switch_cb;
    s_page_switch_user_data = page_switch_user_data;

    s_ring_rd = 0;
    s_ring_wr = 0;
    s_on_nav_page = false;
    s_on_selfcheck_page = false;
    s_avatar_visible = false;
    s_avatar_hiding = false;
    s_wakeup_session_active = false;
    s_session_active = false;
    s_pending_action = PENDING_NONE;

    /* Create 16px font for dialog text. */
#ifdef LV_USE_FREETYPE
    s_font_16 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                    LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 16,
                    LV_FREETYPE_FONT_STYLE_NORMAL);
#endif
    if (s_font_16 == NULL) {
        s_font_16 = (lv_font_t *)&lv_font_montserrat_16;
    }

    create_avatar();
    create_nav_map();
    create_selfcheck_img();
    create_dialog();

    /* Start mqueue reader thread. */
    s_reader_running = true;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    if (pthread_create(&s_reader_tid, &attr, ai_mq_reader_thread, NULL) != 0) {
        printf("[AI] Failed to create mqueue reader thread\n");
    } else {
        pthread_detach(s_reader_tid);
    }
    pthread_attr_destroy(&attr);

    printf("[AI] AI assistant module initialised (%dx%d)\n",
           (int)disp_w, (int)disp_h);
}

void scooterdemo_ai_poll(void)
{
    while (s_ring_rd != s_ring_wr) {
        ai_msg_t *msg = &s_ring[s_ring_rd];

        printf("[AI] cmd=%d text=%.80s\n", (int)msg->cmd, msg->text);
        restart_session_idle_timer();

        switch (msg->cmd) {
        case AI_CMD_IDLE_SYNC:
            action_idle_sync();
            break;
        case AI_CMD_NAV_HOME:
            action_navigate_home(msg->text);
            break;
        case AI_CMD_EXIT_NAV:
            action_exit_nav(msg->text);
            break;
        case AI_CMD_EXIT_AI:
            action_exit_ai(msg->text);
            break;
        case AI_CMD_TTS_DONE:
            action_tts_done();
            break;
        case AI_CMD_SELFCHECK:
            action_selfcheck(msg->text);
            break;
        case AI_CMD_CONDITION_REMIND:
            action_condition_remind(msg->text);
            break;
        case AI_CMD_SHOW_AVATAR:
        case AI_CMD_GENERIC_TEXT:
            action_generic_text(msg->text);
            break;
        default:
            break;
        }

        s_ring_rd = ring_next(s_ring_rd);
    }
}

void scooterdemo_ai_trigger_greeting(void)
{
    printf("[AI] Long-press OK → trigger greeting / wakeup\n");

    /* 自愈：若只剩陈旧 active 标志但界面已空闲，先恢复再继续唤醒。 */
    if (s_wakeup_session_active &&
        s_pending_action == PENDING_NONE &&
        !s_dialog_visible &&
        !s_avatar_visible &&
        !s_avatar_hiding &&
        !s_on_selfcheck_page &&
        !s_on_nav_page) {
        printf("[AI] stale wakeup_session_active recovered\n");
        s_wakeup_session_active = false;
    }

    if (s_wakeup_session_active) {
        printf("[AI] ignore wakeup: assistant session already active\n");
        return;
    }

    s_wakeup_session_active = true;

    /* Show avatar immediately */
    set_session_active(true);
    restart_session_idle_timer();
    show_avatar();

    /* Start safety timer: if __TTS_DONE__ never arrives (user silent,
     * network failure, etc.), auto-recover around chat-timeout window. */
    if (s_avatar_safety_timer) {
        lv_timer_delete(s_avatar_safety_timer);
    }
    s_avatar_safety_timer = lv_timer_create(avatar_safety_timer_cb,
                                            22000, NULL);
    lv_timer_set_repeat_count(s_avatar_safety_timer, 1);

    /* Send WAKEUP to doubao_demo via reverse mqueue.
     * doubao_demo's cmd_mq_reader_thread will call set_gpio_triggered(true)
     * which triggers the wakeup flow and starts voice conversation. */
    send_doubao_cmd("WAKEUP");
}

bool scooterdemo_ai_is_session_active(void)
{
    return s_wakeup_session_active ||
           s_pending_action != PENDING_NONE ||
           s_dialog_visible ||
           s_avatar_visible ||
           s_avatar_hiding ||
           s_on_selfcheck_page;
}

void scooterdemo_ai_force_exit(void)
{
    bool need_home_fade_in = false;

    printf("[AI] force exit requested\n");

    if (s_avatar_safety_timer) {
        lv_timer_delete(s_avatar_safety_timer);
        s_avatar_safety_timer = NULL;
    }

    need_home_fade_in = s_on_nav_page ||
                        s_on_selfcheck_page ||
                        (s_home_cont &&
                         lv_obj_has_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN));

    s_wakeup_session_active = false;
    s_pending_action = PENDING_NONE;
    s_on_selfcheck_page = false;
    s_on_nav_page = false;

    dismiss_dialog();
    force_hide_avatar();

    if (s_home_cont) {
        lv_anim_delete(s_home_cont, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_obj_clear_flag(s_home_cont, LV_OBJ_FLAG_HIDDEN);
        if (need_home_fade_in) {
            fade_in(s_home_cont, FADE_DURATION_MS);
        } else {
            lv_obj_set_style_opa(s_home_cont, LV_OPA_COVER, 0);
        }
    }

    if (s_nav_cont) {
        lv_anim_delete(s_nav_cont, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_obj_add_flag(s_nav_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_nav_cont, LV_OPA_COVER, 0);
    }

    if (s_selfcheck_img) {
        lv_anim_delete(s_selfcheck_img,
                       (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_obj_add_flag(s_selfcheck_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_selfcheck_img, LV_OPA_COVER, 0);
    }

    send_doubao_cmd("IDLE");
    return s_session_active;
}

void scooterdemo_ai_set_bottom_img(lv_obj_t *img)
{
    s_bottom_img = img;
}

void scooterdemo_ai_set_percent_label(lv_obj_t *label)
{
    s_percent_label = label;
}

void scooterdemo_ai_reset_percent_label(void)
{
    if (s_percent_label) {
        lv_label_set_text(s_percent_label, "100%");
    }
}

void scooterdemo_ai_set_navigation_map_path(const char *path)
{
    if (path == NULL || path[0] == '\0' || access(path, F_OK) != 0) {
        return;
    }

    if (strcmp(s_nav_map_path, path) != 0) {
        snprintf(s_nav_map_path, sizeof(s_nav_map_path), "%s", path);
        printf("[AI] nav map=%s\n", s_nav_map_path);
    }

    if (s_nav_map_img != NULL) {
        lv_img_set_src(s_nav_map_img, s_nav_map_path);
        lv_obj_set_size(s_nav_map_img, s_disp_w, s_disp_h);
        lv_obj_align(s_nav_map_img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_invalidate(s_nav_map_img);
    }
}

void scooterdemo_ai_notify_navigation_page(bool nav_page_active)
{
    s_on_nav_page = nav_page_active;
}
