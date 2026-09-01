#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_UI_COMMON_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_UI_COMMON_H

#include <lvgl/lvgl.h>

#define ECA_CLIENT_SCREEN_WIDTH     480
#define ECA_CLIENT_SCREEN_HEIGHT    272

#define ECA_CLIENT_COLOR_BG         0xFFF1A7
#define ECA_CLIENT_COLOR_CARD       0xFFFBE9
#define ECA_CLIENT_COLOR_TEXT       0x241310
#define ECA_CLIENT_COLOR_MUTED      0x6F5A42
#define ECA_CLIENT_COLOR_ACCENT     0xFEB728
#define ECA_CLIENT_COLOR_EMERGENCY  0xFF493D
#define ECA_CLIENT_COLOR_HUMIDITY   0x2D9CDB

typedef struct eca_client_ui_fonts_s {
    const lv_font_t *font_14;
    const lv_font_t *font_18;
    const lv_font_t *font_24;
    const lv_font_t *font_28;
    const lv_font_t *font_bold_18;
    const lv_font_t *font_bold_24;
    const lv_font_t *font_bold_28;
} eca_client_ui_fonts_t;

const eca_client_ui_fonts_t *eca_client_ui_fonts_get(void);

void eca_client_ui_set_display_size(int width, int height);
int eca_client_ui_display_width_get(void);
int eca_client_ui_display_height_get(void);
int eca_client_ui_scale_x(int value);
int eca_client_ui_scale_y(int value);
int eca_client_ui_scale_size(int value);

lv_obj_t *eca_client_ui_create_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color,
                                     int x, int y);
lv_obj_t *eca_client_ui_create_bound_label(lv_obj_t *parent, lv_subject_t *subject,
                                           const char *fmt,
                                           const lv_font_t *font,
                                           uint32_t color, int x, int y);
lv_obj_t *eca_client_ui_create_card(lv_obj_t *parent, int x, int y,
                                    int width, int height, int radius);
lv_obj_t *eca_client_ui_create_circle(lv_obj_t *parent, int x, int y,
                                      int size, uint32_t color);

void eca_client_ui_create_sun_icon(lv_obj_t *parent, int x, int y);
void eca_client_ui_create_mic_icon(lv_obj_t *parent, int x, int y);
void eca_client_ui_create_location_icon(lv_obj_t *parent, int x, int y);
void eca_client_ui_create_emergency_icon(lv_obj_t *parent, int x, int y);
void eca_client_ui_create_humidity_icon(lv_obj_t *parent, int x, int y);

#endif
