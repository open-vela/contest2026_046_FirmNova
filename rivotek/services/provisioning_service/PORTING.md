# Provisioning Service Porting Guide

`provisioning_service` owns network provisioning state, protocol payloads and
platform WiFi operations. The current main flow is pure WiFi provisioning:
the vehicle starts a temporary SoftAP, the APP scans the QR code, connects to
the vehicle SoftAP, sends the phone hotspot SSID/password over TCP, and then the
vehicle switches back to STA mode to connect to that hotspot.

## Enable

On R528/openvela, enable:

```text
CONFIG_RIVOTEK_PROVISIONING_SERVICE=y
CONFIG_RIVOTEK_QRCODE_SERVICE=y
CONFIG_LV_USE_QRCODE=y
```

`provisioning_service/Makefile` adds the service source, shell commands and
R528 WiFi port when `CONFIG_RIVOTEK_PROVISIONING_SERVICE` is enabled.

`qrcode_service` is optional from the provisioning core's point of view, but
needed if the vehicle UI should display the QR overlay.

## Upper-Layer API

Use the common start API. Do not add a separate public API for each WiFi
implementation detail.

```c
#include "rvt_provisioning_service.h"

ret = rvt_provisioning_start(RVT_PROVISIONING_TYPE_WIFI);
```

`RVT_PROVISIONING_TYPE_WIFI` starts the current vehicle-SoftAP WiFi flow.
`RVT_PROVISIONING_TYPE_BLUETOOTH` is reserved and currently returns `-ENOSYS`.

Stop the active provisioning flow:

```c
rvt_provisioning_stop();
```

Useful upper-layer APIs:

```c
int rvt_provisioning_start(rvt_provisioning_type_t type);
int rvt_provisioning_stop(void);
int rvt_provisioning_is_running(void);
int rvt_provisioning_get_session(rvt_provisioning_session_t *session);
int rvt_provisioning_set_event_callback(rvt_provisioning_event_cb_t cb,
                                        void *user_data);
int rvt_provisioning_has_provisioned(void);
int rvt_provisioning_set_provisioned(int provisioned);
int rvt_provisioning_get_network_status(
    rvt_provisioning_network_status_info_t *info);
int rvt_provisioning_set_network_status_callback(
    rvt_provisioning_network_status_cb_t cb,
    void *user_data);
int rvt_provisioning_notify_network_unavailable(void);
```

The network status API reports state only. UI or AI decides the final prompt:

```text
RVT_PROVISIONING_NETWORK_STATUS_UNPROVISIONED
RVT_PROVISIONING_NETWORK_STATUS_PROVISIONED_UNAVAILABLE
```

## Shell Commands

For local debugging, use:

```text
start_provisioning_wifi
stop_provisioning
```

`start_provisioning_wifi` has no argument. Legacy PIN is not part of the public
start API or shell command path.

## Saved WiFi and Boot Reconnect

On R528/openvela, the WAPI runtime config path is:

```text
/data/etc/wifi/wapi.conf
```

This path comes from `CONFIG_WIRELESS_WAPI_CONFIG_PATH`. The current WAPI JSON
loader reads one object named `wlan0`; it is not a multi-network priority list.
Therefore every successful provisioning overwrites the single saved WiFi
configuration.

Successful WiFi provisioning persists state in this order:

1. Write the connected target SSID/password to `/data/etc/wifi/wapi.conf`.
2. Set `/data/rvt_provisioned.flag`.
3. Notify upper layers that provisioning connected.

`rvt_provisioning_set_provisioned(0)` is the reset/unbind path. It clears both
`/data/rvt_provisioned.flag` and `/data/etc/wifi/wapi.conf`, so the next boot
is treated as unprovisioned.

Boot reconnect only needs a valid runtime WiFi config. The provisioned flag is
still kept as APP/product state, but the WiFi reconnect owner does not use it as
a boot gate:

```text
/data/etc/wifi/wapi.conf must be nonempty
```

