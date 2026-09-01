#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include "scooterdemo_settings_subpages.h"

#define COLOR_TEXT lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_DARK lv_color_hex(0x111111)
#define COLOR_SUBTEXT lv_color_hex(0xA0A0A0)
#define COLOR_HIGHLIGHT lv_color_hex(0x4BB2FF)
#define COLOR_BUTTON_INACTIVE lv_color_hex(0x252A31)

#define SETTINGS_THEME_COUNT 3
#define SETTINGS_THEME_DEFAULT_INDEX 0
#define SETTINGS_THEME_STATE_PATH "/data/scooterdemo_theme.cfg"

static const char *g_theme_names[SETTINGS_THEME_COUNT] = {
    "智绿灵润",
    "科蓝慧芯",
    "速橙幻微",
};

static const char *g_theme_preview_paths[SETTINGS_THEME_COUNT] = {
    "/resource/imgs/pv_g.png",
    "/resource/imgs/pv_b.png",
    "/resource/imgs/pv_o.png",
};

static const int g_theme_display_order[SETTINGS_THEME_COUNT] = {
    0,
    1,
    2,
};

static lv_obj_t *g_theme_cards[SETTINGS_THEME_COUNT];
static lv_obj_t *g_theme_preview_boxes[SETTINGS_THEME_COUNT];
static lv_obj_t *g_theme_name_labels[SETTINGS_THEME_COUNT];
static lv_obj_t *g_theme_action_boxes[SETTINGS_THEME_COUNT];
static lv_obj_t *g_theme_action_labels[SETTINGS_THEME_COUNT];
static int g_selected_theme_index = SETTINGS_THEME_DEFAULT_INDEX;
static int g_active_theme_index = SETTINGS_THEME_DEFAULT_INDEX;
static bool g_theme_focused;
static bool g_theme_state_loaded;

static int normalize_theme_index(int theme_index)
{
    if (theme_index < 0 || theme_index >= SETTINGS_THEME_COUNT) {
        return SETTINGS_THEME_DEFAULT_INDEX;
    }

    return theme_index;
}

static int load_persisted_theme_index(void)
{
    FILE *file;
    char buffer[32];
    int theme_index = SETTINGS_THEME_DEFAULT_INDEX;
    int parsed_theme_index;
    syslog(LOG_INFO, "scooterdemo: loading persisted theme index from %s\n",
           SETTINGS_THEME_STATE_PATH);
    file = fopen(SETTINGS_THEME_STATE_PATH, "r");
    if (file == NULL) {
        return SETTINGS_THEME_DEFAULT_INDEX;
    }

    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        if (sscanf(buffer, "theme=%d", &parsed_theme_index) == 1 ||
            sscanf(buffer, "%d", &parsed_theme_index) == 1) {
            theme_index = normalize_theme_index(parsed_theme_index);
        }
    }

    fclose(file);
    return theme_index;
}

static void persist_active_theme_index(void)
{
    FILE *file;
    syslog(LOG_INFO, "scooterdemo: persisting active theme index %d to %s\n",
           g_active_theme_index, SETTINGS_THEME_STATE_PATH);
    file = fopen(SETTINGS_THEME_STATE_PATH, "w+");
    if (file == NULL) {
        syslog(LOG_ERR, "scooterdemo: failed to save theme state to %s: %d\n",
               SETTINGS_THEME_STATE_PATH,
               errno);
        return;
    }

    fprintf(file, "theme=%d\n", normalize_theme_index(g_active_theme_index));
    fclose(file);
}

static void ensure_theme_state_loaded(void)
{
    if (g_theme_state_loaded) {
        return;
    }

    g_active_theme_index = load_persisted_theme_index();
    g_selected_theme_index = g_active_theme_index;
    g_theme_state_loaded = true;
}

static int get_theme_display_slot(int theme_index)
{
    for (int slot = 0; slot < SETTINGS_THEME_COUNT; slot++) {
        if (g_theme_display_order[slot] == theme_index) {
            return slot;
        }
    }

    return 0;
}

/* 设置主题卡片基础样式。 */
static void style_card(lv_obj_t *card)
{
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_outline_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
}

