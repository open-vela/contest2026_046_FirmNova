#include <stdint.h>
#include <hal_clk.h>
#include <hal_gpio.h>
#include "../disp/disp_sys_intf.h"

typedef uint32_t u32;
typedef int32_t  s32;

#include "disp_board_config.h"

struct property_t g_lcd0_config[] = {
    {
        .name = "lcd_used",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "lcd_driver_name",
        .type = PROPERTY_STRING,
        .v.str = "stl5_5_30_258_k",
    },
    {
        .name = "lcd_if",
        .type = PROPERTY_INTGER,
        .v.value = 4,
    },
    {
        .name = "lcd_x",
        .type = PROPERTY_INTGER,
        .v.value = 1080,
    },
    {
        .name = "lcd_y",
        .type = PROPERTY_INTGER,
        .v.value = 1920,
    },
    {
        .name = "lcd_width",
        .type = PROPERTY_INTGER,
        .v.value = 74,
    },
    {
        .name = "lcd_height",
        .type = PROPERTY_INTGER,
        .v.value = 133,
    },
    {
        .name = "lcd_dclk_freq",
        .type = PROPERTY_INTGER,
        .v.value = 146,
    },
    {
        .name = "lcd_dsi_if",
        .type = PROPERTY_INTGER,
        .v.value = LCD_DSI_IF_VIDEO_MODE,
    },
    {
        .name = "lcd_dsi_lane",
        .type = PROPERTY_INTGER,
        .v.value = 4,
    },
    {
        .name = "lcd_dsi_format",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_dsi_te",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_dsi_eotp",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "lcd_dsi_port_num",
        .type = PROPERTY_INTGER,
        .v.value = 3,
    },
    {
        .name = "lcd_dsi_clk_rate",
        .type = PROPERTY_INTGER,
        .v.value = 439,
    },
    {
        .name = "lcd_ht",
        .type = PROPERTY_INTGER,
        .v.value = 1248,
    },
    {
        .name = "lcd_hbp",
        .type = PROPERTY_INTGER,
        .v.value = 80,
    },
    {
        .name = "lcd_hfp",
        .type = PROPERTY_INTGER,
        .v.value = 80,
    },
    {
        .name = "lcd_hspw",
        .type = PROPERTY_INTGER,
        .v.value = 8,
    },
    {
        .name = "lcd_vt",
        .type = PROPERTY_INTGER,
        .v.value = 1955,
    },
    {
        .name = "lcd_vbp",
        .type = PROPERTY_INTGER,
        .v.value = 20,
    },
    {
        .name = "lcd_vfp",
        .type = PROPERTY_INTGER,
        .v.value = 10,
    },
    {
        .name = "lcd_vspw",
        .type = PROPERTY_INTGER,
        .v.value = 5,
    },
    {
        .name = "lcd_frm",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_pwm_used",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "lcd_pwm_ch",
        .type = PROPERTY_INTGER,
        .v.value = 4,
    },
    {
        .name = "lcd_pwm_freq",
        .type = PROPERTY_INTGER,
        .v.value = 1000,
    },
    {
        .name = "lcd_pwm_pol",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_pwm_max_limit",
        .type = PROPERTY_INTGER,
        .v.value = 255,
    },
    {
        .name = "lcd_backlight",
        .type = PROPERTY_INTGER,
        .v.value = 100,
    },
    {
        .name = "lcd_backlight_curve",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_bl_en_power",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "lcd_gpio_0",
        .type = PROPERTY_GPIO,
        .v.gpio_list = {
            .gpio_name = "PD19",
            .port = 3,
            .port_num = 19,
            .mul_sel = GPIO_MUXSEL_OUT,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(19),
        },
    },
    {
        .name = "lcd_bl_en",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD20",
            .port = 3,
            .port_num = 20,
            .mul_sel = GPIO_MUXSEL_FUNCTION5,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_HIGH,
            .gpio = GPIOD(20),
        },
    },
    {
        .name = "dsi_dp0",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD0",
            .port = 3,
            .port_num = 0,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(0),
        },
    },
    {
        .name = "dsi_dn0",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD1",
            .port = 3,
            .port_num = 1,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(1),
        },
    },
    {
        .name = "dsi_dp1",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD2",
            .port = 3,
            .port_num = 2,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(2),
        },
    },
    {
        .name = "dsi_dn1",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD3",
            .port = 3,
            .port_num = 3,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(3),
        },
    },
    {
        .name = "dsi_ckp",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD4",
            .port = 3,
            .port_num = 4,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(4),
        },
    },
    {
        .name = "dsi_ckn",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD5",
            .port = 3,
            .port_num = 5,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(5),
        },
    },
    {
        .name = "dsi_dp2",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD6",
            .port = 3,
            .port_num = 6,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(6),
        },
    },
    {
        .name = "dsi_dn2",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD7",
            .port = 3,
            .port_num = 7,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(7),
        },
    },
    {
        .name = "dsi_dp3",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD8",
            .port = 3,
            .port_num = 8,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(8),
        },
    },
    {
        .name = "dsi_dn3",
        .type = PROPERTY_PIN,
        .v.gpio_list = {
            .gpio_name = "PD9",
            .port = 3,
            .port_num = 9,
            .mul_sel = GPIO_MUXSEL_FUNCTION4,
            .pull = GPIO_PULL_DOWN_DISABLED,
            .drv_level = GPIO_DRIVING_LEVEL3,
            .data = GPIO_DATA_LOW,
            .gpio = GPIOD(9),
        },
    },
    {
        .name = "lcd_power0",
        .type = PROPERTY_POWER,
        .v.power = {
            .power_name = "dcdc1",
            .power_type = AXP2101_REGULATOR,
            .power_id = AXP2101_ID_DCDC1,
            .power_vol = 1800000,
            .always_on = true,
        },
    },
    {
        .name = "lcd_power1",
        .type = PROPERTY_POWER,
        .v.power = {
            .power_name = "dcdc3",
            .power_type = AXP2101_REGULATOR,
            .power_id = AXP2101_ID_DCDC3,
            .power_vol = 1800000,
            .always_on = true,
        },
    },
    {
        .name = "lcd_power2",
        .type = PROPERTY_POWER,
        .v.power = {
            .power_name = "dcdc2",
            .power_type = AXP2101_REGULATOR,
            .power_id = AXP2101_ID_DCDC2,
            .power_vol = 3300000,
            .always_on = true,
        },
    },
    {
        .name = "lcd_bright_curve_en",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "lcd_size",
        .type = PROPERTY_STRING,
        .v.str = "1080x1920",
    },
    {
        .name = "lcd_model_name",
        .type = PROPERTY_STRING,
        .v.str = "STL5.5_30_258_K",
    },
};