Boot WiFi owner is selected by these config priorities:

1. `CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF=y` copies
   `/resource/etc/wifi/wapi_debug.conf` into `/data/etc/wifi/wapi.conf` if
   needed, then always uses the legacy `/etc/wifi/start_wifi.sh` path. This is a
   debug-only override and wins over `CONFIG_RIVOTEK_CONNECTIVITY_SERVICE`.
2. `CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF=n` and
   `CONFIG_RIVOTEK_CONNECTIVITY_SERVICE=y` uses the C `start_wifi` command from
   `connectivity_service/wifi`. It validates SSID/PSK and scans up to 3 times:
   immediately, then after 20s and 40s. If the target SSID is still not visible,
   boot skips WiFi reconnect instead of blindly connecting.
3. If both options are disabled, `rcS.nsh` falls back to the legacy
   `/etc/wifi/start_wifi.sh` path without copying any debug preset.

Packaged default `wapi.conf` files should stay empty. For lab debugging only,
enable `CONFIG_RIVOTEK_DEBUG_PRESET_WAPI_CONF`. This option defaults to `n` and
should remain disabled for production builds.

If product requirements later need multiple remembered APs, do not extend the
current `wapi.conf` shape directly. Add a separate provisioning-owned list store
such as `/data/rvt_wifi_profiles.conf`, select one visible profile by scan
result and priority, then write the selected single profile into WAPI's
`wapi.conf` before reconnect.

## WiFi Porting

Platform WiFi code implements `rvt_provisioning_wifi_port.h`:

```c
int rvt_prov_wifi_start_softap(const rvt_prov_softap_config_t *config);
int rvt_prov_wifi_get_softap_ip(char *ip_buf, int buf_len);
int rvt_prov_wifi_stop_softap(void);
int rvt_prov_wifi_submit_config(const char *ssid, const char *password);
int rvt_prov_wifi_is_ready(void);
```

Porting requirements:

1. `start_softap` starts the temporary vehicle AP using `config->ssid` and
   `config->password`.
2. `get_softap_ip` returns the real AP IPv4 address after AP startup. If it
   fails, the protocol falls back to `RVT_PROV_AP_IP_FALLBACK`.
3. `stop_softap` fully stops the vehicle AP and leaves WiFi ready for STA
   connection.
4. `submit_config` triggers the platform WiFi reconnect owner after the
   provisioning service has saved `/data/etc/wifi/wapi.conf`.
5. `is_ready` reports whether STA has obtained a usable IPv4 address. The
   provisioning service uses this as the success condition.

## QR Bridge

If QR display is enabled, initialize QR service from the UI owner, then register
the provisioning bridge:

```c
rvt_qrcode_ui_metrics_t qr_metrics;

/* Fill qr_metrics from the UI app's screen size and fonts. */
rvt_qrcode_init(screen, &qr_metrics);
rvt_provisioning_qrcode_bridge_init();
```

The bridge lives in `provisioning_service`, not `qrcode_service`. It listens to
provisioning events and maps them to generic `rvt_qrcode_show()` /
`rvt_qrcode_hide()` requests. This explicit bridge init is intentional because
weak UI hooks may be removed by static linking if no object keeps a strong
reference.

## Current WiFi Payload

The QR payload generated by `provisioning_service` is:

```text
rvt://wifi-pair?v=3&m=ap&nt=<network_type>&ssid=<car_ap_ssid>&psk=<car_ap_password>&ip=<car_ip>&port=<config_port>&n=<nonce>&exp=<ttl_seconds>
```

`nt` tells the APP which target network type should be selected by default:
`hotspot` means phone personal hotspot, and `router` means home/router WiFi.
The APP should still allow the user to switch manually. Missing `nt` is treated
as `hotspot` for compatibility with older QR payloads.

The vehicle only generates and displays this payload. The APP owns QR parsing,
connecting to the vehicle AP, and sending the phone hotspot configuration back
to `ip:port`.
