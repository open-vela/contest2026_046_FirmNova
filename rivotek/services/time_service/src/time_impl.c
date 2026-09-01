#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <nuttx/config.h>
#include "time_impl.h"

void ntpc_dualstack_family(int family);
int ntpc_start_with_list(const char *ntp_server_list);
int netlib_get_ipv4addr(const char *ifname, struct in_addr *addr);
int netlib_get_dripv4addr(const char *ifname, struct in_addr *addr);
int netlib_get_ipv4netmask(const char *ifname, struct in_addr *addr);
uint32_t lv_tick_get(void);

struct rivotek_ntpc_status_s {
    unsigned int nsamples;
    struct {
        int64_t offset;
        int64_t delay;
        const struct sockaddr *srv_addr;
        struct sockaddr_storage srv_addr_store;
    } samples[CONFIG_NETUTILS_NTPCLIENT_NUM_SAMPLES];
};

int ntpc_status(struct rivotek_ntpc_status_s *statusp);

#define SCOOTERDEMO_TIME_RETRY_INTERVAL_MS 10000U
#define SCOOTERDEMO_TIME_STATUS_CHECK_MS 2000U
#define SCOOTERDEMO_TIME_NO_SAMPLE_DIAG_MS 60000U
#define SCOOTERDEMO_TIME_HTTP_INITIAL_DELAY_MS 8000U
#define SCOOTERDEMO_TIME_HTTP_INTERVAL_MS 20000U
#define SCOOTERDEMO_TIME_HTTP_TIMEOUT_SEC 2

#define SCOOTERDEMO_TIME_DIAG_RESOLV_CONF "/tmp/resolv.conf"
#define SCOOTERDEMO_TIME_TEXT_PLACEHOLDER "--:--"

static bool g_ntp_started;
static uint32_t g_last_ntp_retry_tick;
static uint32_t g_last_ntp_start_tick;
static uint32_t g_last_ntp_status_tick;
static uint32_t g_last_ntp_diag_tick;
static uint32_t g_last_time_update_tick;
static uint32_t g_last_http_time_attempt_tick;
static bool g_timezone_ready;
static bool g_http_time_sync_inflight;
static bool g_time_ready;
static char g_time_text[6] = SCOOTERDEMO_TIME_TEXT_PLACEHOLDER;

static const char *g_ntp_server_list =
    "ntp.aliyun.com;ntp.tencent.com;cn.pool.ntp.org;pool.ntp.org;"
    "216.239.35.0;216.239.35.4;216.239.35.8;216.239.35.12";
static const char *g_default_tz = "CST-8";
static const char *g_http_time_hosts[] = {
    "www.baidu.com",
    "www.qq.com",
    "www.aliyun.com"
};

static void update_display_text(const char *text)
{
    snprintf(g_time_text, sizeof(g_time_text), "%s", text);
}

static void ensure_timezone_ready(void)
{
    if (g_timezone_ready) {
        return;
    }

    if (getenv("TZ") == NULL) {
        setenv("TZ", g_default_tz, 0);
    }

    tzset();
    g_timezone_ready = true;
}

static void ensure_ntp_started(void)
{
    int pid;

    if (g_ntp_started) {
        return;
    }

    ntpc_dualstack_family(AF_INET);
    pid = ntpc_start_with_list(g_ntp_server_list);
    if (pid >= 0) {
        printf("NTP daemon started pid=%d\n", pid);
        g_ntp_started = true;
        g_last_ntp_start_tick = lv_tick_get();
    } else {
        g_ntp_started = false;
        printf("Failed to start NTP daemon: %d\n", pid);
    }
}

static void print_ipv4_info(const char *ifname)
{
    struct in_addr ip_addr;
    struct in_addr mask_addr;
    struct in_addr gateway_addr;
    char ip_str[INET_ADDRSTRLEN] = {0};
    char mask_str[INET_ADDRSTRLEN] = {0};
    char gateway_str[INET_ADDRSTRLEN] = {0};
    bool has_info = false;

    if (netlib_get_ipv4addr(ifname, &ip_addr) == 0) {
        inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str));
        has_info = true;
    }

    if (netlib_get_ipv4netmask(ifname, &mask_addr) == 0) {
        inet_ntop(AF_INET, &mask_addr, mask_str, sizeof(mask_str));
        has_info = true;
    }

    if (netlib_get_dripv4addr(ifname, &gateway_addr) == 0) {
        inet_ntop(AF_INET, &gateway_addr, gateway_str, sizeof(gateway_str));
        has_info = true;
    }

    if (has_info) {
        printf("NTP diag if=%s ip=%s mask=%s gw=%s\n",
               ifname,
               ip_str[0] != '\0' ? ip_str : "<none>",
               mask_str[0] != '\0' ? mask_str : "<none>",
               gateway_str[0] != '\0' ? gateway_str : "<none>");
    }
}

