#include "stl5_5_30_258_k.h"
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

static void lcd_cfg_panel_info(struct panel_extend_para *info)
{
    if (!info) {
        return;
    }

    u32 i = 0, j = 0;
    u32 items;

    u8 lcd_gamma_tbl[][2] = {
        {0, 0},
        {15, 15},
        {30, 30},
        {45, 45},
        {60, 60},
        {75, 75},
        {90, 90},
        {105, 105},
        {120, 120},
        {135, 135},
        {150, 150},
        {165, 165},
        {180, 180},
        {195, 195},
        {210, 210},
        {225, 225},
        {240, 240},
        {255, 255},
    };

    u32 lcd_cmap_tbl[2][3][4] = {
        {
            {LCD_CMAP_G0, LCD_CMAP_B1, LCD_CMAP_G2, LCD_CMAP_B3},
            {LCD_CMAP_B0, LCD_CMAP_R1, LCD_CMAP_B2, LCD_CMAP_R3},
            {LCD_CMAP_R0, LCD_CMAP_G1, LCD_CMAP_R2, LCD_CMAP_G3},
        },
        {
            {LCD_CMAP_B3, LCD_CMAP_G2, LCD_CMAP_B1, LCD_CMAP_G0},
            {LCD_CMAP_R3, LCD_CMAP_B2, LCD_CMAP_R1, LCD_CMAP_B0},
            {LCD_CMAP_G3, LCD_CMAP_R2, LCD_CMAP_G1, LCD_CMAP_R0},
        },
    };
    memset(info, 0, sizeof(struct panel_extend_para));

    items = sizeof(lcd_gamma_tbl) / 2;
    for (i = 0; i < items - 1; i++) {
        u32 num = lcd_gamma_tbl[i + 1][0] - lcd_gamma_tbl[i][0];

        for (j = 0; j < num; j++) {
            u32 value = 0;

            value = lcd_gamma_tbl[i][1] +
                ((lcd_gamma_tbl[i + 1][1] - lcd_gamma_tbl[i][1]) * j) / num;
            info->lcd_gamma_tbl[lcd_gamma_tbl[i][0] + j] =
                (value << 16) + (value << 8) + value;
        }
    }

    info->lcd_gamma_tbl[255] = (lcd_gamma_tbl[items - 1][1] << 16) +
                               (lcd_gamma_tbl[items - 1][1] << 8) +
                               lcd_gamma_tbl[items - 1][1];

    memcpy(info->lcd_cmap_tbl, lcd_cmap_tbl, sizeof(lcd_cmap_tbl));
}

static s32 lcd_open_flow(u32 sel)
{
    syslog(LOG_INFO, "[LCD_FLOW] Starting LCD open flow, sel=%u\n", sel);
    LCD_OPEN_FUNC(sel, lcd_power_on, 10);
    syslog(LOG_INFO, "[LCD_FLOW] Step 1: Power on scheduled\n");
    LCD_OPEN_FUNC(sel, lcd_panel_init, 10);
    syslog(LOG_INFO, "[LCD_FLOW] Step 2: Panel init scheduled\n");
    LCD_OPEN_FUNC(sel, sunxi_lcd_tcon_enable, 50);
    syslog(LOG_INFO, "[LCD_FLOW] Step 3: TCON enable scheduled\n");
    LCD_OPEN_FUNC(sel, lcd_bl_open, 0);
    syslog(LOG_INFO, "[LCD_FLOW] Step 4: Backlight open scheduled\n");

    return 0;
}

static s32 lcd_close_flow(u32 sel)
{
    LCD_CLOSE_FUNC(sel, lcd_bl_close, 0);
    LCD_CLOSE_FUNC(sel, sunxi_lcd_tcon_disable, 0);
    LCD_CLOSE_FUNC(sel, lcd_panel_exit, 200);
    LCD_CLOSE_FUNC(sel, lcd_power_off, 500);

    return 0;
}

