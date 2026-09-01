#ifndef __RVT_MAP_API_H__
#define __RVT_MAP_API_H__

#include <stdint.h>

#define RVT_MAP_NAV_EVENT_MATCH          "MATCH"
#define RVT_MAP_NAV_EVENT_DISPLAY        "DISPLAY"
#define RVT_MAP_NAV_EVENT_SEARCH         "SEARCH"
#define RVT_MAP_NAV_EVENT_ROUTE          "ROUTE"
#define RVT_MAP_NAV_EVENT_NAV_START      "NAV_START"
#define RVT_MAP_NAV_EVENT_ARRIVED        "ARRIVED"
#define RVT_MAP_NAV_EVENT_FAVORITE_LIMIT "FAVORITE_LIMIT"
#define RVT_MAP_NAV_EVENT_VEHICLE_MODE   "VEHICLE_MODE"

#define RVT_MAP_NAV_STATUS_OK      "OK"
#define RVT_MAP_NAV_STATUS_FAIL    "FAIL"
#define RVT_MAP_NAV_STATUS_SELECT  "SELECT"
#define RVT_MAP_NAV_STATUS_UNKNOWN "UNKNOWN"

typedef enum {
    /* 地图北方向固定朝上。 */
    RVT_MAP_DIRECTION_NORTH_UP = 0,
    /* 导航路线方向朝上，默认推荐给车载导航场景。 */
    RVT_MAP_DIRECTION_ROUTE_UP = 1,
} rvt_map_direction_t;

/*
 * 函数名: rvt_map_set_nav_destination
 * 入参: latitude_e7 目的地纬度乘以 10000000，longitude_e7 目的地经度乘以 10000000，name 目的地名称，可传 NULL
 * 返回值: 0 表示命令通过投屏 TCP 通道发送成功，负值表示 TCP 未连接或发送失败
 */
int rvt_map_set_nav_destination(int32_t latitude_e7,
        int32_t longitude_e7, const char *name);

/*
 * 函数名: rvt_map_search_destination
 * 入参: keyword 目的地搜索关键词，例如“南京站”
 * 返回值: 0 表示搜索命令通过投屏 TCP 通道发送成功，负值表示参数无效、TCP 未连接或发送失败
 */
int rvt_map_search_destination(const char *keyword);

/*
 * 函数名: rvt_map_prepare_navigation
 * 入参: keyword 目的地搜索关键词，例如“南京南站”
 * 返回值: 0 表示已启动投屏服务并发送“搜索目的地+规划路线”命令，负值表示参数无效、投屏服务启动失败或 TCP 未连接
 */
int rvt_map_prepare_navigation(const char *keyword);

/*
 * 函数名: rvt_map_show_location
 * 入参: 无
 * 返回值: 0 表示已启动投屏服务并发送“只显示地图和定位”命令，负值表示投屏服务启动失败或 TCP 未连接
 */
int rvt_map_show_location(void);

/*
 * 函数名: rvt_map_select_destination
 * 入参: select_num 搜索候选目的地序号，从 1 开始，例如 1 表示选择第一个候选
 * 返回值: 0 表示选择命令发送成功，负值表示参数无效、TCP 未连接或发送失败
 */
int rvt_map_select_destination(int select_num);

/*
 * 函数名: rvt_map_plan_route
 * 入参: 无
 * 返回值: 0 表示路线规划命令发送成功，负值表示 TCP 未连接或发送失败
 */
int rvt_map_plan_route(void);

/*
 * 函数名: rvt_map_nav_start
 * 入参: 无
 * 返回值: 0 表示开始导航命令发送成功，负值表示 TCP 未连接、未设置目的地或未规划路线
 */
int rvt_map_nav_start(void);

/*
 * 函数名: rvt_map_nav_stop
 * 入参: 无
 * 返回值: 0 表示退出导航命令发送成功，负值表示 TCP 未连接或发送失败
 */
int rvt_map_nav_stop(void);

/*
 * 函数名: rvt_map_set_vehicle_speed
 * 入参: speed_kmh 当前车速，单位 km/h，负值无效，0 表示未知或静止
 * 返回值: 0 表示命令发送成功，负值表示参数无效、TCP 未连接或发送失败
 */
int rvt_map_set_vehicle_speed(float speed_kmh);

/*
 * 函数名: rvt_map_set_direction
 * 入参: direction 地图方向，RVT_MAP_DIRECTION_NORTH_UP 表示北向上，RVT_MAP_DIRECTION_ROUTE_UP 表示路线向上
 * 返回值: 0 表示命令发送成功，负值表示参数无效、TCP 未连接或发送失败
 */
int rvt_map_set_direction(rvt_map_direction_t direction);

