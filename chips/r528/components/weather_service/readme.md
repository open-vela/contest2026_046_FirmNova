# 天气服务组件使用说明

## 概述

天气服务组件是一个基于 NuttX 的轻量级 HTTPS 天气客户端，它使用 Open-Meteo 天气 API 获取实时天气信息。该组件支持获取温度、风速、风向、天气代码等信息。

## 功能特性

- 支持 HTTPS 连接，使用 mbedTLS 进行安全通信
- 解析 Open-Meteo API 返回的 JSON 数据
- 提供简单的 C API 用于获取天气信息
- 支持自定义 URL 查询天气
- 包含天气代码描述转换功能

## 依赖项

- `NET` - 网络支持
- `NETUTILS_CJSON` - JSON 解析库
- `CRYPTO_MBEDTLS` - mbedTLS 加密库
- `MBEDTLS_NET_C` - mbedTLS 网络功能
- `MBEDTLS_SSL_SERVER_NAME_INDICATION` - SSL 服务器名称指示

## 配置选项

在 Kconfig 中启用天气组件：

```
CONFIG_COMPONENTS_WEATHER=y
```

## API 接口

### 数据结构

```c
#define WEATHER_DEFAULT_URL \
  "https://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074&current_weather=true&temperature_unit=celsius&timezone=Asia/Shanghai"

#define WEATHER_TIME_LEN 32
#define WEATHER_TIMEZONE_LEN 32

struct weather_info_s
{
  double latitude;              // 纬度
  double longitude;             // 经度
  double temperature_c;         // 温度（摄氏度）
  double windspeed_kmh;         // 风速（公里/小时）
  double winddirection_deg;     // 风向（度）
  int weathercode;              // 天气代码
  int is_day;                   // 是否白天（1=是，0=否）
  int interval_seconds;         // 时间间隔（秒）
  char observation_time[WEATHER_TIME_LEN];  // 观测时间
  char timezone[WEATHER_TIMEZONE_LEN];      // 时区
};
```

### 函数接口

#### `int weather_fetch_current(FAR struct weather_info_s *info)`

从默认 URL 获取当前天气信息。

- 参数：
  - `info`: 指向天气信息结构体的指针，用于存储返回的天气数据
- 返回值：
  - `0`: 成功
  - 负数: 错误码

#### `int weather_fetch_current_from_url(FAR const char *url, FAR struct weather_info_s *info)`

从指定的 URL 获取当前天气信息。

- 参数：
  - `url`: 指向天气 API URL 字符串的指针
  - `info`: 指向天气信息结构体的指针，用于存储返回的天气数据
- 返回值：
  - `0`: 成功
  - 负数: 错误码

#### `FAR const char *weather_code_description(int weathercode)`

根据天气代码获取天气描述。

- 参数：
  - `weathercode`: 天气代码
- 返回值：
  - 指向天气描述字符串的常量指针

## 使用示例

### 基本使用 - 获取默认位置天气

```c
#include "weather_service.h"

void get_weather_example(void)
{
  struct weather_info_s weather;
  int ret;
  
  // 获取默认位置（北京）的天气信息
  ret = weather_fetch_current(&weather);
  if (ret == 0)
  {
    printf("纬度: %.4f\n", weather.latitude);
    printf("经度: %.4f\n", weather.longitude);
    printf("温度: %.2f°C\n", weather.temperature_c);
    printf("风速: %.2f 公里/小时\n", weather.windspeed_kmh);
    printf("风向: %.2f 度\n", weather.winddirection_deg);
    printf("天气代码: %d\n", weather.weathercode);
    printf("天气描述: %s\n", weather_code_description(weather.weathercode));
    printf("观测时间: %s\n", weather.observation_time);
    printf("时区: %s\n", weather.timezone);
    printf("是否白天: %s\n", weather.is_day ? "是" : "否");
  }
  else
  {
    printf("获取天气信息失败，错误码: %d\n", ret);
  }
}
```

### 自定义位置天气查询

```c
#include "weather_service.h"

void get_custom_weather_example(void)
{
  struct weather_info_s weather;
  int ret;
  
  // 自定义查询上海的天气（纬度31.2304，经度121.4737）
  const char *shanghai_url = "https://api.open-meteo.com/v1/forecast?latitude=31.2304&longitude=121.4737&current_weather=true&temperature_unit=celsius&timezone=Asia/Shanghai";
  
  ret = weather_fetch_current_from_url(shanghai_url, &weather);
  if (ret == 0)
  {
    printf("上海天气:\n");
    printf("温度: %.2f°C\n", weather.temperature_c);
    printf("天气描述: %s\n", weather_code_description(weather.weathercode));
  }
  else
  {
    printf("获取上海天气信息失败，错误码: %d\n", ret);
  }
}
```

## 天气代码对照表

| 代码 | 描述 |
|------|------|
| 0 | 晴天 |
| 1, 2, 3 | 部分多云 |
| 45, 48 | 雾 |
| 51, 53, 55 | 毛毛雨 |
| 61, 63, 65 | 雨 |
| 66, 67 | 冻雨 |
| 71, 73, 75, 77 | 雪 |
| 80, 81, 82 | 阵雨 |
| 85, 86 | 阵雪 |
| 95 | 雷暴 |
| 96, 99 | 雷暴伴有冰雹 |
| 其他 | 未知 |

## 注意事项

1. 确保设备已连接到互联网并能访问 `api.open-meteo.com`
2. 默认使用 HTTPS 协议，需要 mbedTLS 支持
3. API 请求有频率限制，请避免过于频繁的调用
4. 设备内存应足够大以容纳 HTTP 响应（约 8KB 缓冲区）
5. 当前仅支持 Open-Meteo API，如需其他 API 可能需要修改代码

## 错误处理

函数返回负数表示错误，常见的错误码包括：

- `-EINVAL`: 无效参数
- `-ENOMEM`: 内存不足
- `-EIO`: I/O 错误
- `-ENOTSUP`: 不支持的协议
- `-EHOSTUNREACH`: 主机不可达
- `-EBADMSG`: 消息格式错误
- `-E2BIG`: 请求过大
- `-EOVERFLOW`: 溢出错误

## 故障排除

1. 如果无法获取天气信息，请检查网络连接
2. 确认 mbedTLS 相关配置已正确启用
3. 检查防火墙设置是否阻止了 HTTPS 请求
4. 验证设备时间和日期是否正确设置
5. 查看日志输出中的错误信息以帮助调试