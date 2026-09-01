#ifndef __RIVOTEK_LOG_H__
#define __RIVOTEK_LOG_H__

#include <stdio.h>

#include "rivotek_config.h"

#if RVT_ENABLE_LOG
#define RVT_LOGI(tag, fmt, ...) printf("%s: " fmt "\n", tag, ##__VA_ARGS__)
#define RVT_LOGE(tag, fmt, ...) printf("%s: " fmt "\n", tag, ##__VA_ARGS__)
#else
#define RVT_LOGI(tag, fmt, ...)
#define RVT_LOGE(tag, fmt, ...)
#endif

#endif /* __RIVOTEK_LOG_H__ */