/*
 * 函数名: rvt_map_handle_turn_prompt
 * 入参: distance_m 距离下一路口的距离，单位米；direction 转向类型字符串；prompt_text 手机端生成的路口提示文本
 * 返回值: 无
 */
void rvt_map_handle_turn_prompt(int distance_m,
        const char *direction, const char *prompt_text);

/*
 * 函数名: rvt_map_handle_navigation_arrived
 * 入参: 无
 * 返回值: 无
 */
void rvt_map_handle_navigation_arrived(void);

/*
 * 函数名: rvt_map_handle_navigation_match
 * 入参: status 匹配状态，取值同 rvt_map_handle_navigation_status 的 status；message 手机端生成的提示文本
 * 返回值: 无
 * 说明: 兼容旧 NAV_MATCH 回调，内部可按 event=RVT_MAP_NAV_EVENT_MATCH 归一到 rvt_map_handle_navigation_status
 */
void rvt_map_handle_navigation_match(const char *status, const char *message);

/*
 * 函数名: rvt_map_handle_navigation_status
 * 入参: event 导航事件类型，当前取值:
 *       RVT_MAP_NAV_EVENT_MATCH          目的地匹配/搜索候选结果
 *       RVT_MAP_NAV_EVENT_SEARCH         目的地搜索流程
 *       RVT_MAP_NAV_EVENT_ROUTE          路线规划流程
 *       RVT_MAP_NAV_EVENT_NAV_START      开始导航流程
 *       RVT_MAP_NAV_EVENT_ARRIVED        已到达目的地
 *       RVT_MAP_NAV_EVENT_FAVORITE_LIMIT 收藏地址达到上限
 *       RVT_MAP_NAV_EVENT_VEHICLE_MODE   交通工具选择流程
 *       其他字符串应按未知事件处理，避免后续扩展时兼容性问题
 *       status 事件状态，当前取值:
 *       RVT_MAP_NAV_STATUS_OK      成功或状态完成
 *       RVT_MAP_NAV_STATUS_FAIL    失败
 *       RVT_MAP_NAV_STATUS_SELECT  需要用户/AI 继续选择，例如匹配到多个目的地
 *       RVT_MAP_NAV_STATUS_UNKNOWN 未知状态
 *       message 手机端生成的 UTF-8 状态说明，可为空；可用于语音播报或调试日志，不建议作为业务判断唯一依据
 * 返回值: 无
 * 说明: 后续 AI 集成建议优先监听该统一状态接口；NAV_MATCH/NAV_ARRIVED 仍保留为兼容接口
 */
void rvt_map_handle_navigation_status(const char *event,
        const char *status, const char *message);

/*
 * 函数名: rvt_map_handle_display_active
 * 入参: 无
 * 返回值: 无
 * 说明: 手机投屏首帧已进入渲染队列，UI 可隐藏加载提示
 */
void rvt_map_handle_display_active(void);

/*
 * 函数名: rvt_map_flush_pending_commands
 * 入参: 无
 * 返回值: 无
 */
void rvt_map_flush_pending_commands(void);

/*
 * 函数名: rvt_map_cmd_locate
 * 入参: argc 参数数量，argv 参数列表，支持 map_locate <keyword>
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_locate(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_go
 * 入参: argc 参数数量，argv 参数列表，支持 map_go <keyword>
 * 返回值: 0 表示投屏服务启动且命令发送/缓存成功，负值表示失败
 */
int rvt_map_cmd_go(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_show_location
 * 入参: argc 参数数量，argv 参数列表，支持 map_show_location
 * 返回值: 0 表示投屏服务启动且命令发送/缓存成功，负值表示失败
 */
int rvt_map_cmd_show_location(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_select
 * 入参: argc 参数数量，argv 参数列表，支持 map_select <num>
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_select(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_dest
 * 入参: argc 参数数量，argv 参数列表，支持 map_dest <lat_e7> <lon_e7> [name]
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_dest(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_route
 * 入参: argc 参数数量，argv 参数列表，支持 map_route
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_route(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_nav_start
 * 入参: argc 参数数量，argv 参数列表，支持 map_nav_start
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_nav_start(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_nav_stop
 * 入参: argc 参数数量，argv 参数列表，支持 map_nav_stop
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_nav_stop(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_speed
 * 入参: argc 参数数量，argv 参数列表，支持 map_speed <kmh>
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_speed(int argc, char *argv[]);

/*
 * 函数名: rvt_map_cmd_direction
 * 入参: argc 参数数量，argv 参数列表，支持 map_direction <route_up|north_up>
 * 返回值: 0 表示命令发送成功，负值表示失败
 */
int rvt_map_cmd_direction(int argc, char *argv[]);

#endif
