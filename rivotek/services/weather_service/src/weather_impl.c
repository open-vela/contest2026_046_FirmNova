#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cjson/cJSON.h"
#endif

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "weather_api_config.h"
#include "weather_impl.h"

#define SCOOTERDEMO_WEATHER_NET_WAIT_POLL_SEC 1
#define SCOOTERDEMO_WEATHER_START_DELAY_SEC 5
#define SCOOTERDEMO_WEATHER_FETCH_RETRY_SEC 5
#define SCOOTERDEMO_WEATHER_REFRESH_INTERVAL_SEC (3 * 60 * 60)
#define SCOOTERDEMO_WEATHER_HTTP_TIMEOUT_SEC 5
#define SCOOTERDEMO_WEATHER_DESC_LEN 96
#define SCOOTERDEMO_WEATHER_TEMP_TEXT_LEN 16
#define SCOOTERDEMO_WEATHER_CITY_LEN 48
#define SCOOTERDEMO_WEATHER_STATUS_LEN 48
#define SCOOTERDEMO_WEATHER_AIR_TEXT_LEN 24
#define SCOOTERDEMO_WEATHER_AIR_CATEGORY_LEN 16
#define SCOOTERDEMO_WEATHER_HOST_LEN 128
#define SCOOTERDEMO_WEATHER_PORT_LEN 8
#define SCOOTERDEMO_WEATHER_PATH_LEN 256
#define SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN 4096
#define SCOOTERDEMO_WEATHER_THREAD_STACK_SIZE 32768
#define SCOOTERDEMO_WEATHER_CA_CERT_PATH "/etc/ssl/certs/ca-certificates.crt"
#define WEATHER_LOG_ERROR(fmt, ...) printf("weather_impl: " fmt "\n", ##__VA_ARGS__)
#define WEATHER_LOG_INFO(fmt, ...) do { } while (0)

struct weather_api_result_s {
    char city[SCOOTERDEMO_WEATHER_CITY_LEN];
    char weather[SCOOTERDEMO_WEATHER_STATUS_LEN];
    char air_category[SCOOTERDEMO_WEATHER_AIR_CATEGORY_LEN];
    double temperature_c;
    double aqi;
    double latitude;
    double longitude;
    bool has_temperature;
    bool has_aqi;
    bool has_latlon;
};

static bool g_weather_ready;
static bool g_weather_thread_started;
static bool g_weather_lock_ready;
static char g_weather_city[SCOOTERDEMO_WEATHER_CITY_LEN] = "定位中";
static char g_weather_desc[SCOOTERDEMO_WEATHER_DESC_LEN] = "天气获取中";
static char g_weather_temp_text[SCOOTERDEMO_WEATHER_TEMP_TEXT_LEN] = "--°C";
static char g_weather_air_text[SCOOTERDEMO_WEATHER_AIR_TEXT_LEN] = "空气--";
static pthread_t g_weather_thread;
static pthread_mutex_t g_weather_lock;

static void weather_lock(void)
{
    if (g_weather_lock_ready) {
        pthread_mutex_lock(&g_weather_lock);
    }
}

static void weather_unlock(void)
{
    if (g_weather_lock_ready) {
        pthread_mutex_unlock(&g_weather_lock);
    }
}

static int weather_temp_to_int(double temperature_c)
{
    if (temperature_c >= 0) {
        return (int)(temperature_c + 0.5);
    }

    return (int)(temperature_c - 0.5);
}

static const char *weather_aqi_to_category(double aqi)
{
    if (aqi <= 50) {
        return "优";
    }

    if (aqi <= 100) {
        return "良";
    }

    if (aqi <= 150) {
        return "轻度污染";
    }

    if (aqi <= 200) {
        return "中度污染";
    }

    if (aqi <= 300) {
        return "重度污染";
    }

    return "严重污染";
}