static void print_resolv_conf(void)
{
    int fd;
    ssize_t nread;
    char buf[128];

    fd = open(SCOOTERDEMO_TIME_DIAG_RESOLV_CONF, O_RDONLY);
    if (fd < 0) {
        printf("NTP diag %s unavailable errno=%d\n",
               SCOOTERDEMO_TIME_DIAG_RESOLV_CONF,
               errno);
        return;
    }

    printf("NTP diag %s begin\n", SCOOTERDEMO_TIME_DIAG_RESOLV_CONF);
    while ((nread = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[nread] = '\0';
        printf("%s", buf);
    }
    if (nread < 0) {
        printf("NTP diag read %s failed errno=%d\n",
               SCOOTERDEMO_TIME_DIAG_RESOLV_CONF,
               errno);
    }
    printf("\nNTP diag %s end\n", SCOOTERDEMO_TIME_DIAG_RESOLV_CONF);

    close(fd);
}

static void dump_ntp_diagnostics(void)
{
    static const char *ifnames[] = {"wlan0", "eth0", "ap0", "wl0", "en0"};
    time_t now;
    int index;

    time(&now);
    printf("NTP diag realtime=%ld tz=%s ntp_started=%d\n",
           (long)now,
           getenv("TZ") != NULL ? getenv("TZ") : "<unset>",
           g_ntp_started ? 1 : 0);

    for (index = 0; index < (int)(sizeof(ifnames) / sizeof(ifnames[0])); index++) {
        print_ipv4_info(ifnames[index]);
    }

    print_resolv_conf();
}

static bool is_leap_year(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int month_from_name(const char *month)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int index;

    for (index = 0; index < 12; index++) {
        if (strncmp(month, months[index], 3) == 0) {
            return index;
        }
    }

    return -1;
}

static bool parse_http_date_to_epoch(const char *line, time_t *utc_seconds)
{
    static const int days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    char weekday[4];
    char month_name[4];
    int day;
    int year;
    int hour;
    int minute;
    int second;
    int month;
    int days = 0;
    int current_year;

    if (sscanf(line, "Date: %3[^,], %d %3s %d %d:%d:%d GMT",
               weekday, &day, month_name, &year, &hour, &minute, &second) != 7) {
        return false;
    }

    month = month_from_name(month_name);
    if (month < 0 || year < 1970) {
        return false;
    }

    for (current_year = 1970; current_year < year; current_year++) {
        days += is_leap_year(current_year) ? 366 : 365;
    }

    days += days_before_month[month];
    if (month >= 2 && is_leap_year(year)) {
        days += 1;
    }

    days += day - 1;
    *utc_seconds = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

static int fetch_http_date_from_host(const char *host, time_t *utc_seconds)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *entry;
    struct timeval timeout;
    int socket_fd = -1;
    int ret;
    ssize_t sent;
    ssize_t received;
    char request[256];
    char response[1024];
    size_t total = 0;
    char *line;
    char *line_end;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ret = getaddrinfo(host, "80", &hints, &result);
    if (ret != 0) {
        printf("HTTP time getaddrinfo(%s) failed: %d\n", host, ret);
        return -1;
    }

    timeout.tv_sec = SCOOTERDEMO_TIME_HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    for (entry = result; entry != NULL; entry = entry->ai_next) {
        socket_fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }

        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (connect(socket_fd, entry->ai_addr, entry->ai_addrlen) == 0) {
            break;
        }

        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(result);

    if (socket_fd < 0) {
        printf("HTTP time connect(%s) failed errno=%d\n", host, errno);
        return -1;
    }

    snprintf(request, sizeof(request),
             "HEAD / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: yd-scootererdemo/1.0\r\n\r\n",
             host);

    sent = send(socket_fd, request, strlen(request), 0);
    if (sent < 0) {
        printf("HTTP time send(%s) failed errno=%d\n", host, errno);
        close(socket_fd);
        return -1;
    }

    while (total < sizeof(response) - 1) {
        received = recv(socket_fd, response + total, sizeof(response) - 1 - total, 0);
        if (received <= 0) {
            break;
        }

        total += received;
        response[total] = '\0';
        if (strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }

    close(socket_fd);

    if (total == 0) {
        printf("HTTP time recv(%s) got empty response\n", host);
        return -1;
    }

    line = response;
    while (line != NULL && *line != '\0') {
        line_end = strstr(line, "\r\n");
        if (line_end != NULL) {
            *line_end = '\0';
        }

        if (strncmp(line, "Date:", 5) == 0 && parse_http_date_to_epoch(line, utc_seconds)) {
            return 0;
        }

        if (line_end == NULL) {
            break;
        }

        line = line_end + 2;
    }

    printf("HTTP time response from %s missing Date header\n", host);
    return -1;
}

static void *http_time_sync_thread(void *arg)
{
    struct timespec current_time;
    time_t utc_seconds;
    int index;

    (void)arg;

    for (index = 0; index < (int)(sizeof(g_http_time_hosts) / sizeof(g_http_time_hosts[0])); index++) {
        if (fetch_http_date_from_host(g_http_time_hosts[index], &utc_seconds) == 0) {
            current_time.tv_sec = utc_seconds;
            current_time.tv_nsec = 0;

            if (clock_settime(CLOCK_REALTIME, &current_time) == 0) {
                printf("HTTP time sync succeeded via %s, utc=%ld\n",
                       g_http_time_hosts[index],
                       (long)utc_seconds);
                break;
            }

            printf("HTTP time clock_settime failed errno=%d\n", errno);
        }
    }

    g_http_time_sync_inflight = false;
    return NULL;
}

static void maybe_start_http_time_sync(uint32_t tick)
{
    pthread_attr_t attr;
    pthread_t thread_id;
    int ret;

    if (g_http_time_sync_inflight ||
        tick - g_last_http_time_attempt_tick < SCOOTERDEMO_TIME_HTTP_INTERVAL_MS) {
        return;
    }

    g_last_http_time_attempt_tick = tick;
    g_http_time_sync_inflight = true;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&thread_id, &attr, http_time_sync_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        g_http_time_sync_inflight = false;
        printf("HTTP time pthread_create failed: %d\n", ret);
        return;
    }

    printf("HTTP time sync started\n");
}

static void refresh_ntp_state(uint32_t tick)
{
    struct rivotek_ntpc_status_s status;

    if (!g_ntp_started) {
        return;
    }

    if (ntpc_status(&status) < 0) {
        printf("Failed to query NTP status, will retry start\n");
        g_ntp_started = false;
        return;
    }

    if (status.nsamples == 0 &&
        tick - g_last_ntp_start_tick >= SCOOTERDEMO_TIME_HTTP_INITIAL_DELAY_MS) {
        maybe_start_http_time_sync(tick);
    }

    if (status.nsamples == 0 &&
        tick - g_last_ntp_diag_tick >= SCOOTERDEMO_TIME_NO_SAMPLE_DIAG_MS) {
        g_last_ntp_diag_tick = tick;
        dump_ntp_diagnostics();
        printf("NTP still has no samples after %u ms, keep waiting for client retries\n",
               (unsigned int)(tick - g_last_ntp_start_tick));
    }
}

void rivotek_time_component_init(void)
{
    g_ntp_started = false;
    g_last_ntp_retry_tick = 0;
    g_last_ntp_start_tick = 0;
    g_last_ntp_status_tick = 0;
    g_last_ntp_diag_tick = 0;
    g_last_time_update_tick = 0;
    g_last_http_time_attempt_tick = 0;
    g_timezone_ready = false;
    g_http_time_sync_inflight = false;
    g_time_ready = false;
    update_display_text(SCOOTERDEMO_TIME_TEXT_PLACEHOLDER);

    ensure_timezone_ready();
    ensure_ntp_started();
}

void rivotek_time_component_update(uint32_t tick)
{
    time_t now;
    struct tm local_time;
    char time_str[6];

    if (g_last_time_update_tick != 0 && tick - g_last_time_update_tick < 1000U) {
        return;
    }

    g_last_time_update_tick = tick;
    time(&now);
    if (now < 1000000000) {
        if (!g_ntp_started && tick - g_last_ntp_retry_tick >= SCOOTERDEMO_TIME_RETRY_INTERVAL_MS) {
            g_last_ntp_retry_tick = tick;
            ensure_ntp_started();
        }

        if (g_ntp_started && tick - g_last_ntp_status_tick >= SCOOTERDEMO_TIME_STATUS_CHECK_MS) {
            g_last_ntp_status_tick = tick;
            refresh_ntp_state(tick);
        }

        g_time_ready = false;
        update_display_text(SCOOTERDEMO_TIME_TEXT_PLACEHOLDER);
        return;
    }

    ensure_timezone_ready();
    if (localtime_r(&now, &local_time) != NULL) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d", local_time.tm_hour, local_time.tm_min);
        g_time_ready = true;
        update_display_text(time_str);
        return;
    }

    g_time_ready = false;
    update_display_text(SCOOTERDEMO_TIME_TEXT_PLACEHOLDER);
}

const char *rivotek_time_component_get_text(void)
{
    return g_time_text;
}

bool rivotek_time_component_is_ready(void)
{
    return g_time_ready;
}
