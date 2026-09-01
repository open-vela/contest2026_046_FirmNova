#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include "eca_client_ui_common.h"

static eca_client_ui_fonts_t g_fonts;
static bool g_fonts_ready;
static int g_display_width = ECA_CLIENT_SCREEN_WIDTH;
static int g_display_height = ECA_CLIENT_SCREEN_HEIGHT;

static int eca_client_ui_scale_axis(int value, int current, int base)
{
    int64_t scaled;

    if (base <= 0 || current <= 0) {
        return value;
    }

    scaled = (int64_t)value * current;
    if (scaled >= 0) {
        scaled += base / 2;
    } else {
        scaled -= base / 2;
    }

    return (int)(scaled / base);
}

static int eca_client_ui_scale_font_size(int size)
{
    int width_size = eca_client_ui_scale_x(size);
    int height_size = eca_client_ui_scale_y(size);
    int scaled = width_size < height_size ? width_size : height_size;

    return scaled > 0 ? scaled : 1;
}

void eca_client_ui_set_display_size(int width, int height)
{
    if (width > 0) {
        g_display_width = width;
    }

    if (height > 0) {
        g_display_height = height;
    }
}

int eca_client_ui_display_width_get(void)
{
    return g_display_width;
}

int eca_client_ui_display_height_get(void)
{
    return g_display_height;
}

int eca_client_ui_scale_x(int value)
{
    return eca_client_ui_scale_axis(value, g_display_width,
                                    ECA_CLIENT_SCREEN_WIDTH);
}

int eca_client_ui_scale_y(int value)
{
    return eca_client_ui_scale_axis(value, g_display_height,
                                    ECA_CLIENT_SCREEN_HEIGHT);
}

int eca_client_ui_scale_size(int value)
{
    int width_size = eca_client_ui_scale_x(value);
    int height_size = eca_client_ui_scale_y(value);

    if (value < 0) {
        return -eca_client_ui_scale_size(-value);
    }

    return width_size < height_size ? width_size : height_size;
}

static lv_obj_t *eca_client_ui_create_raw_label(lv_obj_t *parent,
                                                const char *text,
                                                const lv_font_t *font,
                                                uint32_t color,
                                                int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return label;
}

static lv_obj_t *eca_client_ui_create_raw_circle(lv_obj_t *parent,
                                                 int x, int y,
                                                 int size, uint32_t color)
{
    lv_obj_t *circle = lv_obj_create(parent);

    lv_obj_set_pos(circle, x, y);
    lv_obj_set_size(circle, size, size);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);

    return circle;
}