static void lcd_power_on(u32 sel)
{
    syslog(LOG_INFO, "[LCD_PWR] Starting power on sequence, sel=%u\n", sel);
    panel_reset(sel, GPIO_DATA_LOW);
    syslog(LOG_INFO, "[LCD_PWR] Step 1: RESET LOW\n");
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC1);
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC3);
    syslog(LOG_INFO, "[LCD_PWR] Step 2: VDD 1.8V enabled (DCDC1, DCDC3)\n");
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_pin_cfg(sel, 1);
    syslog(LOG_INFO, "[LCD_PWR] Step 3: Pin config enabled\n");
    sunxi_lcd_delay_ms(50);
    panel_reset(sel, GPIO_DATA_HIGH);
    syslog(LOG_INFO, "[LCD_PWR] Step 4: RESET HIGH\n");
    sunxi_lcd_delay_ms(20);
    panel_reset(sel, GPIO_DATA_LOW);
    syslog(LOG_INFO, "[LCD_PWR] Step 5: RESET LOW\n");
    sunxi_lcd_delay_ms(30);
    panel_reset(sel, GPIO_DATA_HIGH);
    syslog(LOG_INFO, "[LCD_PWR] Step 6: RESET HIGH (final)\n");
    sunxi_lcd_delay_ms(30);
    sunxi_lcd_power_enable(sel, AXP2101_ID_DCDC2);
    syslog(LOG_INFO, "[LCD_PWR] Step 7: AVDD 3.3V enabled (DCDC2)\n");
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_dsi_clk_enable(sel);
    syslog(LOG_INFO, "[LCD_PWR] Step 8: DSI clock enabled\n");
}

static void lcd_power_off(u32 sel)
{
    sunxi_lcd_dsi_clk_disable(sel);
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC2);
    sunxi_lcd_delay_ms(20);
    panel_reset(sel, GPIO_DATA_LOW);
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_pin_cfg(sel, 0);
    sunxi_lcd_delay_ms(20);
    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC3);
    sunxi_lcd_power_disable(sel, AXP2101_ID_DCDC1);
}

static void lcd_bl_open(u32 sel)
{
    syslog(LOG_INFO, "[LCD_BL] Starting backlight open, sel=%u\n", sel);
    sunxi_lcd_pwm_enable(sel);
    syslog(LOG_INFO, "[LCD_BL] PWM enabled\n");
    sunxi_lcd_delay_ms(100);
    sunxi_lcd_backlight_enable(sel);
    syslog(LOG_INFO, "[LCD_BL] Backlight power enabled\n");
    sunxi_lcd_delay_ms(100);
    syslog(LOG_INFO, "[LCD_BL] Backlight open completed\n");
}

static void lcd_bl_close(u32 sel)
{
    sunxi_lcd_backlight_disable(sel);
    sunxi_lcd_pwm_disable(sel);
    sunxi_lcd_delay_ms(200);
}

