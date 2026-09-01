#ifndef __RVT_WIFI_DISPLAY_API_H__
#define __RVT_WIFI_DISPLAY_API_H__

/*
 * 函数名: rvt_wifi_display_start
 * 入参: 无，默认使用 STA 模式，即车机先连接手机 WiFi 热点
 * 返回值: 0 表示启动成功，负值表示启动失败
 */
int rvt_wifi_display_start(void);

/*
 * 函数名: rvt_wifi_display_stop
 * 入参: 无
 * 返回值: 无
 */
void rvt_wifi_display_stop(void);

/*
 * 函数名: rvt_wifi_display_is_running
 * 入参: 无
 * 返回值: 1 表示投屏服务正在运行，0 表示未运行
 */
int rvt_wifi_display_is_running(void);

/*
 * 函数名: rvt_wifi_display_set_screen_size
 * 入参: width 屏幕宽度像素，height 屏幕高度像素
 * 返回值: 0 表示设置成功，负值表示参数无效
 */
int rvt_wifi_display_set_screen_size(int width, int height);

/*
 * 函数名: rvt_wifi_display_set_rotation
 * 入参: rotation 旋转角度，只支持 0、90、180、270
 * 返回值: 0 表示设置成功，负值表示参数无效
 */
int rvt_wifi_display_set_rotation(int rotation);

#endif