static bool weather_network_is_ready(void)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *entry;
    bool ready = false;

    if (getifaddrs(&ifaddr) != 0) {
        return false;
    }

    for (entry = ifaddr; entry != NULL; entry = entry->ifa_next) {
        struct sockaddr_in *addr;

        if (entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if ((entry->ifa_flags & IFF_UP) == 0 ||
            (entry->ifa_flags & IFF_RUNNING) == 0 ||
            (entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        addr = (struct sockaddr_in *)entry->ifa_addr;
        if (addr->sin_addr.s_addr == 0) {
            continue;
        }

        ready = true;
        break;
    }

    freeifaddrs(ifaddr);
    return ready;
}

static bool str_has_text(const char *text)
{
    return text != NULL && text[0] != '\0';
}

static bool parse_weather_url(const char *url, bool *use_tls,
                              char *host, size_t host_len,
                              char *port, size_t port_len,
                              char *path, size_t path_len)
{
    const char *scheme_end;
    const char *host_begin;
    const char *path_begin;
    const char *port_begin;
    size_t host_size;
    size_t port_size;

    if (url == NULL || use_tls == NULL || host == NULL ||
        port == NULL || path == NULL) {
        return false;
    }

    scheme_end = strstr(url, "://");
    if (scheme_end == NULL) {
        return false;
    }

    if (strncmp(url, "https", 5) == 0) {
        *use_tls = true;
    } else if (strncmp(url, "http", 4) == 0) {
        *use_tls = false;
    } else {
        return false;
    }

    host_begin = scheme_end + 3;
    path_begin = strchr(host_begin, '/');
    if (path_begin == NULL) {
        path_begin = host_begin + strlen(host_begin);
        snprintf(path, path_len, "%s", "/");
    } else {
        snprintf(path, path_len, "%s", path_begin);
    }

    port_begin = strchr(host_begin, ':');
    if (port_begin != NULL && port_begin < path_begin) {
        host_size = (size_t)(port_begin - host_begin);
        port_begin += 1;
        port_size = (size_t)(path_begin - port_begin);
        if (port_size == 0 || port_size >= port_len) {
            return false;
        }
        snprintf(port, port_len, "%.*s", (int)port_size, port_begin);
    } else {
        host_size = (size_t)(path_begin - host_begin);
        snprintf(port, port_len, "%s", *use_tls ? "443" : "80");
    }

    if (host_size == 0 || host_size >= host_len) {
        return false;
    }

    snprintf(host, host_len, "%.*s", (int)host_size, host_begin);
    return true;
}

static void format_mbedtls_error(int ret, char *buffer, size_t buffer_len)
{
    if (buffer == NULL || buffer_len == 0) {
        return;
    }

    mbedtls_strerror(ret, buffer, buffer_len);
}

static int build_weather_http_request(const char *host, const char *path,
                                      char *request, size_t request_len)
{
    int written;

    if (host == NULL || path == NULL || request == NULL || request_len == 0) {
        return -1;
    }

    written = snprintf(request, request_len,
                       "GET %s HTTP/1.0\r\n"
                       "Host: %s\r\n"
                       "Accept: application/json\r\n"
                       SCOOTERDEMO_WEATHER_AUTH_HEADER
                       "Connection: close\r\n"
                       "User-Agent: yd-scooterdemo-weather/1.0\r\n"
                       "\r\n",
                       path, host);
    if (written < 0 || (size_t)written >= request_len) {
        WEATHER_LOG_ERROR("weather HTTP request buffer too small");
        return -1;
    }

    return 0;
}

static int connect_http_server(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *entry;
    struct timeval timeout;
    int socket_fd = -1;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ret = getaddrinfo(host, port, &hints, &result);
    if (ret != 0) {
        WEATHER_LOG_ERROR("getaddrinfo(%s:%s) failed: %d", host, port, ret);
        return -1;
    }

    timeout.tv_sec = SCOOTERDEMO_WEATHER_HTTP_TIMEOUT_SEC;
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
    return socket_fd;
}

static int https_get_weather_body(const char *host, const char *port,
                                  const char *path, char *body, size_t body_len)
{
    mbedtls_net_context server_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    char request[512];
    char *response = NULL;
    char errbuf[128];
    char *header_end;
    const unsigned char *pers =
        (const unsigned char *)"yd-scooterdemo-weather";
    size_t total = 0;
    int ca_ret;
    int ret;
    int status_code = 0;
    int verify_result;
    struct timeval timeout;

    mbedtls_net_init(&server_fd);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    response = (char *)malloc(SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN);
    if (response == NULL) {
        WEATHER_LOG_ERROR("alloc HTTPS response buffer failed");
        ret = -1;
        goto cleanup;
    }

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                pers, strlen((const char *)pers));
    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ctr_drbg_seed failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    ca_ret = mbedtls_x509_crt_parse_file(&cacert, SCOOTERDEMO_WEATHER_CA_CERT_PATH);
    if (ca_ret < 0) {
        format_mbedtls_error(ca_ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("load CA cert failed: %s (%d), continue without verify",
                          errbuf, ca_ret);
    }

    ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ssl_config_defaults failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    if (ca_ret == 0) {
        mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ssl_setup failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    ret = mbedtls_ssl_set_hostname(&ssl, host);
    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ssl_set_hostname failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    ret = mbedtls_net_connect(&server_fd, host, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_net_connect(%s:%s) failed: %s (%d)",
                          host, port, errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    timeout.tv_sec = SCOOTERDEMO_WEATHER_HTTP_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(server_fd.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(server_fd.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    do {
        ret = mbedtls_ssl_handshake(&ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (ret != 0) {
        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ssl_handshake failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    verify_result = (int)mbedtls_ssl_get_verify_result(&ssl);
    if (verify_result != 0 && verify_result != -1) {
        char verify_info[256];

        mbedtls_x509_crt_verify_info(verify_info, sizeof(verify_info), "",
                                     (uint32_t)verify_result);
        WEATHER_LOG_INFO("TLS verify warning: %s", verify_info);
    }

    if (build_weather_http_request(host, path, request, sizeof(request)) != 0) {
        ret = -1;
        goto cleanup;
    }

    {
        size_t request_len = strlen(request);
        size_t sent_total = 0;

        while (sent_total < request_len) {
            ret = mbedtls_ssl_write(&ssl,
                                    (const unsigned char *)request + sent_total,
                                    request_len - sent_total);
            if (ret > 0) {
                sent_total += (size_t)ret;
                continue;
            }

            if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }

            format_mbedtls_error(ret, errbuf, sizeof(errbuf));
            WEATHER_LOG_ERROR("mbedtls_ssl_write failed: %s (%d)", errbuf, ret);
            ret = -1;
            goto cleanup;
        }
    }

    while (total < SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN - 1) {
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)response + total,
                               SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN - 1 - total);
        if (ret > 0) {
            total += (size_t)ret;
            continue;
        }

        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            break;
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        format_mbedtls_error(ret, errbuf, sizeof(errbuf));
        WEATHER_LOG_ERROR("mbedtls_ssl_read failed: %s (%d)", errbuf, ret);
        ret = -1;
        goto cleanup;
    }

    if (total == 0) {
        WEATHER_LOG_ERROR("empty HTTPS weather response");
        ret = -1;
        goto cleanup;
    }

    response[total] = '\0';
    if (sscanf(response, "HTTP/%*d.%*d %d", &status_code) != 1 ||
        status_code != 200) {
        WEATHER_LOG_ERROR("unexpected HTTPS status %d", status_code);
        ret = -1;
        goto cleanup;
    }

    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        WEATHER_LOG_ERROR("malformed HTTPS response");
        ret = -1;
        goto cleanup;
    }

    header_end += 4;
    if (strlen(header_end) >= body_len) {
        WEATHER_LOG_ERROR("HTTPS weather response body too large");
        ret = -1;
        goto cleanup;
    }

    snprintf(body, body_len, "%s", header_end);
    ret = 0;

cleanup:
    free(response);
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&server_fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

static int http_get_weather_body(const char *url, char *body, size_t body_len)
{
    bool use_tls;
    char host[SCOOTERDEMO_WEATHER_HOST_LEN];
    char port[SCOOTERDEMO_WEATHER_PORT_LEN];
    char path[SCOOTERDEMO_WEATHER_PATH_LEN];
    char request[512];
    char *response = NULL;
    char *header_end;
    size_t total = 0;
    ssize_t sent;
    ssize_t received;
    int socket_fd;
    int status_code = 0;

    if (!parse_weather_url(url, &use_tls, host, sizeof(host), port, sizeof(port),
                           path, sizeof(path))) {
        WEATHER_LOG_ERROR("invalid weather API URL: %s", url);
        return -1;
    }

    if (use_tls) {
        return https_get_weather_body(host, port, path, body, body_len);
    }

    response = (char *)malloc(SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN);
    if (response == NULL) {
        WEATHER_LOG_ERROR("alloc HTTP response buffer failed");
        return -1;
    }

    socket_fd = connect_http_server(host, port);
    if (socket_fd < 0) {
        WEATHER_LOG_ERROR("connect(%s:%s) failed errno=%d", host, port, errno);
        free(response);
        return -1;
    }

    if (build_weather_http_request(host, path, request, sizeof(request)) != 0) {
        close(socket_fd);
        free(response);
        return -1;
    }

    sent = send(socket_fd, request, strlen(request), 0);
    if (sent < 0) {
        WEATHER_LOG_ERROR("send weather request failed errno=%d", errno);
        close(socket_fd);
        free(response);
        return -1;
    }

    while (total < SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN - 1) {
        received = recv(socket_fd, response + total,
                        SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN - 1 - total, 0);
        if (received <= 0) {
            break;
        }

        total += received;
    }

    close(socket_fd);

    if (total == 0) {
        WEATHER_LOG_ERROR("empty weather response");
        free(response);
        return -1;
    }

    response[total] = '\0';
    if (sscanf(response, "HTTP/%*d.%*d %d", &status_code) != 1 || status_code != 200) {
        WEATHER_LOG_ERROR("unexpected HTTP status %d", status_code);
        free(response);
        return -1;
    }

    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        WEATHER_LOG_ERROR("malformed HTTP response");
        free(response);
        return -1;
    }

    header_end += 4;
    if (strlen(header_end) >= body_len) {
        WEATHER_LOG_ERROR("weather response body too large");
        free(response);
        return -1;
    }

    snprintf(body, body_len, "%s", header_end);
    free(response);
    return 0;
}

static const cJSON *get_object_item_case_compat(const cJSON *object,
                                                const char *key1,
                                                const char *key2,
                                                const char *key3)
{
    const cJSON *item = NULL;

    if (object == NULL) {
        return NULL;
    }

    if (key1 != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, key1);
        if (item != NULL) {
            return item;
        }
    }

    if (key2 != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, key2);
        if (item != NULL) {
            return item;
        }
    }

    if (key3 != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, key3);
    }

    return item;
}

static bool json_item_to_double(const cJSON *item, double *value)
{
    if (item == NULL || value == NULL) {
        return false;
    }

    if (cJSON_IsNumber(item)) {
        *value = item->valuedouble;
        return true;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *end_ptr;
        double parsed;

        parsed = strtod(item->valuestring, &end_ptr);
        if (end_ptr != item->valuestring) {
            *value = parsed;
            return true;
        }
    }

    return false;
}

static bool json_item_to_text(const cJSON *item, char *buffer, size_t buffer_len)
{
    if (item == NULL || buffer == NULL || buffer_len == 0) {
        return false;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        snprintf(buffer, buffer_len, "%s", item->valuestring);
        return true;
    }

    if (cJSON_IsNumber(item)) {
        snprintf(buffer, buffer_len, "%.0f", item->valuedouble);
        return true;
    }

    return false;
}

static int parse_weather_response_json(const char *json_text,
                                       struct weather_api_result_s *result)
{
    cJSON *root;
    const cJSON *weather_object;
    const cJSON *city_item;
    const cJSON *weather_item;
    const cJSON *temp_item;
    const cJSON *air_item;
    const cJSON *aqi_item;
    const cJSON *lat_item;
    const cJSON *lon_item;
    const cJSON *location_item;

    if (json_text == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    root = cJSON_Parse(json_text);
    if (root == NULL) {
        WEATHER_LOG_ERROR("failed to parse weather JSON");
        return -1;
    }

    weather_object = root;
    location_item = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(location_item)) {
        weather_object = location_item;
    }

    city_item = get_object_item_case_compat(weather_object, "city", "city_name",
                                            "district");
    weather_item = get_object_item_case_compat(weather_object, "weather", "condition",
                                               "weather_desc");
    temp_item = get_object_item_case_compat(weather_object, "temperature", "temperature_c",
                                            "temp");
    air_item = get_object_item_case_compat(weather_object, "aqi_category", "air_quality",
                                           "quality");
    aqi_item = get_object_item_case_compat(weather_object, "aqi", "air_quality_index",
                                           NULL);
    lat_item = get_object_item_case_compat(weather_object, "latitude", "lat", NULL);
    lon_item = get_object_item_case_compat(weather_object, "longitude", "lon", NULL);

    location_item = cJSON_GetObjectItemCaseSensitive((cJSON *)weather_object, "location");
    if (city_item == NULL && cJSON_IsObject(location_item)) {
        city_item = get_object_item_case_compat(location_item, "city", "city_name",
                                                "district");
    }

    if (weather_item == NULL && cJSON_IsObject(location_item)) {
        weather_item = get_object_item_case_compat(location_item, "weather", "condition",
                                                   "weather_desc");
    }

    if (temp_item == NULL && cJSON_IsObject(location_item)) {
        temp_item = get_object_item_case_compat(location_item, "temperature",
                                                "temperature_c", "temp");
    }

    if (air_item == NULL && cJSON_IsObject(location_item)) {
        air_item = get_object_item_case_compat(location_item, "aqi_category",
                                               "air_quality", "quality");
    }

    if (aqi_item == NULL && cJSON_IsObject(location_item)) {
        aqi_item = get_object_item_case_compat(location_item, "aqi",
                                               "air_quality_index", NULL);
    }

    if (lat_item == NULL && cJSON_IsObject(location_item)) {
        lat_item = get_object_item_case_compat(location_item, "latitude", "lat", NULL);
    }

    if (lon_item == NULL && cJSON_IsObject(location_item)) {
        lon_item = get_object_item_case_compat(location_item, "longitude", "lon", NULL);
    }

    json_item_to_text(city_item, result->city, sizeof(result->city));
    json_item_to_text(weather_item, result->weather, sizeof(result->weather));
    json_item_to_text(air_item, result->air_category, sizeof(result->air_category));
    result->has_temperature = json_item_to_double(temp_item, &result->temperature_c);
    result->has_aqi = json_item_to_double(aqi_item, &result->aqi);
    if (result->has_aqi && result->aqi < 0) {
        result->has_aqi = false;
    }
    result->has_latlon = json_item_to_double(lat_item, &result->latitude) &&
                         json_item_to_double(lon_item, &result->longitude);

    cJSON_Delete(root);

    if (!result->has_temperature || !str_has_text(result->weather)) {
        WEATHER_LOG_ERROR("weather JSON missing required fields");
        return -1;
    }

    return 0;
}

static void update_weather_cache(const struct weather_api_result_s *info)
{
    int weather_temp_c;

    weather_lock();
    weather_temp_c = weather_temp_to_int(info->temperature_c);
    snprintf(g_weather_temp_text, sizeof(g_weather_temp_text), "%d°C", weather_temp_c);
    if (str_has_text(info->city)) {
        snprintf(g_weather_city, sizeof(g_weather_city), "%s", info->city);
    } else {
        snprintf(g_weather_city, sizeof(g_weather_city), "%s", "未知地区");
    }
    snprintf(g_weather_desc, sizeof(g_weather_desc), "%s", info->weather);
    if (str_has_text(info->air_category)) {
        if (strncmp(info->air_category, "空气", strlen("空气")) == 0) {
            snprintf(g_weather_air_text, sizeof(g_weather_air_text),
                     "%s", info->air_category);
        } else {
            snprintf(g_weather_air_text, sizeof(g_weather_air_text),
                     "空气%s", info->air_category);
        }
    } else if (info->has_aqi) {
        snprintf(g_weather_air_text, sizeof(g_weather_air_text),
                 "空气%s", weather_aqi_to_category(info->aqi));
    } else {
        snprintf(g_weather_air_text, sizeof(g_weather_air_text), "%s", "空气--");
    }
    g_weather_ready = true;
    weather_unlock();
}

static void set_weather_error_text(bool keep_last_success)
{
    weather_lock();
    if (!keep_last_success || !g_weather_ready) {
        snprintf(g_weather_city, sizeof(g_weather_city), "%s", "定位失败");
        snprintf(g_weather_temp_text, sizeof(g_weather_temp_text), "%s", "--°C");
        snprintf(g_weather_desc, sizeof(g_weather_desc), "%s", "天气获取失败");
        snprintf(g_weather_air_text, sizeof(g_weather_air_text), "%s", "空气--");
    }
    weather_unlock();
}

const char *rivotek_weather_component_get_city_text(void)
{
    static char city_text[SCOOTERDEMO_WEATHER_CITY_LEN];

    weather_lock();
    snprintf(city_text, sizeof(city_text), "%s", g_weather_city);
    weather_unlock();

    return city_text;
}

const char *rivotek_weather_component_get_temp_text(void)
{
    static char temp_text[SCOOTERDEMO_WEATHER_TEMP_TEXT_LEN];

    weather_lock();
    snprintf(temp_text, sizeof(temp_text), "%s", g_weather_temp_text);
    weather_unlock();

    return temp_text;
}

const char *rivotek_weather_component_get_desc_text(void)
{
    static char desc_text[SCOOTERDEMO_WEATHER_DESC_LEN];

    weather_lock();
    snprintf(desc_text, sizeof(desc_text), "%s", g_weather_desc);
    weather_unlock();

    return desc_text;
}

const char *rivotek_weather_component_get_air_quality_text(void)
{
    static char air_text[SCOOTERDEMO_WEATHER_AIR_TEXT_LEN];

    weather_lock();
    snprintf(air_text, sizeof(air_text), "%s", g_weather_air_text);
    weather_unlock();

    return air_text;
}

static void *weather_fetch_thread_entry(void *arg)
{
    struct weather_api_result_s info;
    char *response_body;

    (void)arg;
    response_body = (char *)malloc(SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN);
    if (response_body == NULL) {
        WEATHER_LOG_ERROR("alloc weather thread buffer failed");
        return NULL;
    }

    WEATHER_LOG_INFO("waiting for Wi-Fi/network ready");

    while (!weather_network_is_ready()) {
        sleep(SCOOTERDEMO_WEATHER_NET_WAIT_POLL_SEC);
    }

    WEATHER_LOG_INFO("Wi-Fi/network ready, start fetch after %d seconds",
                     SCOOTERDEMO_WEATHER_START_DELAY_SEC);
    sleep(SCOOTERDEMO_WEATHER_START_DELAY_SEC);

    for (;;) {
        int ret;

        while (!weather_network_is_ready()) {
            set_weather_error_text(true);
            sleep(SCOOTERDEMO_WEATHER_NET_WAIT_POLL_SEC);
        }

        ret = http_get_weather_body(SCOOTERDEMO_WEATHER_API_URL,
                                    response_body, SCOOTERDEMO_WEATHER_HTTP_RESPONSE_LEN);
        if (ret == 0) {
            ret = parse_weather_response_json(response_body, &info);
        }

        if (ret == 0) {
            update_weather_cache(&info);
            WEATHER_LOG_INFO("weather fetch success city=%s temp=%dC desc=%s",
                             str_has_text(info.city) ? info.city : "-",
                             weather_temp_to_int(info.temperature_c),
                             info.weather);
            sleep(SCOOTERDEMO_WEATHER_REFRESH_INTERVAL_SEC);
        } else {
            WEATHER_LOG_ERROR("weather fetch failed ret=%d, retry in %d seconds",
                              ret, SCOOTERDEMO_WEATHER_FETCH_RETRY_SEC);
            set_weather_error_text(false);
            sleep(SCOOTERDEMO_WEATHER_FETCH_RETRY_SEC);
        }
    }

    free(response_body);
    return NULL;
}

void rivotek_weather_component_init(void)
{
    if (!g_weather_lock_ready) {
        if (pthread_mutex_init(&g_weather_lock, NULL) != 0) {
            WEATHER_LOG_ERROR("pthread_mutex_init failed, continue without weather lock");
        } else {
            g_weather_lock_ready = true;
        }
    }

    weather_lock();
    g_weather_ready = false;
    snprintf(g_weather_city, sizeof(g_weather_city), "%s", "定位中");
    snprintf(g_weather_temp_text, sizeof(g_weather_temp_text), "%s", "--°C");
    snprintf(g_weather_desc, sizeof(g_weather_desc), "%s", "天气获取中");
    snprintf(g_weather_air_text, sizeof(g_weather_air_text), "%s", "空气--");
    weather_unlock();
    g_weather_thread_started = false;
}

int rivotek_weather_component_start(void)
{
    pthread_attr_t attr;
    int ret;

    weather_lock();
    if (g_weather_thread_started) {
        weather_unlock();
        return 0;
    }
    g_weather_thread_started = true;
    weather_unlock();

    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        weather_lock();
        g_weather_thread_started = false;
        weather_unlock();
        return -ret;
    }

    pthread_attr_setstacksize(&attr, SCOOTERDEMO_WEATHER_THREAD_STACK_SIZE);
    ret = pthread_create(&g_weather_thread, &attr, weather_fetch_thread_entry, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        weather_lock();
        g_weather_thread_started = false;
        weather_unlock();
        return -ret;
    }

    pthread_detach(g_weather_thread);
    return 0;
}