/* 设置主题预览图容器基础样式。 */
static void style_preview_box(lv_obj_t *preview)
{
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x070B0F), 0);
    lv_obj_set_style_bg_opa(preview, LV_OPA_100, 0);
    lv_obj_set_style_border_width(preview, 0, 0);
    lv_obj_set_style_border_color(preview, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_radius(preview, 3, 0);
    lv_obj_set_style_outline_width(preview, 0, 0);
    lv_obj_set_style_shadow_width(preview, 0, 0);
    lv_obj_set_style_pad_all(preview, 0, 0);
    lv_obj_set_scrollbar_mode(preview, LV_SCROLLBAR_MODE_OFF);
}

/* 设置主题状态底色基础样式。 */
static void style_action_box(lv_obj_t *box, const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_set_size(box, SCOOTERDEMO_W(ui, 100), SCOOTERDEMO_H(ui, 26));
    lv_obj_set_style_radius(box, 13, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_outline_width(box, 0, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
}

/* 设置主题状态文字基础样式。 */
static void style_action_label(lv_obj_t *label, const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_set_size(label, SCOOTERDEMO_W(ui, 100), SCOOTERDEMO_H(ui, 26));
    lv_obj_set_style_text_font(label, ui->font_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
}

/*
 * 构建主题切换子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_theme_build(lv_obj_t *parent,
                                                   const scooterdemo_ui_metrics_t *ui)
{
    lv_obj_t *container = lv_obj_create(parent);

    ensure_theme_state_loaded();
    lv_obj_set_size(container, SCOOTERDEMO_W(ui, 585), SCOOTERDEMO_H(ui, 360));
    lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_outline_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);

    for (int slot = 0; slot < SETTINGS_THEME_COUNT; slot++) {
        int theme_index = g_theme_display_order[slot];
        lv_obj_t *card = lv_obj_create(container);
        g_theme_cards[theme_index] = card;
        lv_obj_set_size(card, SCOOTERDEMO_W(ui, 170), SCOOTERDEMO_H(ui, 172));
        lv_obj_align(card, LV_ALIGN_TOP_LEFT,
                     SCOOTERDEMO_W(ui, 4 + slot * 194),
                     SCOOTERDEMO_H(ui, 68));
        style_card(card);

        lv_obj_t *preview = lv_obj_create(card);
        g_theme_preview_boxes[theme_index] = preview;
        lv_obj_set_size(preview, SCOOTERDEMO_W(ui, 160), SCOOTERDEMO_H(ui, 96));
        lv_obj_align(preview, LV_ALIGN_TOP_MID, 0, 0);
        style_preview_box(preview);

        if (access(g_theme_preview_paths[theme_index], F_OK) == 0) {
            lv_obj_t *preview_img = lv_img_create(preview);
            lv_img_set_src(preview_img, g_theme_preview_paths[theme_index]);
            lv_obj_center(preview_img);
        } else {
            lv_obj_t *placeholder = lv_label_create(preview);
            lv_label_set_text(placeholder, "主题预览图");
            lv_obj_set_style_text_color(placeholder, COLOR_SUBTEXT, 0);
            lv_obj_set_style_text_font(placeholder, ui->font_16, 0);
            lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(placeholder);
        }

        g_theme_name_labels[theme_index] = lv_label_create(card);
        lv_label_set_text(g_theme_name_labels[theme_index],
                          g_theme_names[theme_index]);
        lv_obj_set_style_text_font(g_theme_name_labels[theme_index],
                                   ui->font_16,
                                   0);
        lv_obj_set_style_text_color(g_theme_name_labels[theme_index],
                                    COLOR_TEXT,
                                    0);
        lv_obj_set_style_text_align(g_theme_name_labels[theme_index],
                                    LV_TEXT_ALIGN_CENTER,
                                    0);
        lv_obj_set_width(g_theme_name_labels[theme_index],
                         SCOOTERDEMO_W(ui, 170));
        lv_obj_align(g_theme_name_labels[theme_index],
                     LV_ALIGN_TOP_MID,
                     0,
                     SCOOTERDEMO_H(ui, 105));

        g_theme_action_boxes[theme_index] = lv_obj_create(card);
        style_action_box(g_theme_action_boxes[theme_index], ui);
        lv_obj_align(g_theme_action_boxes[theme_index],
                     LV_ALIGN_TOP_MID,
                     0,
                     SCOOTERDEMO_H(ui, 136));

        g_theme_action_labels[theme_index] =
            lv_label_create(g_theme_action_boxes[theme_index]);
        lv_label_set_text(g_theme_action_labels[theme_index], "切换");
        style_action_label(g_theme_action_labels[theme_index], ui);
        lv_obj_center(g_theme_action_labels[theme_index]);
    }

    return container;
}

/* 释放主题子页静态对象引用。 */
void scooterdemo_settings_subpage_theme_release(void)
{
    for (int i = 0; i < SETTINGS_THEME_COUNT; i++) {
        g_theme_cards[i] = NULL;
        g_theme_preview_boxes[i] = NULL;
        g_theme_name_labels[i] = NULL;
        g_theme_action_boxes[i] = NULL;
        g_theme_action_labels[i] = NULL;
    }
}

/* 刷新主题卡片高亮和启用状态。 */
static void update_theme_visuals(void)
{
    for (int i = 0; i < SETTINGS_THEME_COUNT; i++) {
        lv_obj_t *card = g_theme_cards[i];

        if (card == NULL) {
            continue;
        }

        lv_obj_set_style_outline_width(card, 0, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);

        if (g_theme_preview_boxes[i] != NULL) {
            bool highlighted = g_theme_focused && i == g_selected_theme_index;

            lv_obj_set_style_border_width(g_theme_preview_boxes[i],
                                          highlighted ? 2 : 0,
                                          0);
            lv_obj_set_style_border_color(g_theme_preview_boxes[i],
                                          highlighted ? COLOR_HIGHLIGHT
                                                      : lv_color_hex(0x101820),
                                          0);
        }

        if (g_theme_name_labels[i] != NULL) {
            lv_obj_set_style_text_color(g_theme_name_labels[i],
                                        COLOR_TEXT,
                                        0);
        }

        if (g_theme_action_labels[i] != NULL) {
            lv_label_set_text(g_theme_action_labels[i],
                              i == g_active_theme_index ? "已切换" : "未切换");
            lv_obj_set_style_text_color(g_theme_action_labels[i],
                                        i == g_active_theme_index ? COLOR_TEXT_DARK : COLOR_TEXT,
                                        0);
        }

        if (g_theme_action_boxes[i] != NULL) {
            lv_obj_set_style_bg_color(g_theme_action_boxes[i],
                                      i == g_active_theme_index ? lv_color_hex(0xFFFFFF)
                                                                : COLOR_BUTTON_INACTIVE,
                                      0);
            lv_obj_set_style_bg_opa(g_theme_action_boxes[i], LV_OPA_100, 0);
        }
    }
}

/*
 * 设置主题子页焦点状态。
 * 参数 focused: true 表示主题列表进入焦点，false 表示退出焦点。
 */
void scooterdemo_settings_subpage_theme_set_focus(bool focused)
{
    ensure_theme_state_loaded();
    g_theme_focused = focused;
    update_theme_visuals();
}

/* 获取主题子页当前生效主题索引。 */
int scooterdemo_settings_subpage_theme_get_active_index(void)
{
    ensure_theme_state_loaded();
    return g_active_theme_index;
}

/* 处理主题列表向上导航。 */
bool scooterdemo_settings_subpage_theme_handle_nav_up(void)
{
    ensure_theme_state_loaded();

    if (!g_theme_focused) {
        return false;
    }

    int selected_slot = get_theme_display_slot(g_selected_theme_index) - 1;
    if (selected_slot < 0) {
        selected_slot = SETTINGS_THEME_COUNT - 1;
    }
    g_selected_theme_index = g_theme_display_order[selected_slot];

    update_theme_visuals();
    return true;
}

/* 处理主题列表向下导航。 */
bool scooterdemo_settings_subpage_theme_handle_nav_down(void)
{
    ensure_theme_state_loaded();

    if (!g_theme_focused) {
        return false;
    }

    int selected_slot = get_theme_display_slot(g_selected_theme_index) + 1;
    if (selected_slot >= SETTINGS_THEME_COUNT) {
        selected_slot = 0;
    }
    g_selected_theme_index = g_theme_display_order[selected_slot];

    update_theme_visuals();
    return true;
}

/* 处理主题确认事件并应用主题。 */
bool scooterdemo_settings_subpage_theme_handle_enter(void)
{
    ensure_theme_state_loaded();

    if (!g_theme_focused || g_selected_theme_index < 0 ||
        g_selected_theme_index >= SETTINGS_THEME_COUNT) {
        return false;
    }

    g_active_theme_index = g_selected_theme_index;
    persist_active_theme_index();
    update_theme_visuals();
    return true;
}

/* 处理主题子页双击事件。 */
bool scooterdemo_settings_subpage_theme_handle_double_click(void)
{
    return false;
}
