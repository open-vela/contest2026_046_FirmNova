#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_SETTINGS_SUBPAGES_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_SETTINGS_SUBPAGES_H

#include <stdbool.h>

#include "scooterdemo_pages.h"

typedef enum {
    /* 系统设置子页。 */
    SCOOTERDEMO_SETTINGS_SUBPAGE_SYSTEM = 0,
    /* 胎压监测子页。 */
    SCOOTERDEMO_SETTINGS_SUBPAGE_TIRE,
    /* 驻车设置子页。 */
    SCOOTERDEMO_SETTINGS_SUBPAGE_PARKING,
    /* 车辆信息子页。 */
    SCOOTERDEMO_SETTINGS_SUBPAGE_VEHICLE,
    /* 主题切换子页。 */
    SCOOTERDEMO_SETTINGS_SUBPAGE_THEME,
} scooterdemo_settings_subpage_id_t;

typedef enum {
    /* 蓝牙音频输出到仪表端。 */
    SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_CLUSTER = 0,
    /* 蓝牙音频输出到手机端。 */
    SCOOTERDEMO_SYSTEM_AUDIO_ROUTE_PHONE,
} scooterdemo_system_audio_route_t;

typedef struct {
    bool bt_enabled;
    bool use_24h_clock;
    scooterdemo_system_audio_route_t audio_route;
} scooterdemo_system_settings_state_t;

typedef struct {
    void (*on_bt_switch_changed)(bool enabled, void *user_data);
    void (*on_audio_route_changed)(scooterdemo_system_audio_route_t route, void *user_data);
    void (*on_clock_mode_changed)(bool use_24h_clock, void *user_data);
} scooterdemo_system_settings_callbacks_t;

typedef enum {
    /* 边撑感应开关。 */
    SCOOTERDEMO_PARKING_SWITCH_SIDE_STAND = 0,
    /* 自动大灯开关。 */
    SCOOTERDEMO_PARKING_SWITCH_AUTO_HEADLIGHT,
    /* TCS 开关。 */
    SCOOTERDEMO_PARKING_SWITCH_TCS,
    /* 陡坡缓降开关。 */
    SCOOTERDEMO_PARKING_SWITCH_HDC,
    /* 坡道驻停开关。 */
    SCOOTERDEMO_PARKING_SWITCH_HILL_HOLD,
    /* 驻车开关总数。 */
    SCOOTERDEMO_PARKING_SWITCH_COUNT,
} scooterdemo_parking_switch_id_t;

typedef struct {
    bool side_stand_enabled;
    bool auto_headlight_enabled;
    bool tcs_enabled;
    bool hdc_enabled;
    bool hill_hold_enabled;
} scooterdemo_parking_settings_state_t;

typedef struct {
    void (*on_side_stand_changed)(bool enabled, void *user_data);
    void (*on_auto_headlight_changed)(bool enabled, void *user_data);
    void (*on_tcs_changed)(bool enabled, void *user_data);
    void (*on_hdc_changed)(bool enabled, void *user_data);
    void (*on_hill_hold_changed)(bool enabled, void *user_data);
    void (*on_any_switch_changed)(scooterdemo_parking_switch_id_t id,
                                  bool enabled,
                                  void *user_data);
} scooterdemo_parking_settings_callbacks_t;

typedef struct {
    bool use_bar_unit;
    float left_pressure;
    float right_pressure;
    int left_temp_c;
    int right_temp_c;
} scooterdemo_tire_info_state_t;

/*
 * 设置系统子页回调。
 * 参数 callbacks: 回调函数集合，传 NULL 表示清空回调。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_subpage_system_set_callbacks(
    const scooterdemo_system_settings_callbacks_t *callbacks,
    void *user_data);
/*
 * 设置系统子页状态并刷新界面。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_system_set_state(
    const scooterdemo_system_settings_state_t *state);
/*
 * 获取系统子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_system_get_state(
    scooterdemo_system_settings_state_t *state);
/* 处理系统子页向上导航。 */
bool scooterdemo_settings_subpage_system_handle_nav_up(void);
/* 处理系统子页向下导航。 */
bool scooterdemo_settings_subpage_system_handle_nav_down(void);
/*
 * 设置系统子页焦点状态。
 * 参数 focused: true 表示进入子页焦点，false 表示清除子页焦点。
 */
void scooterdemo_settings_subpage_system_set_focus(bool focused);
/* 处理系统子页确认事件。 */
bool scooterdemo_settings_subpage_system_handle_enter(void);
/* 处理系统子页双击事件。 */
bool scooterdemo_settings_subpage_system_handle_double_click(void);
/* 释放系统子页持有的对象引用。 */
void scooterdemo_settings_subpage_system_release(void);