struct property_t g_disp_config[] = {
    {
        .name = "disp_init_enable",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "disp_mode",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
    {
        .name = "screen0_output_type",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "screen0_output_mode",
        .type = PROPERTY_INTGER,
        .v.value = 4,
    },
    {
        .name = "screen1_output_type",
        .type = PROPERTY_INTGER,
        .v.value = 4,
    },
    {
        .name = "fb0_buffer_num",
        .type = PROPERTY_INTGER,
        .v.value = 2,
    },
    {
        .name = "fb0_width",
        .type = PROPERTY_INTGER,
        .v.value = 1080,
    },
    {
        .name = "fb0_height",
        .type = PROPERTY_INTGER,
        .v.value = 1920,
    },
#ifdef CONFIG_SUNXI_DISP2_FB_HW_ROTATION_SUPPORT
    {
        .name = "disp_rotation_used",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
    {
        .name = "degree0",
        .type = PROPERTY_INTGER,
        .v.value = 1,
    },
#endif
};

struct property_t g_lcd1_config[] = {
    {
        .name = "lcd_used",
        .type = PROPERTY_INTGER,
        .v.value = 0,
    },
};

struct property_t *g_lcd0_config_list[] = {
    g_lcd0_config,
    NULL,
    NULL,
};

u32 g_lcd0_config_len_list[] = {
    sizeof(g_lcd0_config) / sizeof(struct property_t),
    sizeof(g_lcd1_config) / sizeof(struct property_t),
};

u32 g_lcd0_config_list_len = sizeof(g_lcd0_config_len_list) / sizeof(u32);
u32 g_lcd1_config_len = sizeof(g_lcd1_config) / sizeof(struct property_t);
u32 g_disp_config_len = sizeof(g_disp_config) / sizeof(struct property_t);
u32 g_lcd0_config_len = sizeof(g_lcd0_config) / sizeof(struct property_t);