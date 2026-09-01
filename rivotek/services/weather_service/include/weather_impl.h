#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_WEATHER_IMPL_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_WEATHER_IMPL_H

#include <stdbool.h>

void rivotek_weather_component_init(void);
int rivotek_weather_component_start(void);
const char *rivotek_weather_component_get_city_text(void);
const char *rivotek_weather_component_get_temp_text(void);
const char *rivotek_weather_component_get_desc_text(void);
const char *rivotek_weather_component_get_air_quality_text(void);

static inline void rivotek_weather_service_init(void)
{
	rivotek_weather_component_init();
}

static inline int rivotek_weather_service_start(void)
{
	return rivotek_weather_component_start();
}

static inline const char *rivotek_weather_service_get_city_text(void)
{
	return rivotek_weather_component_get_city_text();
}

static inline const char *rivotek_weather_service_get_temp_text(void)
{
	return rivotek_weather_component_get_temp_text();
}

static inline const char *rivotek_weather_service_get_desc_text(void)
{
	return rivotek_weather_component_get_desc_text();
}

static inline const char *rivotek_weather_service_get_air_quality_text(void)
{
	return rivotek_weather_component_get_air_quality_text();
}

static inline void scooterdemo_weather_init(void)
{
	rivotek_weather_component_init();
}

static inline int scooterdemo_weather_start(void)
{
	return rivotek_weather_component_start();
}

static inline const char *scooterdemo_weather_get_city_text(void)
{
	return rivotek_weather_component_get_city_text();
}

static inline const char *scooterdemo_weather_get_temp_text(void)
{
	return rivotek_weather_component_get_temp_text();
}

static inline const char *scooterdemo_weather_get_desc_text(void)
{
	return rivotek_weather_component_get_desc_text();
}

static inline const char *scooterdemo_weather_get_air_quality_text(void)
{
	return rivotek_weather_component_get_air_quality_text();
}

#endif
