#ifndef RVT_PROVISIONING_PROTOCOL_H
#define RVT_PROVISIONING_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* 临时热点和协议消息中的随机数长度，用于区分每次配网会话。 */
#define RVT_PROV_NONCE_LEN      12
/* WiFi SSID 最大长度，按 802.11 常见 32 字节限制预留结尾 0。 */
#define RVT_PROV_SSID_MAX_LEN   32
/* 当前主流程中，车机临时 SoftAP 的 SSID 前缀。 */
#define RVT_PROV_AP_SSID_PREFIX "YADEA_CAR_"
/* 车机临时 SoftAP SSID 后缀随机字符长度。 */
#define RVT_PROV_AP_RANDOM_LEN  6
/* 车机临时 SoftAP 密码长度。 */
#define RVT_PROV_AP_PASSWORD_LEN 12
/*
 * 车机 SoftAP 网关 IP 兜底值。实际二维码发布的 IP 应优先由平台 port
 * 在 SoftAP 启动后读取当前 netdev 地址。
 */
#define RVT_PROV_AP_IP_FALLBACK "192.168.49.1"
#define RVT_PROV_CONFIG_PORT    39888

/* 当前主流程的车机 SoftAP 配置。APP 扫码连接该热点后下发真实手机热点信息。 */
typedef struct rvt_prov_softap_config {
    /* 车机临时 SoftAP 的 SSID。 */
    char ssid[RVT_PROV_SSID_MAX_LEN + 1];
    /* 车机临时 SoftAP 的随机密码。 */
    char password[RVT_PROV_AP_PASSWORD_LEN + 1];
    /* 本次配网会话随机数，APP 下发配置时必须带回。 */
    char nonce[RVT_PROV_NONCE_LEN + 1];
    /* 车机临时 SoftAP 网关 IP。 */
    char ip[16];
    /* 车机 TCP 配置服务监听端口。 */
    int port;
} rvt_prov_softap_config_t;

/*
 * 函数描述：生成一次性的车机 SoftAP SSID、密码、nonce、IP 和端口。
 * 入参：
 *   config - 输出 SoftAP 配置。
 * 返回值：成功返回 0；参数非法或随机生成失败返回负 errno。
 */
int rvt_prov_generate_softap_config(rvt_prov_softap_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
