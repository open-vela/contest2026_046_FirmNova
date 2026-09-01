#include "rvt_provisioning_protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RFC4648 base32 字符表，去掉 padding，避免 SSID/密码中出现特殊字符。 */
static const char g_base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

/*
 * 函数描述：生成指定长度的随机 base32 字符串。
 * 入参：
 *   out - 输出字符串缓冲区。
 *   out_len - 输出缓冲区长度。
 *   value_len - 需要生成的字符串长度。
 * 返回值：成功返回 0；参数非法返回负 errno。
 */
static int generate_base32(char *out, int out_len, int value_len)
{
    uint8_t random_bytes[RVT_PROV_AP_PASSWORD_LEN];
    int i;

    if (!out || out_len <= value_len ||
        value_len <= 0 ||
        value_len > (int)sizeof(random_bytes)) {
        return -EINVAL;
    }

    arc4random_buf(random_bytes, value_len);
    for (i = 0; i < value_len; i++) {
        out[i] = g_base32[random_bytes[i] & 0x1f];
    }
    out[value_len] = '\0';
    return 0;
}

/*
 * 函数描述：生成车机临时 SoftAP 的 SSID、密码、nonce、IP 和配置端口。
 * 入参：
 *   config - 输出 SoftAP 配置。
 * 返回值：成功返回 0；参数非法或随机生成失败返回负 errno。
 */
int rvt_prov_generate_softap_config(rvt_prov_softap_config_t *config)
{
    char suffix[RVT_PROV_AP_RANDOM_LEN + 1];
    int ret;

    if (!config) {
        return -EINVAL;
    }

    memset(config, 0, sizeof(*config));

    ret = generate_base32(suffix, sizeof(suffix), RVT_PROV_AP_RANDOM_LEN);
    if (ret < 0) {
        return ret;
    }

    ret = generate_base32(config->password, sizeof(config->password),
                          RVT_PROV_AP_PASSWORD_LEN);
    if (ret < 0) {
        return ret;
    }

    ret = generate_base32(config->nonce, sizeof(config->nonce),
                          RVT_PROV_NONCE_LEN);
    if (ret < 0) {
        return ret;
    }

    snprintf(config->ssid, sizeof(config->ssid), "%s%s",
             RVT_PROV_AP_SSID_PREFIX, suffix);
    snprintf(config->ip, sizeof(config->ip), "%s", RVT_PROV_AP_IP_FALLBACK);
    config->port = RVT_PROV_CONFIG_PORT;
    return 0;
}