/*
 * 构建系统子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 构建出的子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_system_build(lv_obj_t *parent,
                                                    const scooterdemo_ui_metrics_t *ui);
/*
 * 构建胎压子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 构建出的子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_tire_build(lv_obj_t *parent,
                                                  const scooterdemo_ui_metrics_t *ui);
/*
 * 设置胎压子页状态并刷新界面。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_tire_set_state(
    const scooterdemo_tire_info_state_t *state);
/*
 * 获取胎压子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_tire_get_state(
    scooterdemo_tire_info_state_t *state);
/* 处理胎压子页向上导航。 */
bool scooterdemo_settings_subpage_tire_handle_nav_up(void);
/* 处理胎压子页向下导航。 */
bool scooterdemo_settings_subpage_tire_handle_nav_down(void);
/* 处理胎压子页确认事件。 */
bool scooterdemo_settings_subpage_tire_handle_enter(void);
/* 处理胎压子页双击事件。 */
bool scooterdemo_settings_subpage_tire_handle_double_click(void);
/* 释放胎压子页持有的对象引用。 */
void scooterdemo_settings_subpage_tire_release(void);
/*
 * 构建驻车子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 构建出的子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_parking_build(lv_obj_t *parent,
                                                     const scooterdemo_ui_metrics_t *ui);
/*
 * 设置驻车子页回调。
 * 参数 callbacks: 回调函数集合，传 NULL 表示清空回调。
 * 参数 user_data: 回调透传上下文。
 */
void scooterdemo_settings_subpage_parking_set_callbacks(
    const scooterdemo_parking_settings_callbacks_t *callbacks,
    void *user_data);
/*
 * 设置驻车子页状态并刷新界面。
 * 参数 state: 新状态，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_parking_set_state(
    const scooterdemo_parking_settings_state_t *state);
/*
 * 获取驻车子页当前状态。
 * 参数 state: 输出状态指针，传 NULL 则忽略。
 */
void scooterdemo_settings_subpage_parking_get_state(
    scooterdemo_parking_settings_state_t *state);
/* 处理驻车子页向上导航。 */
bool scooterdemo_settings_subpage_parking_handle_nav_up(void);
/* 处理驻车子页向下导航。 */
bool scooterdemo_settings_subpage_parking_handle_nav_down(void);
/*
 * 设置驻车子页焦点状态。
 * 参数 focused: true 表示进入子页焦点，false 表示清除子页焦点。
 */
void scooterdemo_settings_subpage_parking_set_focus(bool focused);
/* 处理驻车子页确认事件。 */
bool scooterdemo_settings_subpage_parking_handle_enter(void);
/* 处理驻车子页双击事件。 */
bool scooterdemo_settings_subpage_parking_handle_double_click(void);
/* 释放驻车子页持有的对象引用。 */
void scooterdemo_settings_subpage_parking_release(void);
/*
 * 构建车辆信息子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 构建出的子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_vehicle_build(lv_obj_t *parent,
                                                     const scooterdemo_ui_metrics_t *ui);
/* 处理车辆信息子页向上导航。 */
bool scooterdemo_settings_subpage_vehicle_handle_nav_up(void);
/* 处理车辆信息子页向下导航。 */
bool scooterdemo_settings_subpage_vehicle_handle_nav_down(void);
/* 处理车辆信息子页确认事件。 */
bool scooterdemo_settings_subpage_vehicle_handle_enter(void);
/* 处理车辆信息子页双击事件。 */
bool scooterdemo_settings_subpage_vehicle_handle_double_click(void);
/* 释放车辆信息子页持有的对象引用。 */
void scooterdemo_settings_subpage_vehicle_release(void);
/*
 * 构建主题子页。
 * 参数 parent: 子页父容器。
 * 参数 ui: UI 缩放与字体参数。
 * 返回值: 构建出的子页根对象。
 */
lv_obj_t *scooterdemo_settings_subpage_theme_build(lv_obj_t *parent,
                                                   const scooterdemo_ui_metrics_t *ui);
/* 释放主题子页持有的对象引用。 */
void scooterdemo_settings_subpage_theme_release(void);
/*
 * 设置主题子页焦点状态。
 * 参数 focused: true 表示主题列表进入焦点，false 表示退出焦点。
 */
void scooterdemo_settings_subpage_theme_set_focus(bool focused);
/* 获取主题子页当前生效主题索引。 */
int scooterdemo_settings_subpage_theme_get_active_index(void);
/* 处理主题子页向上导航。 */
bool scooterdemo_settings_subpage_theme_handle_nav_up(void);
/* 处理主题子页向下导航。 */
bool scooterdemo_settings_subpage_theme_handle_nav_down(void);
/* 处理主题子页确认事件并应用主题。 */
bool scooterdemo_settings_subpage_theme_handle_enter(void);
/* 处理主题子页双击事件。 */
bool scooterdemo_settings_subpage_theme_handle_double_click(void);

#endif
