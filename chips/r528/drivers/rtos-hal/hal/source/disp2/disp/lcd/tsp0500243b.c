#include "tsp0500243b.h"
#include <string.h>
#include <syslog.h>

extern s32 bsp_disp_lcd_set_bright(u32 disp, u32 bright);

static void lcd_power_on(u32 sel);
static void lcd_power_off(u32 sel);
static void lcd_bl_open(u32 sel);
static void lcd_bl_close(u32 sel);
static void lcd_panel_init(u32 sel);
static void lcd_panel_exit(u32 sel);

#define panel_reset(sel, val) sunxi_lcd_gpio_set_value(sel, 0, val)

static void lcd_cfg_panel_info(struct panel_extend_para *info){
    syslog(LOG_INFO, "fisker tsp0500243b: Configuring panel info...\n");
}

/*开机时序配置*/
static s32 lcd_open_flow(u32 sel){
    syslog(LOG_INFO, "fisker tsp0500243b: Starting LCD open flow, sel=%lu\n", sel);

    LCD_OPEN_FUNC(sel, lcd_power_on, 10);
    LCD_OPEN_FUNC(sel, lcd_panel_init, 10);
    LCD_OPEN_FUNC(sel, sunxi_lcd_tcon_enable, 50);
    LCD_OPEN_FUNC(sel, lcd_bl_open, 0);

    return 0;
}

/*关机时序配置*/
static s32 lcd_close_flow(u32 sel)
{
    syslog(LOG_INFO, "fisker T070S140B: Starting LCD close flow, sel=%lu\n", sel);

    LCD_CLOSE_FUNC(sel, lcd_bl_close, 0);
    LCD_CLOSE_FUNC(sel, sunxi_lcd_tcon_disable, 0);
    LCD_CLOSE_FUNC(sel, lcd_panel_exit, 200);
    LCD_CLOSE_FUNC(sel, lcd_power_off, 500);

    return 0;
}
//上电时序配置
static void lcd_power_on(u32 sel)
{
    panel_reset(sel, GPIO_DATA_LOW);
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC1);
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC3);
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_pin_cfg(sel, 1);
    sunxi_lcd_delay_ms(50);
    panel_reset(sel, GPIO_DATA_HIGH);
    sunxi_lcd_delay_ms(20);
    syslog(LOG_INFO, "fisker tsp0500243b: Step 3 - Release RESET (HIGH - active state)\n");
    panel_reset(sel, GPIO_DATA_LOW);
    sunxi_lcd_delay_ms(30);

    panel_reset(sel, GPIO_DATA_HIGH);
    sunxi_lcd_delay_ms(30);
    syslog(LOG_INFO, "fisker tsp0500243b: Step 5 - Enabling AVDD (3.3V - after reset release)\n");
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC2); // power_id=2 对应配置中的 lcd_power2
    sunxi_lcd_delay_ms(20);
    syslog(LOG_INFO, "fisker tsp0500243b: Step 6 - Enabling DSI clock\n");
    sunxi_lcd_dsi_clk_enable(sel);
    syslog(LOG_INFO, "fisker tsp0500243b: Power-on sequence completed\n");
}

/*下电函数(上电的逆序)*/
static void lcd_power_off(u32 sel)
{
    syslog(LOG_INFO, "fisker tsp0500243b: Power off sequence started (reverse of power-on), sel=%lu\n", sel);
    sunxi_lcd_dsi_clk_disable(sel);
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC2);
    sunxi_lcd_delay_ms(20);
    panel_reset(sel, GPIO_DATA_LOW);
    sunxi_lcd_delay_ms(20);

    sunxi_lcd_pin_cfg(sel,0);
    sunxi_lcd_delay_ms(20);

    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC3); // 关闭VDD_IF (1.8V接口电源)
    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC1); // 关闭VDD (1.8V数字电源)
}

/*开启背光*/
static void lcd_bl_open(u32 sel)
{
    syslog(LOG_INFO, "fisker tsp0500243b: Enabling PWM backlight control (ch=4, freq=10KHz, pol=1)\n");
    sunxi_lcd_pwm_enable(sel);
    sunxi_lcd_delay_ms(100);

    syslog(LOG_INFO, "fisker tsp0500243b: Enabling backlight power (LED+)\n");
    sunxi_lcd_backlight_enable(sel);
    sunxi_lcd_delay_ms(100);
}

/*关闭背光*/
static void lcd_bl_close(u32 sel)
{
    syslog(LOG_INFO, "fisker tsp0500243b: Backlight off, sel=%lu\n", sel);

    sunxi_lcd_backlight_disable(sel);
    sunxi_lcd_pwm_disable(sel);
    sunxi_lcd_delay_ms(200);
}

static void lcd_panel_init(u32 sel)
{
    // R01h - 软件复位
    syslog(LOG_INFO, "fisker tsp0500243b: Sending software reset command (0x01)\n");
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_SOFT_RESET);
    sunxi_lcd_delay_ms(120);

    // R11h - EXIT_SLEEP_MODE
    syslog(LOG_INFO, "fisker tsp0500243b: Sending EXIT_SLEEP_MODE (0x11) - vendor mandatory\n");
    printf("[FISKER TSP0500243B] Sending EXIT_SLEEP_MODE command (0x11)\n");
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_EXIT_SLEEP_MODE);

    // R29h - Display On
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_SET_DISPLAY_ON);
    sunxi_lcd_delay_ms(200);
}

static void lcd_panel_exit(u32 sel)
{
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_ENTER_SLEEP_MODE); // Sleep In
    sunxi_lcd_delay_ms(80);
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_SET_DISPLAY_OFF); // Display Off
    sunxi_lcd_delay_ms(50);
}

/* 用户自定义函数*/
static s32 lcd_user_defined_func(u32 sel, u32 para1, u32 para2, u32 para3)
{
    bsp_disp_lcd_set_bright(sel,60);
    return 0;
}
struct __lcd_panel tsp0500243b_panel = {
    .name = "tsp0500243b",
    .func = {
        .cfg_panel_info =  lcd_cfg_panel_info,
        .cfg_open_flow  =  lcd_open_flow,
        .cfg_close_flow =  lcd_close_flow,
        .lcd_user_defined_func = lcd_user_defined_func,
    },
};


