# QR Code Service Porting Guide

`qrcode_service` provides a shared LVGL QR-code overlay for WiFi provisioning,
AI device binding and other business payloads. Business modules build the
payload and display text; this service only owns the top-layer UI.

## Enable

On R528/openvela, enable:

```text
CONFIG_RIVOTEK_QRCODE_SERVICE=y
CONFIG_LV_USE_QRCODE=y
```

`CONFIG_RIVOTEK_QRCODE_SERVICE` defaults to `y` in
`rivotek/services/qrcode_service/Kconfig`. The board config also needs LVGL QR
support enabled, otherwise the API is compiled as a no-op and
`rvt_qrcode_show()` will return `-ENOSYS`.

## Build Integration

For an app that owns the LVGL screen, add the QR service source and include path
only when the feature is enabled. The R528 `scooterdemo` app uses this pattern:

```make
SCOOTER_SERVICES = $(RIVOTEK_DIR)/services
QRCODE_SERVICE = $(SCOOTER_SERVICES)/qrcode_service

ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
VPATH += :$(QRCODE_SERVICE)/src
CSRCS += rvt_qrcode.c
CFLAGS += ${INCDIR_PREFIX}$(QRCODE_SERVICE)/include
endif
```

Business bridges live in their own business service. For WiFi provisioning, add
the provisioning bridge source together with provisioning headers:

```make
PROVISIONING_SERVICE = $(SCOOTER_SERVICES)/provisioning_service

ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
VPATH += :$(PROVISIONING_SERVICE)/src
CFLAGS += ${INCDIR_PREFIX}$(PROVISIONING_SERVICE)/include
ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
CSRCS += rvt_provisioning_qrcode_bridge.c
endif
endif
```

## UI Integration

Initialize the service after the LVGL screen and UI metrics are ready:

```c
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
#include "rvt_qrcode.h"
#endif
#if defined(CONFIG_RIVOTEK_PROVISIONING_SERVICE) && \
    defined(CONFIG_RIVOTEK_QRCODE_SERVICE)
#include "rvt_provisioning_qrcode_bridge.h"
#endif

...

#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
rvt_qrcode_ui_metrics_t qr_metrics;

/* Fill qr_metrics from the UI app's screen size and fonts. */
rvt_qrcode_init(screen, &qr_metrics);
#ifdef CONFIG_RIVOTEK_PROVISIONING_SERVICE
if (rvt_provisioning_qrcode_bridge_init() < 0) {
    printf("Failed to init QR provisioning bridge\n");
}
#endif
#endif
```

Call `rvt_qrcode_poll()` from the LVGL UI timer or UI thread loop:

```c
#ifdef CONFIG_RIVOTEK_QRCODE_SERVICE
rvt_qrcode_poll();
#endif
```

Do not call LVGL APIs directly from business worker threads. `rvt_qrcode_show()`
and `rvt_qrcode_hide()` only enqueue a request protected by a mutex. The actual
LVGL object creation, update, countdown and hide are processed by
`rvt_qrcode_poll()` on the UI thread.

## Generic Display API

Use `rvt_qrcode_show()` for custom business payloads:

```c
rvt_qrcode_request_t req;

memset(&req, 0, sizeof(req));
req.biz = RVT_QRCODE_BIZ_AI_DEVICE;
req.payload = "rvt://ai-bind?v=1&token=...";
req.title = "设备绑定";
req.primary_text = "请使用手机 App 扫码";
req.secondary_text = "绑定完成后自动关闭";
req.hint_text = "二维码仅用于本次绑定。";
req.ttl_seconds = 120;

ret = rvt_qrcode_show(&req);
```

Use `rvt_qrcode_hide()` to close the overlay manually:

```c
rvt_qrcode_hide();
```

`ttl_seconds > 0` enables countdown and auto-hide. `ttl_seconds <= 0` keeps the
QR visible until `rvt_qrcode_hide()` is called.

For status-only pages, set `status_only = 1`. This keeps the same top-layer
overlay but hides the QR graphic. Set `hide_countdown = 1` if the page should
auto-hide without showing countdown text.

Input limits are defined in `rvt_qrcode.h`:

```text
RVT_QRCODE_PAYLOAD_MAX_LEN = 512
RVT_QRCODE_TEXT_MAX_LEN    = 96
```

`payload` must be non-empty and shorter than `RVT_QRCODE_PAYLOAD_MAX_LEN` unless
`status_only` is set. Text fields may be `NULL`; defaults are selected by
`biz`.

## Business Bridge Pattern

Business modules should not render LVGL directly. Prefer a small bridge file
inside the business service that maps business events to `rvt_qrcode_request_t`.
This keeps `qrcode_service` independent from provisioning, AI binding and future
business protocols.

WiFi provisioning uses this pattern:

1. `scooterdemo` calls `rvt_provisioning_qrcode_bridge_init()`.
2. The bridge registers `rvt_provisioning_set_event_callback()`.
3. On `RVT_PROVISIONING_EVENT_QR_SHOW`, the bridge maps the provisioning
   session to `rvt_qrcode_request_t` and calls `rvt_qrcode_show()`.
4. On `RVT_PROVISIONING_EVENT_CONNECTING`, `CONNECTED` or `FAILED`, the bridge
   uses a status-only QR request.
5. On hide or stopped events, the bridge calls `rvt_qrcode_hide()`.

This explicit registration is intentional. A weak UI hook alone can be dropped
by static linking when the bridge object has no strong reference. Do not replace
the bridge init with an empty function just to force linking.

For a new business type, add a bridge similar to:

```c
int rvt_qrcode_ai_bridge_init(void)
{
    return ai_service_set_event_callback(rvt_qrcode_ai_event_cb, NULL);
}
```

Then call that bridge init from the UI app after `rvt_qrcode_init()`.

## Porting Checklist

1. Enable `CONFIG_RIVOTEK_QRCODE_SERVICE` and `CONFIG_LV_USE_QRCODE`.
2. Add `rivotek/services/qrcode_service/include` to the UI app include path.
3. Add `rivotek/services/qrcode_service/src` to `VPATH` and include
   `rvt_qrcode.c` in `CSRCS`.
4. Initialize with `rvt_qrcode_init(screen, &qr_metrics)`.
5. Poll with `rvt_qrcode_poll()` from the LVGL UI thread/timer.
6. Add business bridges through explicit callback registration.
7. Keep QR payload generation inside the business module. The QR service should
   not know protocol details beyond display text and payload.
