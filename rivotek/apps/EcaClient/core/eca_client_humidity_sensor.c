#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <sensor/humi.h>
#include <sensor/temp.h>

#include "eca_client_device_state.h"
#include "eca_client_humidity_sensor.h"
#include "eca_client_log.h"

#define ECA_CLIENT_TEMPERATURE_DEV "/dev/uorb/sensor_ambient_temp0"
#define ECA_CLIENT_HUMIDITY_DEV    "/dev/uorb/sensor_humi0"

static int g_humidity_fd = -1;
static int g_temperature_fd = -1;

static void eca_client_humidity_sensor_close_fd(FAR int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

int eca_client_humidity_sensor_init(eca_client_model_t *model)
{
    eca_client_model_clear_humidity(model);
    eca_client_model_clear_temperature(model);

    g_temperature_fd = open(ECA_CLIENT_TEMPERATURE_DEV, O_RDONLY | O_NONBLOCK);
    if (g_temperature_fd < 0) {
        ECA_LOGW("open %s failed: %d", ECA_CLIENT_TEMPERATURE_DEV, errno);
    }

    g_humidity_fd = open(ECA_CLIENT_HUMIDITY_DEV, O_RDONLY | O_NONBLOCK);
    if (g_humidity_fd < 0) {
        ECA_LOGW("open %s failed: %d", ECA_CLIENT_HUMIDITY_DEV, errno);
    }

    if (g_humidity_fd < 0 && g_temperature_fd < 0) {
        return -ENODEV;
    }

    return 0;
}

int eca_client_humidity_sensor_update(eca_client_model_t *model, int timeout_ms)
{
    struct pollfd fds[2];
    nfds_t nfds = 0;
    int update_count = 0;
    int ret;

    if (g_humidity_fd < 0 && g_temperature_fd < 0) {
        return -ENODEV;
    }

    if (g_humidity_fd >= 0) {
        fds[nfds].fd = g_humidity_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    if (g_temperature_fd >= 0) {
        fds[nfds].fd = g_temperature_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    ret = poll(fds, nfds, timeout_ms);
    if (ret <= 0) {
        return ret;
    }

    for (nfds_t i = 0; i < nfds; i++) {
        if ((fds[i].revents & POLLIN) == 0) {
            continue;
        }

        if (fds[i].fd == g_humidity_fd) {
            struct sensor_humi data;

            ret = read(g_humidity_fd, &data, sizeof(data));
            if (ret == sizeof(data)) {
                eca_client_model_set_humidity(model, data.humidity);
                eca_client_device_state_set_humidity(data.humidity);
                update_count++;
            }
        } else if (fds[i].fd == g_temperature_fd) {
            struct sensor_temp data;

            ret = read(g_temperature_fd, &data, sizeof(data));
            if (ret == sizeof(data)) {
                eca_client_model_set_temperature(model, data.temperature);
                eca_client_device_state_set_temperature(data.temperature);
                update_count++;
            }
        }
    }

    return update_count;
}

void eca_client_humidity_sensor_deinit(void)
{
    eca_client_humidity_sensor_close_fd(&g_humidity_fd);
    eca_client_humidity_sensor_close_fd(&g_temperature_fd);
}
