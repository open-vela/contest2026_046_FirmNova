# Rivotek Platform Decoupling Guide

This document describes the first-stage decoupling for `vendor/allwinnertech/rivotek`.

## Goals

- Keep business logic unchanged.
- Move platform-specific settings to a single abstraction layer.
- Provide a stable extension point for chip/board migration.

## What Is Abstracted

The new platform abstraction currently centralizes:

- Framebuffer device path
- Key input primary/fallback device paths
- External Bluetooth UART device path
- Whether app startup should call `boardctl(BOARDIOC_INIT)`

## New Files

- `platform/include/rivotek_platform.h`
- `platform/src/rivotek_platform_default.c`

## Integration Changes

- `apps/scooterdemo/Makefile` now builds `rivotek_platform_default.c`.
- Runtime callers use `rivotek_platform_config_get()` instead of hard-coded paths:
  - `core/scooterdemo.c`
  - `services/input_service/src/key_processor.c`
  - `integrations/bluetooth_external/src/bt_music_impl_external.c`

## Kconfig Options

In `apps/scooterdemo/Kconfig`:

- `RIVOTEK_PLATFORM_NEED_BOARDINIT`
- `RIVOTEK_PLATFORM_FB_PATH`
- `RIVOTEK_PLATFORM_KEY_DEVICE_PRIMARY`
- `RIVOTEK_PLATFORM_KEY_DEVICE_FALLBACK`
- `RIVOTEK_PLATFORM_BT_UART_PATH`

## How To Port To Another Chip/Board

1. Adjust Kconfig defaults for your board, or configure them in defconfig.
2. If runtime customization is needed, implement this symbol in board-specific code:

```c
void rivotek_platform_customize(rivotek_platform_config_t *config)
{
    config->fb_path = "/dev/lcd1";
    config->key_device_primary = "/dev/input/event0";
    config->key_device_fallback = "";
    config->bt_uart_path = "/dev/ttyS2";
    config->need_board_init = false;
}
```

`rivotek_platform_customize()` is weak in default implementation and can be overridden by a board-specific strong symbol.

## Next Recommended Decoupling Steps

- Introduce a pure interface layer for audio/weather/input providers in `services/`.
- Split UI and device services into independent static libraries.
- Replace remaining Allwinner-specific include paths in Makefiles with overridable variables.
- Add per-platform profile folders, for example:
  - `platform_profiles/r528/`
  - `platform_profiles/<new_chip>/`

## Stage 2 (Completed)

This codebase now includes a second-stage structural decoupling:

- App-specific sources and headers were moved out of shared root folders:
  - from `core/` to `apps/scooterdemo/core/`
  - from `include/` to `apps/scooterdemo/include/`
  - from `apps/scooterdemo/scooterdemo_ui/` to `apps/scooterdemo/ui/`
- Services now have independent configuration entries:
  - `services/<name>/Kconfig`
- Services now build from the aggregated service target:
  - `services/Makefile`

## Stage 3 (Completed)

The rivotek tree now separates product, service, platform, and integration
code:

- `services/` contains shared Rivotek service modules.
- `platform/` contains the board/platform abstraction layer.
- `integrations/bluetooth_external/` contains the external Bluetooth module
  integration.
- `apps/scooterdemo/drivers/` contains demo-only driver glue.
- Top-level `rivotek/Kconfig` now sources `services/Kconfig` so service enablement is managed centrally.

## Stage 4 (Completed)

Most Allwinner app modules were moved under the Rivotek tree. Only
`vendor/allwinnertech/apps/btn_reset` and `vendor/allwinnertech/apps/showlogo`
remain in the Allwinner apps root.

- Rivotek application/test modules moved to `rivotek/apps/`:
  `audio_test`, `bt_instance`, `factory_test`, `led_rgb`, `ltr553`,
  `luncher_mini`, `shtc3`, `sunxi_ir_tx`, `t070s140b_touch_test`, `wifi_test`.
- Rivotek service modules moved to `rivotek/services/`:
  `audioservice`, `map_service`, `wifi_display_service`.
- Shared Rivotek logging/config headers moved to `rivotek/services/common/include`.
- `apps/scooterdemo/Kconfig` now explicitly depends on enabled service modules.
- `apps/scooterdemo/Makefile` now assembles service source/include paths through service Makefile fragments.

In addition, service headers now expose neutral aliases (`rivotek_*_service_*`) while keeping legacy symbols for compatibility.
