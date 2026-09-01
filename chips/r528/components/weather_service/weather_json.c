#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <debug.h>

#include "netutils/cJSON.h"

#include "weather_service.h"

static int weather_json_get_number(FAR const cJSON *object,
                                   FAR const char *name,
                                   FAR double *value)
{
  FAR const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

  if (!cJSON_IsNumber(item))
    {
      return -ENOENT;
    }

  *value = cJSON_GetNumberValue(item);
  return 0;
}

static int weather_json_get_int(FAR const cJSON *object,
                                FAR const char *name,
                                FAR int *value)
{
  double number;
  int ret = weather_json_get_number(object, name, &number);

  if (ret < 0)
    {
      return ret;
    }

  *value = (int)number;
  return 0;
}

static void weather_json_get_string(FAR const cJSON *object,
                                    FAR const char *name,
                                    FAR char *buffer,
                                    size_t buflen)
{
  FAR const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

  if (!cJSON_IsString(item) || item->valuestring == NULL || buflen == 0)
    {
      if (buflen > 0)
        {
          buffer[0] = '\0';
        }

      return;
    }

  snprintf(buffer, buflen, "%s", item->valuestring);
}

int weather_json_parse_current(FAR const char *payload,
                               FAR struct weather_info_s *info)
{
  FAR cJSON *root;
  FAR cJSON *current;
  int ret;

  if (payload == NULL || info == NULL)
    {
      return -EINVAL;
    }

  root = cJSON_Parse(payload);
  if (root == NULL)
    {
      nerr("weather json parse failed near: %s\n", cJSON_GetErrorPtr());
      return -EBADMSG;
    }

  current = cJSON_GetObjectItemCaseSensitive(root, "current_weather");
  if (!cJSON_IsObject(current))
    {
      cJSON_Delete(root);
      return -EBADMSG;
    }

  memset(info, 0, sizeof(*info));

  ret = weather_json_get_number(root, "latitude", &info->latitude);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_number(root, "longitude", &info->longitude);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_number(current, "temperature", &info->temperature_c);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_number(current, "windspeed", &info->windspeed_kmh);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_number(current, "winddirection",
                                &info->winddirection_deg);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_int(current, "weathercode", &info->weathercode);
  if (ret < 0)
    {
      goto errout;
    }

  ret = weather_json_get_int(current, "is_day", &info->is_day);
  if (ret < 0)
    {
      info->is_day = -1;
    }

  ret = weather_json_get_int(current, "interval", &info->interval_seconds);
  if (ret < 0)
    {
      info->interval_seconds = 0;
    }

  weather_json_get_string(current, "time", info->observation_time,
                          sizeof(info->observation_time));
  weather_json_get_string(root, "timezone", info->timezone,
                          sizeof(info->timezone));

  cJSON_Delete(root);
  return 0;

errout:
  cJSON_Delete(root);
  return -EBADMSG;
}
