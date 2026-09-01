#ifndef __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_LOG_H
#define __VENDOR_ALLWINNERTECH_RIVOTEK_APPS_ECA_CLIENT_LOG_H

#include <nuttx/config.h>
#include <syslog.h>

#ifndef ECA_CLIENT_LOG_TAG
#define ECA_CLIENT_LOG_TAG "EcaClient"
#endif

#define ECA_LOGE(fmt, ...) \
    syslog(LOG_ERR, ECA_CLIENT_LOG_TAG ": " fmt "\n", ##__VA_ARGS__)
#define ECA_LOGW(fmt, ...) \
    syslog(LOG_WARNING, ECA_CLIENT_LOG_TAG ": " fmt "\n", ##__VA_ARGS__)

#ifdef CONFIG_ECA_CLIENT_VERBOSE_LOG
#define ECA_LOGI(fmt, ...) \
    syslog(LOG_INFO, ECA_CLIENT_LOG_TAG ": " fmt "\n", ##__VA_ARGS__)
#define ECA_LOGD(fmt, ...) \
    syslog(LOG_DEBUG, ECA_CLIENT_LOG_TAG ": " fmt "\n", ##__VA_ARGS__)
#else
#define ECA_LOGI(fmt, ...) do { } while (0)
#define ECA_LOGD(fmt, ...) do { } while (0)
#endif

#endif
