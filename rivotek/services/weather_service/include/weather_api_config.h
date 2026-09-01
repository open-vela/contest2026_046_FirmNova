#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_WEATHER_API_CONFIG_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_WEATHER_API_CONFIG_H

/*
 * 固定公开天气接口地址。
 *
 * 约定服务端按请求来源公网 IP 自动定位，并返回 JSON，至少包含：
 * - city: 城市名
 * - temperature / temperature_c / temp: 当前温度
 * - weather / condition / weather_desc: 天气状况
 *
 * 可选返回：
 * - country
 * - latitude / lat
 * - longitude / lon
 */
#define SCOOTERDEMO_WEATHER_API_URL \
    "https://uapis.cn/api/v1/misc/weather?extended=true"

#define SCOOTERDEMO_WEATHER_AUTH_HEADER \
    "Authorization: Bearer uapi-pqmpu9jstR9W8cqFEL90diQWLjVhNchNZo40QPvI\r\n"

#endif
