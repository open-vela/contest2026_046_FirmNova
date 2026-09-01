#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "weather_service.h"

#define WEATHER_HTTP_BUFFER_LEN 8192

int weather_http_fetch_body(FAR const char *url, FAR char *buffer,
                            size_t buflen);
int weather_json_parse_current(FAR const char *payload,
                               FAR struct weather_info_s *info);

int weather_fetch_current_from_url(FAR const char *url,
                                   FAR struct weather_info_s *info)
{
  FAR char *buffer;
  int ret;

  if (url == NULL || info == NULL)
    {
      return -EINVAL;
    }

  buffer = (FAR char *)malloc(WEATHER_HTTP_BUFFER_LEN);
  if (buffer == NULL)
    {
      return -ENOMEM;
    }

  ret = weather_http_fetch_body(url, buffer, WEATHER_HTTP_BUFFER_LEN);
  if (ret >= 0)
    {
      ret = weather_json_parse_current(buffer, info);
    }

  free(buffer);
  return ret;
}

int weather_fetch_current(FAR struct weather_info_s *info)//从默认URL获取当前天气信息
{
  return weather_fetch_current_from_url(WEATHER_DEFAULT_URL, info);
}

FAR const char *weather_code_description(int weathercode)
{
  switch (weathercode)
    {
      case 0:
        return "晴天";
      case 1:
        return "晴朗";
      case 2:
        return "多云";
      case 3:
        return "阴天";
      case 45:
      case 48:
        return "雾天";
      case 51:
      case 53:
      case 55:
        return "毛毛雨";
      case 61:
      case 63:
      case 65:
        return "雨天";
      case 66:
      case 67:
        return "冻雨";
      case 71:
      case 73:
      case 75:
      case 77:
        return "雪天";
      case 80:
      case 81:
      case 82:
        return "阵雨";
      case 85:
      case 86:
        return "阵雪";
      case 95:
        return "雷暴";
      case 96:
      case 99:
        return "雷暴伴冰雹";
      default:
        return "未知天气";
    }
}