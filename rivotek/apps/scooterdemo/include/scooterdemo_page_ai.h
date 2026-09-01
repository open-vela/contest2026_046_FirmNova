/****************************************************************************
 * scooterdemo_page_ai.h
 *
 * AI Assistant overlay for the scooter dashboard.
 * - Listens on POSIX mqueue "doubao_chat" for AI text replies.
 * - Shows / hides a cartoon avatar on the main dashboard.
 * - Drives page transitions (home ↔ navigation) with fade animation.
 ****************************************************************************/

#ifndef SCOOTERDEMO_PAGE_AI_H
#define SCOOTERDEMO_PAGE_AI_H

#include <lvgl/lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*scooterdemo_ai_page_switch_cb_t)(int page_index, void *user_data);

/**
 * Initialise the AI-assistant module.
 *
 * @param screen        The active LVGL screen (lv_scr_act()).
 * @param home_cont     Container of the HOME page.
 * @param nav_cont      Container of the NAVIGATION page.
 * @param disp_w        Display width in pixels.
 * @param disp_h        Display height in pixels.
 *
 * Call once after all pages have been built and registered.
 * Spawns a background thread that reads "doubao_chat" mqueue.
 */
void scooterdemo_ai_init(lv_obj_t *screen,
                         lv_obj_t *home_cont,
                         lv_obj_t *nav_cont,
                         int32_t disp_w,
                         int32_t disp_h,
                         scooterdemo_ai_page_switch_cb_t page_switch_cb,
                         void *page_switch_user_data);

/**
 * Poll function – call from the 100 ms UI timer callback.
 * Processes any pending AI messages on the LVGL thread.
 */
void scooterdemo_ai_poll(void);

/**
 * Trigger AI greeting / voice wakeup (called on long-press OK).
 * Shows avatar and sends WAKEUP command to doubao_demo.
 */
void scooterdemo_ai_trigger_greeting(void);

/**
 * Check whether the AI assistant is currently in an active session.
 * Return whether the AI assistant session is active.
 *
 * True after wakeup / voice input starts, false after the assistant fully exits.
 */
bool scooterdemo_ai_is_session_active(void);

/**
 * Force the current AI session to exit immediately.
 */
void scooterdemo_ai_force_exit(void);

/**
 * Pass the bottom-bar battery image so the AI module can swap it.
 */
void scooterdemo_ai_set_bottom_img(lv_obj_t *img);

/**
 * Pass the battery percent label so the AI module can update it.
 */
void scooterdemo_ai_set_percent_label(lv_obj_t *label);

/**
 * Reset the battery percent label back to 100%.
 */
void scooterdemo_ai_reset_percent_label(void);

/**
 * Set the navigation map image used by the AI navigation overlay.
 */
void scooterdemo_ai_set_navigation_map_path(const char *path);

/**
 * Sync the actual active page state back to the AI module.
 */
void scooterdemo_ai_notify_navigation_page(bool nav_page_active);

#ifdef __cplusplus
}
#endif

#endif /* SCOOTERDEMO_PAGE_AI_H */
