#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_TIME_IMPL_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_SCOOTERDEMO_TIME_IMPL_H

#include <stdbool.h>
#include <stdint.h>

void rivotek_time_component_init(void);
void rivotek_time_component_update(uint32_t tick);
const char *rivotek_time_component_get_text(void);
bool rivotek_time_component_is_ready(void);

static inline void rivotek_time_service_init(void)
{
	rivotek_time_component_init();
}

static inline void rivotek_time_service_update(uint32_t tick)
{
	rivotek_time_component_update(tick);
}

static inline const char *rivotek_time_service_get_text(void)
{
	return rivotek_time_component_get_text();
}

static inline bool rivotek_time_service_is_ready(void)
{
	return rivotek_time_component_is_ready();
}

static inline void scooterdemo_time_init(void)
{
	rivotek_time_component_init();
}

static inline void scooterdemo_time_update(uint32_t tick)
{
	rivotek_time_component_update(tick);
}

static inline const char *scooterdemo_time_get_text(void)
{
	return rivotek_time_component_get_text();
}

static inline bool scooterdemo_time_is_ready(void)
{
	return rivotek_time_component_is_ready();
}

#endif