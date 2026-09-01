#ifndef __VENDOR_ALLWINNERTECH_CHIPS_R528_COMPONENTS_WEATHER_SERVICE_INCLUDE_WEATHER_SERVICE_H
#define __VENDOR_ALLWINNERTECH_CHIPS_R528_COMPONENTS_WEATHER_SERVICE_INCLUDE_WEATHER_SERVICE_H

#include <nuttx/config.h>

#include <stdbool.h>

#define WEATHER_DEFAULT_URL \
  "https://api.open-meteo.com/v1/forecast?latitude=29.5588&longitude=106.5491&current_weather=true&temperature_unit=celsius&timezone=Asia/Shanghai"

#define WEATHER_TIME_LEN 32
#define WEATHER_TIMEZONE_LEN 32

struct weather_info_s
{
  double latitude;
  double longitude;
  double temperature_c;
  double windspeed_kmh;
  double winddirection_deg;
  int weathercode;
  int is_day;
  int interval_seconds;
  char observation_time[WEATHER_TIME_LEN];
  char timezone[WEATHER_TIMEZONE_LEN];
};

#ifdef __cplusplus
extern "C"
{
#endif

int weather_fetch_current(FAR struct weather_info_s *info);
int weather_fetch_current_from_url(FAR const char *url,
                                   FAR struct weather_info_s *info);
FAR const char *weather_code_description(int weathercode);

#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_ALLWINNERTECH_CHIPS_R528_COMPONENTS_WEATHER_SERVICE_INCLUDE_WEATHER_SERVICE_H */