static void eca_client_ui_init_fonts(void)
{
    if (g_fonts_ready) {
        return;
    }

#ifdef LV_USE_FREETYPE
    g_fonts.font_14 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              eca_client_ui_scale_font_size(14),
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_18 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              eca_client_ui_scale_font_size(18),
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_24 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              eca_client_ui_scale_font_size(24),
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_28 = lv_freetype_font_create("/resource/fonts/MiSans-Normal.ttf",
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              eca_client_ui_scale_font_size(28),
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_bold_18 = lv_freetype_font_create("/resource/fonts/MiSans-Bold.ttf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                                   eca_client_ui_scale_font_size(18),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_bold_24 = lv_freetype_font_create("/resource/fonts/MiSans-Bold.ttf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                                   eca_client_ui_scale_font_size(24),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
    g_fonts.font_bold_28 = lv_freetype_font_create("/resource/fonts/MiSans-Bold.ttf",
                                                   LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                                   eca_client_ui_scale_font_size(28),
                                                   LV_FREETYPE_FONT_STYLE_NORMAL);
#endif

    if (g_fonts.font_14 == NULL) {
        g_fonts.font_14 = &lv_font_montserrat_16;
    }

    if (g_fonts.font_18 == NULL) {
        g_fonts.font_18 = &lv_font_montserrat_20;
    }

    if (g_fonts.font_24 == NULL) {
        g_fonts.font_24 = &lv_font_montserrat_28;
    }

    if (g_fonts.font_28 == NULL) {
        g_fonts.font_28 = &lv_font_montserrat_28;
    }

    if (g_fonts.font_bold_18 == NULL) {
        g_fonts.font_bold_18 = g_fonts.font_18;
    }

    if (g_fonts.font_bold_24 == NULL) {
        g_fonts.font_bold_24 = g_fonts.font_24;
    }

    if (g_fonts.font_bold_28 == NULL) {
        g_fonts.font_bold_28 = g_fonts.font_28;
    }

    g_fonts_ready = true;
}

const eca_client_ui_fonts_t *eca_client_ui_fonts_get(void)
{
    eca_client_ui_init_fonts();
    return &g_fonts;
}

lv_obj_t *eca_client_ui_create_label(lv_obj_t *parent, const char *text,
                                     const lv_font_t *font, uint32_t color,
                                     int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, eca_client_ui_scale_x(x), eca_client_ui_scale_y(y));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return label;
}

lv_obj_t *eca_client_ui_create_bound_label(lv_obj_t *parent, lv_subject_t *subject,
                                           const char *fmt,
                                           const lv_font_t *font,
                                           uint32_t color, int x, int y)
{
    lv_obj_t *label = eca_client_ui_create_label(parent, "", font, color, x, y);

    lv_label_bind_text(label, subject, fmt);
    return label;
}

lv_obj_t *eca_client_ui_create_card(lv_obj_t *parent, int x, int y,
                                    int width, int height, int radius)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_pos(card, eca_client_ui_scale_x(x), eca_client_ui_scale_y(y));
    lv_obj_set_size(card, eca_client_ui_scale_x(width),
                    eca_client_ui_scale_y(height));
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(card, eca_client_ui_scale_size(radius), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(ECA_CLIENT_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    return card;
}

lv_obj_t *eca_client_ui_create_circle(lv_obj_t *parent, int x, int y,
                                      int size, uint32_t color)
{
    lv_obj_t *circle = lv_obj_create(parent);

    lv_obj_set_pos(circle, eca_client_ui_scale_x(x), eca_client_ui_scale_y(y));
    size = eca_client_ui_scale_size(size);
    lv_obj_set_size(circle, size, size);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);

    return circle;
}

void eca_client_ui_create_sun_icon(lv_obj_t *parent, int x, int y)
{
    static const struct {
        int x;
        int y;
        int w;
        int h;
    } rays[] = {
        {9, 0, 2, 5}, {9, 25, 2, 5}, {0, 14, 5, 2}, {25, 14, 5, 2},
        {3, 4, 3, 2}, {24, 23, 3, 2}, {23, 4, 3, 2}, {4, 23, 3, 2},
    };
    lv_obj_t *sun;
    int base_x = eca_client_ui_scale_x(x);
    int base_y = eca_client_ui_scale_y(y);
    size_t i;

    sun = eca_client_ui_create_raw_circle(parent,
                                          base_x + eca_client_ui_scale_size(7),
                                          base_y + eca_client_ui_scale_size(7),
                                          eca_client_ui_scale_size(16),
                                          ECA_CLIENT_COLOR_ACCENT);

    for (i = 0; i < sizeof(rays) / sizeof(rays[0]); i++) {
        lv_obj_t *ray = lv_obj_create(parent);

        lv_obj_set_pos(ray, base_x + eca_client_ui_scale_size(rays[i].x),
                       base_y + eca_client_ui_scale_size(rays[i].y));
        lv_obj_set_size(ray, eca_client_ui_scale_size(rays[i].w),
                        eca_client_ui_scale_size(rays[i].h));
        lv_obj_clear_flag(ray, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(ray, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(ray, lv_color_hex(ECA_CLIENT_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ray, 0, 0);
        lv_obj_set_style_radius(ray, eca_client_ui_scale_size(1), 0);
    }

    lv_obj_move_foreground(sun);
}

void eca_client_ui_create_mic_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *body;
    lv_obj_t *arc;
    lv_obj_t *stem;
    lv_obj_t *base;
    int base_x = eca_client_ui_scale_size(x);
    int base_y = eca_client_ui_scale_size(y);

    body = lv_obj_create(parent);
    lv_obj_set_pos(body, base_x + eca_client_ui_scale_size(22),
                   base_y + eca_client_ui_scale_size(13));
    lv_obj_set_size(body, eca_client_ui_scale_size(18),
                    eca_client_ui_scale_size(29));
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(body, eca_client_ui_scale_size(9), 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(body, 0, 0);

    arc = lv_arc_create(parent);
    lv_obj_set_pos(arc, base_x + eca_client_ui_scale_size(14),
                   base_y + eca_client_ui_scale_size(21));
    lv_obj_set_size(arc, eca_client_ui_scale_size(40),
                    eca_client_ui_scale_size(34));
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 20, 160);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, eca_client_ui_scale_size(4), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    stem = lv_obj_create(parent);
    lv_obj_set_pos(stem, base_x + eca_client_ui_scale_size(30),
                   base_y + eca_client_ui_scale_size(52));
    lv_obj_set_size(stem, eca_client_ui_scale_size(4),
                    eca_client_ui_scale_size(10));
    lv_obj_set_style_bg_color(stem, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stem, 0, 0);

    base = lv_obj_create(parent);
    lv_obj_set_pos(base, base_x + eca_client_ui_scale_size(20),
                   base_y + eca_client_ui_scale_size(61));
    lv_obj_set_size(base, eca_client_ui_scale_size(22),
                    eca_client_ui_scale_size(4));
    lv_obj_set_style_radius(base, eca_client_ui_scale_size(2), 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(base, 0, 0);
}

void eca_client_ui_create_location_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *pin;
    lv_obj_t *dot;
    int base_x = eca_client_ui_scale_x(x);
    int base_y = eca_client_ui_scale_y(y);

    pin = eca_client_ui_create_raw_circle(parent, base_x, base_y,
                                          eca_client_ui_scale_size(14),
                                          ECA_CLIENT_COLOR_ACCENT);
    lv_obj_set_style_transform_pivot_x(pin, eca_client_ui_scale_size(7), 0);
    lv_obj_set_style_transform_pivot_y(pin, eca_client_ui_scale_size(7), 0);

    dot = eca_client_ui_create_raw_circle(parent,
                                          base_x + eca_client_ui_scale_size(5),
                                          base_y + eca_client_ui_scale_size(4),
                                          eca_client_ui_scale_size(5),
                                          0xFFFFFF);
    lv_obj_move_foreground(dot);
}

void eca_client_ui_create_emergency_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *badge;
    lv_obj_t *box;
    lv_obj_t *check;
    int base_x = eca_client_ui_scale_x(x);
    int base_y = eca_client_ui_scale_y(y);

    badge = eca_client_ui_create_raw_circle(parent, base_x, base_y,
                                            eca_client_ui_scale_size(44),
                                            ECA_CLIENT_COLOR_EMERGENCY);
    lv_obj_set_style_radius(badge, eca_client_ui_scale_size(9), 0);

    box = lv_obj_create(badge);
    lv_obj_set_pos(box, eca_client_ui_scale_size(11),
                   eca_client_ui_scale_size(13));
    lv_obj_set_size(box, eca_client_ui_scale_size(22),
                    eca_client_ui_scale_size(17));
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(box, eca_client_ui_scale_size(4), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);

    check = eca_client_ui_create_raw_label(box, LV_SYMBOL_DOWN,
                                           &lv_font_montserrat_20,
                                           ECA_CLIENT_COLOR_EMERGENCY,
                                           eca_client_ui_scale_size(3),
                                           eca_client_ui_scale_size(-4));
    lv_obj_move_foreground(check);
}

void eca_client_ui_create_humidity_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *drop;
    lv_obj_t *shine;
    int base_x = eca_client_ui_scale_x(x);
    int base_y = eca_client_ui_scale_y(y);

    drop = eca_client_ui_create_raw_circle(parent,
                                           base_x + eca_client_ui_scale_size(7),
                                           base_y + eca_client_ui_scale_size(12),
                                           eca_client_ui_scale_size(28),
                                           ECA_CLIENT_COLOR_HUMIDITY);
    lv_obj_set_style_transform_pivot_x(drop, eca_client_ui_scale_size(14), 0);
    lv_obj_set_style_transform_pivot_y(drop, eca_client_ui_scale_size(14), 0);
    lv_obj_set_style_transform_rotation(drop, 450, 0);
    lv_obj_set_style_radius(drop, eca_client_ui_scale_size(10), 0);

    shine = eca_client_ui_create_raw_circle(drop, eca_client_ui_scale_size(7),
                                            eca_client_ui_scale_size(5),
                                            eca_client_ui_scale_size(6),
                                            0xFFFFFF);
    lv_obj_set_style_bg_opa(shine, LV_OPA_80, 0);
    lv_obj_move_foreground(shine);
}
