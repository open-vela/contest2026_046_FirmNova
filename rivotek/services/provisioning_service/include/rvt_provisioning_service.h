#ifndef RVT_PROVISIONING_SERVICE_H
#define RVT_PROVISIONING_SERVICE_H

#include "rvt_provisioning_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 配网二维码 payload 最大长度，需要覆盖当前车机 SoftAP 方案。 */
#define RVT_PROVISIONING_QR_PAYLOAD_MAX_LEN 256

typedef enum {
    /* 未启动配网。 */
    RVT_PROVISIONING_STATE_IDLE = 0,
    /* 正在生成会话或启动临时 SoftAP。 */
    RVT_PROVISIONING_STATE_STARTING,
    /* 临时 SoftAP 已启动，等待 APP 连接并下发手机热点信息。 */
    RVT_PROVISIONING_STATE_WAITING_APP,
    /* 已收到手机热点信息，正在关闭临时 SoftAP 并切换 STA 回连。 */
    RVT_PROVISIONING_STATE_CONNECTING,
    /* 已连接到目标手机热点。 */
    RVT_PROVISIONING_STATE_CONNECTED,
    /* 配网失败或超时。 */
    RVT_PROVISIONING_STATE_FAILED,
    /* 外部主动停止配网。 */
    RVT_PROVISIONING_STATE_STOPPED,
} rvt_provisioning_state_t;

typedef enum {
    /* 纯 WiFi 配网：车机启动临时 SoftAP，APP 连接后下发手机热点信息。 */
    RVT_PROVISIONING_TYPE_WIFI = 0,
    /* 预留蓝牙配网入口，当前尚未实现。 */
    RVT_PROVISIONING_TYPE_BLUETOOTH,
} rvt_provisioning_type_t;

typedef enum {
    /* 通知 UI 显示二维码。 */
    RVT_PROVISIONING_EVENT_QR_SHOW = 0,
    /* 通知 UI 隐藏二维码。 */
    RVT_PROVISIONING_EVENT_QR_HIDE,
    /* APP 热点信息已收到，通知 UI 切换为配网中状态。 */
    RVT_PROVISIONING_EVENT_CONNECTING,
    /* 车机已连接到手机热点。 */
    RVT_PROVISIONING_EVENT_CONNECTED,
    /* 配网失败。 */
    RVT_PROVISIONING_EVENT_FAILED,
    /* 配网被停止。 */
    RVT_PROVISIONING_EVENT_STOPPED,
} rvt_provisioning_event_t;

typedef struct rvt_provisioning_session {
    /* 当前配网状态。 */
    rvt_provisioning_state_t state;
    /* 车机临时 SoftAP 的 SSID/密码。 */
    char ap_ssid[RVT_PROV_SSID_MAX_LEN + 1];
    char ap_password[RVT_PROV_AP_PASSWORD_LEN + 1];
    /* 本次配网会话随机数，APP 回传配置时必须带回。 */
    char nonce[RVT_PROV_NONCE_LEN + 1];
    /* APP 侧上报的手机标识，只用于日志和状态跟踪，不读取手机 MAC。 */
    char peer_phone_id[65];
    /* 与车机 TCP 配置服务建立连接的对端 IP:port。 */
    char peer_addr[48];
    /* 车机临时 SoftAP 的网关 IP 和配置端口。 */
    char ip[16];
    int port;
    /* UI 直接显示的二维码 payload，QR service 不解析协议内容。 */
    char qr_payload[RVT_PROVISIONING_QR_PAYLOAD_MAX_LEN];
    /* 二维码显示倒计时，单位秒；小于等于 0 表示不自动隐藏。 */
    int ttl_seconds;
    /* 最近一次配网结果，0 表示成功，负值为 errno 风格错误码。 */
    int result;
    /* 异步配网任务是否仍在运行。 */
    int running;
} rvt_provisioning_session_t;

typedef void (*rvt_provisioning_event_cb_t)(
    rvt_provisioning_event_t event,
    const rvt_provisioning_session_t *session,
    void *user_data);

typedef enum {
    /* 从未成功配网，当前网络不可用。 */
    RVT_PROVISIONING_NETWORK_STATUS_UNPROVISIONED = 0,
    /* 曾经成功配网，当前网络不可用。 */
    RVT_PROVISIONING_NETWORK_STATUS_PROVISIONED_UNAVAILABLE,
} rvt_provisioning_network_status_t;