static void lcd_panel_init(u32 sel)
{
    u8 data_buf[64];

    syslog(LOG_INFO, "[LCD_INIT] Starting panel initialization (DTS-based), sel=%lu\n", sel);

    sunxi_lcd_dsi_dcs_write_1para(sel, 0xB0, 0x04);
    syslog(LOG_INFO, "[LCD_INIT] DCS B0=0x04\n");
    sunxi_lcd_delay_ms(0);

    sunxi_lcd_dsi_dcs_write_1para(sel, 0xD6, 0x01);
    syslog(LOG_INFO, "[LCD_INIT] DCS D6=0x01\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x14; data_buf[1] = 0x00; data_buf[2] = 0x00;
    data_buf[3] = 0x00; data_buf[4] = 0x00; data_buf[5] = 0x00;
    sunxi_lcd_dsi_dcs_write(sel, 0xB3, data_buf, 6);
    syslog(LOG_INFO, "[LCD_INIT] DCS B3 len=6\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x0C; data_buf[1] = 0x00;
    sunxi_lcd_dsi_dcs_write(sel, 0xB4, data_buf, 2);
    syslog(LOG_INFO, "[LCD_INIT] DCS B4 len=2\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x4B; data_buf[1] = 0xDB; data_buf[2] = 0x00;
    sunxi_lcd_dsi_dcs_write(sel, 0xB6, data_buf, 3);
    syslog(LOG_INFO, "[LCD_INIT] DCS B6 len=3\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x04; data_buf[1] = 0x60; data_buf[2] = 0x00; data_buf[3] = 0x3F;
    data_buf[4] = 0x98; data_buf[5] = 0xF3; data_buf[6] = 0xC7; data_buf[7] = 0x2F;
    data_buf[8] = 0xFF; data_buf[9] = 0xFF; data_buf[10] = 0xFF; data_buf[11] = 0x5D;
    data_buf[12] = 0x63; data_buf[13] = 0xAC; data_buf[14] = 0xB9; data_buf[15] = 0xFF;
    data_buf[16] = 0xFF; data_buf[17] = 0xFF; data_buf[18] = 0xE4; data_buf[19] = 0x8B;
    data_buf[20] = 0x7F; data_buf[21] = 0x0C; data_buf[22] = 0xF8; data_buf[23] = 0x00;
    data_buf[24] = 0x00; data_buf[25] = 0x00; data_buf[26] = 0x00; data_buf[27] = 0x00;
    data_buf[28] = 0x6B; data_buf[29] = 0x11; data_buf[30] = 0x02; data_buf[31] = 0x21;
    data_buf[32] = 0x00; data_buf[33] = 0x01; data_buf[34] = 0x11;
    sunxi_lcd_dsi_dcs_write(sel, 0xC1, data_buf, 35);
    syslog(LOG_INFO, "[LCD_INIT] DCS C1 len=35\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x31; data_buf[1] = 0xF7; data_buf[2] = 0x80; data_buf[3] = 0x08;
    data_buf[4] = 0x08; data_buf[5] = 0x00; data_buf[6] = 0x00; data_buf[7] = 0x08;
    sunxi_lcd_dsi_dcs_write(sel, 0xC2, data_buf, 8);
    syslog(LOG_INFO, "[LCD_INIT] DCS C2 len=8\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x70; data_buf[1] = 0x00; data_buf[2] = 0x00; data_buf[3] = 0x00;
    data_buf[4] = 0x00; data_buf[5] = 0x00; data_buf[6] = 0x00; data_buf[7] = 0x00;
    data_buf[8] = 0x00; data_buf[9] = 0x01; data_buf[10] = 0x06;
    sunxi_lcd_dsi_dcs_write(sel, 0xC4, data_buf, 11);
    syslog(LOG_INFO, "[LCD_INIT] DCS C4 len=11\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0xC8; data_buf[1] = 0x01; data_buf[2] = 0x76; data_buf[3] = 0x06;
    data_buf[4] = 0x6A; data_buf[5] = 0x00; data_buf[6] = 0x00; data_buf[7] = 0x00;
    data_buf[8] = 0x00; data_buf[9] = 0x00; data_buf[10] = 0x00; data_buf[11] = 0x00;
    data_buf[12] = 0x00; data_buf[13] = 0x00; data_buf[14] = 0x00; data_buf[15] = 0x00;
    data_buf[16] = 0x00; data_buf[17] = 0x0A; data_buf[18] = 0x1C; data_buf[19] = 0x07;
    data_buf[20] = 0xC8;
    sunxi_lcd_dsi_dcs_write(sel, 0xC6, data_buf, 21);
    syslog(LOG_INFO, "[LCD_INIT] DCS C6 len=21\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x02; data_buf[1] = 0x0E; data_buf[2] = 0x17; data_buf[3] = 0x22;
    data_buf[4] = 0x31; data_buf[5] = 0x41; data_buf[6] = 0x49; data_buf[7] = 0x58;
    data_buf[8] = 0x3C; data_buf[9] = 0x45; data_buf[10] = 0x52; data_buf[11] = 0x61;
    data_buf[12] = 0x6A; data_buf[13] = 0x71; data_buf[14] = 0x7C; data_buf[15] = 0x02;
    data_buf[16] = 0x0E; data_buf[17] = 0x17; data_buf[18] = 0x22; data_buf[19] = 0x31;
    data_buf[20] = 0x41; data_buf[21] = 0x49; data_buf[22] = 0x58; data_buf[23] = 0x3C;
    data_buf[24] = 0x45; data_buf[25] = 0x52; data_buf[26] = 0x61; data_buf[27] = 0x6A;
    data_buf[28] = 0x71; data_buf[29] = 0x7C;
    sunxi_lcd_dsi_dcs_write(sel, 0xC7, data_buf, 30);
    syslog(LOG_INFO, "[LCD_INIT] DCS C7 len=30 (GAMMA)\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0xFF; data_buf[1] = 0xFF; data_buf[2] = 0xFF; data_buf[3] = 0xFF;
    data_buf[4] = 0x00; data_buf[5] = 0x00; data_buf[6] = 0x00; data_buf[7] = 0x00;
    data_buf[8] = 0x00; data_buf[9] = 0x00; data_buf[10] = 0x00; data_buf[11] = 0x00;
    data_buf[12] = 0xE0; data_buf[13] = 0x00; data_buf[14] = 0x00;
    sunxi_lcd_dsi_dcs_write(sel, 0xCB, data_buf, 15);
    syslog(LOG_INFO, "[LCD_INIT] DCS CB len=15\n");
    sunxi_lcd_delay_ms(0);

    sunxi_lcd_dsi_dcs_write_1para(sel, 0xCC, 0x0E);
    syslog(LOG_INFO, "[LCD_INIT] DCS CC=0x0E\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x11; data_buf[1] = 0x00; data_buf[2] = 0x00; data_buf[3] = 0x54;
    data_buf[4] = 0xCA; data_buf[5] = 0x40; data_buf[6] = 0x19; data_buf[7] = 0x19;
    data_buf[8] = 0x09; data_buf[9] = 0x00;
    sunxi_lcd_dsi_dcs_write(sel, 0xD0, data_buf, 10);
    syslog(LOG_INFO, "[LCD_INIT] DCS D0 len=10\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x04; data_buf[1] = 0x48; data_buf[2] = 0x06; data_buf[3] = 0x0D;
    sunxi_lcd_dsi_dcs_write(sel, 0xD1, data_buf, 4);
    syslog(LOG_INFO, "[LCD_INIT] DCS D1 len=4\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x1B; data_buf[1] = 0x33; data_buf[2] = 0xBB; data_buf[3] = 0xBB;
    data_buf[4] = 0xB3; data_buf[5] = 0x33; data_buf[6] = 0x33; data_buf[7] = 0x33;
    data_buf[8] = 0x33; data_buf[9] = 0x00; data_buf[10] = 0x01; data_buf[11] = 0x00;
    data_buf[12] = 0x00; data_buf[13] = 0xD8; data_buf[14] = 0xA0; data_buf[15] = 0x08;
    data_buf[16] = 0x4C; data_buf[17] = 0x4C; data_buf[18] = 0x33; data_buf[19] = 0x33;
    data_buf[20] = 0x72; data_buf[21] = 0x12; data_buf[22] = 0x8A; data_buf[23] = 0x57;
    data_buf[24] = 0x3D; data_buf[25] = 0xBC;
    sunxi_lcd_dsi_dcs_write(sel, 0xD3, data_buf, 26);
    syslog(LOG_INFO, "[LCD_INIT] DCS D3 len=26\n");
    sunxi_lcd_delay_ms(0);

    data_buf[0] = 0x06; data_buf[1] = 0x00; data_buf[2] = 0x00; data_buf[3] = 0x01;
    data_buf[4] = 0x2B; data_buf[5] = 0x01; data_buf[6] = 0x2B;
    sunxi_lcd_dsi_dcs_write(sel, 0xD5, data_buf, 7);
    syslog(LOG_INFO, "[LCD_INIT] DCS D5 len=7\n");
    sunxi_lcd_delay_ms(0);

    sunxi_lcd_dsi_dcs_write_1para(sel, DSI_DCS_SET_PIXEL_FORMAT, 0x70);
    syslog(LOG_INFO, "[LCD_INIT] DCS SET_PIXEL_FORMAT (0x3A) = 0x70\n");
    sunxi_lcd_delay_ms(0);

    sunxi_lcd_dsi_dcs_write_1para(sel, 0xDE, 0x00);
    syslog(LOG_INFO, "[LCD_INIT] DCS DE=0x00\n");
    sunxi_lcd_delay_ms(0);

    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_SET_DISPLAY_ON);
    syslog(LOG_INFO, "[LCD_INIT] DCS SET_DISPLAY_ON (0x29)\n");
    sunxi_lcd_delay_ms(80);

    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_EXIT_SLEEP_MODE);
    syslog(LOG_INFO, "[LCD_INIT] DCS EXIT_SLEEP_MODE (0x11) - panel init completed\n");
    sunxi_lcd_delay_ms(120);
}

static void lcd_panel_exit(u32 sel)
{
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_SET_DISPLAY_OFF);
    sunxi_lcd_delay_ms(120);
    sunxi_lcd_dsi_dcs_write_0para(sel, DSI_DCS_ENTER_SLEEP_MODE);
    sunxi_lcd_delay_ms(0);
}

static s32 lcd_user_defined_func(u32 sel, u32 para1, u32 para2, u32 para3)
{
    bsp_disp_lcd_set_bright(sel, 60);
    return 0;
}

struct __lcd_panel stl5_5_30_258_k_panel = {
    .name = "stl5_5_30_258_k",
    .func = {
        .cfg_panel_info = lcd_cfg_panel_info,
        .cfg_open_flow = lcd_open_flow,
        .cfg_close_flow = lcd_close_flow,
        .lcd_user_defined_func = lcd_user_defined_func,
    },
};