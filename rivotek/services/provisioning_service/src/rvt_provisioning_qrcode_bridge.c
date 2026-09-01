#include "rvt_provisioning_qrcode_bridge.h"

#include "rvt_provisioning_service.h"
#include "rvt_qrcode.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define RVT_PROV_QRCODE_LOG(...) dprintf(STDOUT_FILENO, __VA_ARGS__)

/*
 * 函数描述：把 provisioning session 转换成 QR service 的通用显示请求。
 * 入参：
 *   session - 当前配网会话快照。
 * 返回值：无。
 */
static void rvt_provisioning_qrcode_show(
    const rvt_provisioning_session_t *session)
{
    rvt_qrcode_request_t request;
    char primary[RVT_QRCODE_TEXT_MAX_LEN];
    char secondary[RVT_QRCODE_TEXT_MAX_LEN];
    int ret;

    if (session == NULL || session->qr_payload[0] == '\0') {
        return;
    }

    memset(&request, 0, sizeof(request));
    memset(primary, 0, sizeof(primary));
    memset(secondary, 0, sizeof(secondary));

    snprintf(primary, sizeof(primary), "热点 %s", session->ap_ssid);
    snprintf(secondary, sizeof(secondary), "密码 %s", session->ap_password);

    request.biz = RVT_QRCODE_BIZ_WIFI_PROVISIONING;
    request.payload = session->qr_payload;
    request.title = "WiFi 配网";
    request.primary_text = primary;
    request.secondary_text = secondary;
    request.hint_text = "请用手机 App 扫描二维码，或打开 WiFi 手动连接。";
    request.ttl_seconds = session->ttl_seconds;

    ret = rvt_qrcode_show(&request);
    if (ret < 0) {
        RVT_PROV_QRCODE_LOG("provisioning: show QR failed ret=%d\n", ret);
    }
}

/*
 * 函数描述：显示配网状态页，不显示二维码图形，用于配网中、成功和失败提示。
 * 入参：
 *   session - 当前配网会话快照，可为空。
 *   title - 状态页标题。
 *   hint - 状态页提示文案。
 * 返回值：无。
 */
static void rvt_provisioning_qrcode_show_status(
    const rvt_provisioning_session_t *session,
    const char *title,
    const char *hint)
{
    rvt_qrcode_request_t request;
    int ret;

    memset(&request, 0, sizeof(request));
    request.biz = RVT_QRCODE_BIZ_WIFI_PROVISIONING;
    request.title = title;
    request.hint_text = hint;
    request.ttl_seconds = session ? session->ttl_seconds : 0;
    request.status_only = 1;
    request.hide_countdown = 1;

    ret = rvt_qrcode_show(&request);
    if (ret < 0) {
        RVT_PROV_QRCODE_LOG("provisioning: show status failed ret=%d\n",
                            ret);
    }
}

/*
 * 函数描述：接收 provisioning_service 事件，并驱动二维码显示或隐藏。
 * 入参：
 *   event - 配网事件。
 *   session - 当前配网会话快照。
 *   user_data - 回调透传上下文，当前未使用。
 * 返回值：无。
 */
static void rvt_provisioning_qrcode_event_cb(
    rvt_provisioning_event_t event,
    const rvt_provisioning_session_t *session,
    void *user_data)
{
    (void)user_data;

    if (event == RVT_PROVISIONING_EVENT_QR_SHOW) {
        RVT_PROV_QRCODE_LOG("provisioning: QR show event\n");
        rvt_provisioning_qrcode_show(session);
    } else if (event == RVT_PROVISIONING_EVENT_CONNECTING) {
        RVT_PROV_QRCODE_LOG("provisioning: connecting status event\n");
        rvt_provisioning_qrcode_show_status(
            session,
            "配网中",
            "已收到热点信息，正在连接手机热点，请稍候。");
    } else if (event == RVT_PROVISIONING_EVENT_CONNECTED) {
        RVT_PROV_QRCODE_LOG("provisioning: connected status event\n");
        rvt_provisioning_qrcode_show_status(session,
                                            "配网成功",
                                            "已连接手机热点。");
    } else if (event == RVT_PROVISIONING_EVENT_FAILED) {
        RVT_PROV_QRCODE_LOG("provisioning: failed status event\n");
        rvt_provisioning_qrcode_show_status(
            session,
            "配网失败",
            "请检查手机热点、名称和密码，然后重新配网。");
    } else {
        RVT_PROV_QRCODE_LOG("provisioning: QR hide event=%d\n", event);
        rvt_qrcode_hide();
    }
}

/*
 * 函数描述：注册 provisioning_service 到 qrcode_service 的事件桥接。
 * 入参：无。
 * 返回值：成功返回 0；注册失败返回负 errno。
 */
int rvt_provisioning_qrcode_bridge_init(void)
{
    return rvt_provisioning_set_event_callback(
        rvt_provisioning_qrcode_event_cb, NULL);
}