typedef struct rvt_provisioning_network_status_info {
    /* 网络状态，上层可按状态自行决定 UI 或语音提示。 */
    rvt_provisioning_network_status_t status;
} rvt_provisioning_network_status_info_t;

typedef void (*rvt_provisioning_network_status_cb_t)(
    const rvt_provisioning_network_status_info_t *info,
    void *user_data);

/*
 * 函数描述：UI/AI 使用的高层异步启动接口，根据 type 选择配网方式。
 * 入参：
 *   type - 配网方式，当前支持 RVT_PROVISIONING_TYPE_WIFI。
 * 返回值：成功返回 0；已在运行、类型不支持或启动失败返回负 errno。
 */
int rvt_provisioning_start(rvt_provisioning_type_t type);

/*
 * 函数描述：停止当前配网流程，终止 STA 连接或车机 SoftAP，并通知 UI 隐藏二维码。
 * 入参：无。
 * 返回值：成功返回 0；底层停止失败返回负 errno。
 */
int rvt_provisioning_stop(void);

/*
 * 函数描述：查询配网服务是否处于启动、等待、已连接或 SoftAP 工作状态。
 * 入参：无。
 * 返回值：正在运行返回非 0；空闲返回 0。
 */
int rvt_provisioning_is_running(void);

/*
 * 函数描述：获取当前配网会话快照。
 * 入参：
 *   session - 输出当前配网会话快照。
 * 返回值：成功返回 0；参数非法返回负 errno。
 */
int rvt_provisioning_get_session(rvt_provisioning_session_t *session);

/*
 * 函数描述：注册配网事件回调，供 QR service 或其他业务模块接收状态变化。
 * 入参：
 *   cb - 事件回调函数，传空表示清除回调。
 *   user_data - 回调透传上下文。
 * 返回值：固定返回 0。
 */
int rvt_provisioning_set_event_callback(rvt_provisioning_event_cb_t cb,
                                        void *user_data);

/*
 * 函数描述：查询当前设备是否曾经成功完成配网。
 * 入参：无。
 * 返回值：曾经成功配网返回 1；未配过网或读取失败返回 0。
 */
int rvt_provisioning_has_provisioned(void);

/*
 * 函数描述：设置配网完成标志，供恢复出厂、解绑或调试命令重置状态。
 * 入参：
 *   provisioned - 非 0 表示已配网；0 表示未配网。
 * 返回值：成功返回 0；持久化失败返回负 errno。
 */
int rvt_provisioning_set_provisioned(int provisioned);

/*
 * 函数描述：根据配网完成标志生成网络状态信息。
 * 入参：
 *   info - 输出网络状态信息。
 * 返回值：成功返回 0；参数非法返回负 errno。
 */
int rvt_provisioning_get_network_status(
    rvt_provisioning_network_status_info_t *info);

/*
 * 函数描述：注册网络状态回调，供 AI 或应用层按需接收。
 * 入参：
 *   cb - 网络状态回调；NULL 表示清除回调。
 *   user_data - 回调透传上下文。
 * 返回值：固定返回 0。
 */
int rvt_provisioning_set_network_status_callback(
    rvt_provisioning_network_status_cb_t cb,
    void *user_data);

/*
 * 函数描述：通知上层当前网络不可用，并上报未配网或已配网但不可用状态。
 * 入参：无。
 * 返回值：成功返回 0；内部生成状态失败返回负 errno。
 */
int rvt_provisioning_notify_network_unavailable(void);

/*
 * 函数描述：可选 UI hook，默认是 weak 空实现；UI 代码需要复制 session 数据后切到 LVGL UI 线程处理。
 * 入参：
 *   event - 配网事件。
 *   session - 当前配网会话快照。
 * 返回值：无。
 */
void rvt_provisioning_ui_notify(rvt_provisioning_event_t event,
                                const rvt_provisioning_session_t *session);

/*
 * 函数描述：可选网络状态 hook，默认是 weak 空实现；AI 或应用可选择覆盖或使用回调注册接口。
 * 入参：
 *   info - 网络状态信息。
 * 返回值：无。
 */
void rvt_provisioning_network_status_notify(
    const rvt_provisioning_network_status_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
