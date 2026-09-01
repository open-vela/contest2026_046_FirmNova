#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_THEME_IMPL_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_THEME_IMPL_H

#include <stdbool.h>
#include <lvgl/lvgl.h>

typedef void (*rivotek_theme_changed_cb_t)(int theme_index, void *user_data);
typedef rivotek_theme_changed_cb_t scooterdemo_theme_changed_cb_t;

void rivotek_theme_component_init(int initial_theme_index);
void rivotek_theme_component_set_changed_cb(rivotek_theme_changed_cb_t cb,
                                            void *user_data);
void rivotek_theme_component_request_apply(int theme_index);
int rivotek_theme_component_get_active_index(void);

void rivotek_theme_component_bind_home_background(lv_obj_t *img);
void rivotek_theme_component_bind_navigation_background(lv_obj_t *img,
                                                        lv_obj_t *slot);
void rivotek_theme_component_bind_speed_background(lv_obj_t *img,
                                                   lv_obj_t *driving_img);
void rivotek_theme_component_bind_music_right_image(lv_obj_t *img);
void rivotek_theme_component_bind_bottom_layer(lv_obj_t *img);
void rivotek_theme_component_refresh_speed_background(void);
void rivotek_theme_component_refresh_bottom_layer(void);
void rivotek_theme_component_refresh_backgrounds(void);
void rivotek_theme_component_refresh_navigation_background(void);

bool rivotek_theme_component_apply_sidebar_item(lv_obj_t *img,
                                                int item_index,
                                                bool selected);

static inline void rivotek_theme_service_init(int initial_theme_index)
{
    rivotek_theme_component_init(initial_theme_index);
}

static inline void rivotek_theme_service_set_changed_cb(rivotek_theme_changed_cb_t cb,
                                                        void *user_data)
{
    rivotek_theme_component_set_changed_cb(cb, user_data);
}

static inline void rivotek_theme_service_request_apply(int theme_index)
{
    rivotek_theme_component_request_apply(theme_index);
}

static inline int rivotek_theme_service_get_active_index(void)
{
    return rivotek_theme_component_get_active_index();
}

static inline void scooterdemo_theme_init(int initial_theme_index)
{
    rivotek_theme_component_init(initial_theme_index);
}

static inline void scooterdemo_theme_set_changed_cb(scooterdemo_theme_changed_cb_t cb,
                                                    void *user_data)
{
    rivotek_theme_component_set_changed_cb(cb, user_data);
}

static inline void scooterdemo_theme_request_apply(int theme_index)
{
    rivotek_theme_component_request_apply(theme_index);
}

static inline int scooterdemo_theme_get_active_index(void)
{
    return rivotek_theme_component_get_active_index();
}

static inline void scooterdemo_theme_bind_home_background(lv_obj_t *img)
{
    rivotek_theme_component_bind_home_background(img);
}

static inline void scooterdemo_theme_bind_navigation_background(lv_obj_t *img,
                                                                lv_obj_t *slot)
{
    rivotek_theme_component_bind_navigation_background(img, slot);
}

static inline void scooterdemo_theme_bind_speed_background(lv_obj_t *img,
                                                           lv_obj_t *driving_img)
{
    rivotek_theme_component_bind_speed_background(img, driving_img);
}

static inline void scooterdemo_theme_bind_music_right_image(lv_obj_t *img)
{
    rivotek_theme_component_bind_music_right_image(img);
}

static inline void scooterdemo_theme_bind_bottom_layer(lv_obj_t *img)
{
    rivotek_theme_component_bind_bottom_layer(img);
}

static inline void scooterdemo_theme_refresh_speed_background(void)
{
    rivotek_theme_component_refresh_speed_background();
}

static inline void scooterdemo_theme_refresh_bottom_layer(void)
{
    rivotek_theme_component_refresh_bottom_layer();
}

static inline void scooterdemo_theme_refresh_backgrounds(void)
{
    rivotek_theme_component_refresh_backgrounds();
}

static inline void scooterdemo_theme_refresh_navigation_background(void)
{
    rivotek_theme_component_refresh_navigation_background();
}

static inline bool scooterdemo_theme_apply_sidebar_item(lv_obj_t *img,
                                                        int item_index,
                                                        bool selected)
{
    return rivotek_theme_component_apply_sidebar_item(img, item_index, selected);
}

#endif
