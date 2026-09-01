#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#define WEATHER_DEFAULT_PORT "443"
#define WEATHER_HOST_LEN 64
#define WEATHER_PORT_LEN 8
#define WEATHER_PATH_LEN 512
#define WEATHER_REQUEST_LEN 768

struct weather_url_s
{
  char host[WEATHER_HOST_LEN];
  char port[WEATHER_PORT_LEN];
  char path[WEATHER_PATH_LEN];
};

static int weather_log_tls_error(FAR const char *tag, int ret)
{
  char errbuf[128];

  mbedtls_strerror(ret, errbuf, sizeof(errbuf));
  nerr("%s failed: %s (%d)\n", tag, errbuf, ret);
  return -EIO;
}

static int weather_parse_url(FAR const char *url, FAR struct weather_url_s *out)
{
  FAR const char *host_begin;
  FAR const char *host_end;
  FAR const char *path_begin;
  FAR const char *port_sep;
  size_t host_len;
  size_t port_len;
  size_t path_len;

  if (url == NULL || out == NULL)
    {
      return -EINVAL;
    }

  if (strncmp(url, "https://", 8) != 0)
    {
      return -ENOTSUP;
    }

  memset(out, 0, sizeof(*out));

  host_begin = url + 8;
  path_begin = strchr(host_begin, '/');
  host_end = path_begin != NULL ? path_begin : url + strlen(url);
  port_sep = memchr(host_begin, ':', host_end - host_begin);

  if (port_sep != NULL)
    {
      host_len = (size_t)(port_sep - host_begin);
      port_len = (size_t)(host_end - port_sep - 1);
      if (port_len == 0 || port_len >= sizeof(out->port))
        {
          return -EINVAL;
        }

      memcpy(out->port, port_sep + 1, port_len);
      out->port[port_len] = '\0';
    }
  else
    {
      host_len = (size_t)(host_end - host_begin);
      snprintf(out->port, sizeof(out->port), "%s", WEATHER_DEFAULT_PORT);
    }

  if (host_len == 0 || host_len >= sizeof(out->host))
    {
      return -EINVAL;
    }

  memcpy(out->host, host_begin, host_len);
  out->host[host_len] = '\0';

  if (path_begin == NULL)
    {
      snprintf(out->path, sizeof(out->path), "/");
      return 0;
    }

  path_len = strlen(path_begin);
  if (path_len >= sizeof(out->path))
    {
      return -ENAMETOOLONG;
    }

  memcpy(out->path, path_begin, path_len);
  out->path[path_len] = '\0';
  return 0;
}

static int weather_ssl_write_all(FAR mbedtls_ssl_context *ssl,
                                 FAR const unsigned char *buffer,
                                 size_t len)
{
  size_t offset = 0;

  while (offset < len)
    {
      int ret = mbedtls_ssl_write(ssl, buffer + offset, len - offset);
      if (ret > 0)
        {
          offset += (size_t)ret;
          continue;
        }

      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      return weather_log_tls_error("mbedtls_ssl_write", ret);
    }

  return 0;
}

static int weather_ssl_read_all(FAR mbedtls_ssl_context *ssl,
                                FAR char *buffer,
                                size_t buflen)
{
  size_t total = 0;

  if (buffer == NULL || buflen < 2)
    {
      return -EINVAL;
    }

  for (; ; )
    {
      int ret;

      if (total + 1 >= buflen)
        {
          return -EOVERFLOW;
        }

      ret = mbedtls_ssl_read(ssl, (unsigned char *)buffer + total,
                             buflen - total - 1);
      if (ret > 0)
        {
          total += (size_t)ret;
          continue;
        }

      if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
          break;
        }

      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      return weather_log_tls_error("mbedtls_ssl_read", ret);
    }

  buffer[total] = '\0';
  return 0;
}

static FAR char *weather_find_body(FAR char *response, FAR int *status_code)
{
  FAR char *line_end;
  FAR char *body;

  if (response == NULL)
    {
      return NULL;
    }

  if (status_code != NULL)
    {
      *status_code = -1;
    }

  line_end = strstr(response, "\r\n");
  if (line_end != NULL && status_code != NULL)
    {
      FAR char *status = strchr(response, ' ');
      if (status != NULL && status < line_end)
        {
          *status_code = atoi(status + 1);
        }
    }

  body = strstr(response, "\r\n\r\n");
  if (body == NULL)
    {
      return NULL;
    }

  return body + 4;
}

int weather_http_fetch_body(FAR const char *url, FAR char *buffer,
                            size_t buflen)
{
  struct weather_url_s parsed;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_net_context net_ctx;
  mbedtls_ssl_context ssl_ctx;
  mbedtls_ssl_config ssl_conf;
  FAR char *body;
  char request[WEATHER_REQUEST_LEN];
  static const char personalization[] = "r528-weather";
  int status_code = -1;
  int ret;

  if (url == NULL || buffer == NULL)
    {
      return -EINVAL;
    }

  ret = weather_parse_url(url, &parsed);
  if (ret < 0)
    {
      return ret;
    }

  ret = snprintf(request, sizeof(request),
                 "GET %s HTTP/1.0\r\n"
                 "Host: %s\r\n"
                 "User-Agent: r528-weather/1.0\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 parsed.path, parsed.host);
  if (ret < 0 || ret >= (int)sizeof(request))
    {
      return -E2BIG;
    }

  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);
  mbedtls_net_init(&net_ctx);
  mbedtls_ssl_init(&ssl_ctx);
  mbedtls_ssl_config_init(&ssl_conf);

  ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (FAR const unsigned char *)personalization,
                              sizeof(personalization) - 1);
  if (ret != 0)
    {
      ret = weather_log_tls_error("mbedtls_ctr_drbg_seed", ret);
      goto out;
    }

  ret = mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      ret = weather_log_tls_error("mbedtls_ssl_config_defaults", ret);
      goto out;
    }

  mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);

  ret = mbedtls_net_connect(&net_ctx, parsed.host, parsed.port,
                            MBEDTLS_NET_PROTO_TCP);
  if (ret != 0)
    {
      ret = weather_log_tls_error("mbedtls_net_connect", ret);
      goto out;
    }

  ret = mbedtls_ssl_setup(&ssl_ctx, &ssl_conf);
  if (ret != 0)
    {
      ret = weather_log_tls_error("mbedtls_ssl_setup", ret);
      goto out;
    }

  ret = mbedtls_ssl_set_hostname(&ssl_ctx, parsed.host);
  if (ret != 0)
    {
      ret = weather_log_tls_error("mbedtls_ssl_set_hostname", ret);
      goto out;
    }

  mbedtls_ssl_set_bio(&ssl_ctx, &net_ctx, mbedtls_net_send,
                      mbedtls_net_recv, NULL);

  for (; ; )
    {
      ret = mbedtls_ssl_handshake(&ssl_ctx);
      if (ret == 0)
        {
          break;
        }

      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }

      ret = weather_log_tls_error("mbedtls_ssl_handshake", ret);
      goto out;
    }

  ret = weather_ssl_write_all(&ssl_ctx, (FAR const unsigned char *)request,
                              strlen(request));
  if (ret < 0)
    {
      goto out;
    }

  ret = weather_ssl_read_all(&ssl_ctx, buffer, buflen);
  if (ret < 0)
    {
      goto out;
    }

  body = weather_find_body(buffer, &status_code);
  if (body == NULL)
    {
      ret = -EBADMSG;
      goto out;
    }

  if (status_code != 200)
    {
      nerr("weather http status: %d\n", status_code);
      ret = -EHOSTUNREACH;
      goto out;
    }

  memmove(buffer, body, strlen(body) + 1);
  ret = 0;

out:
  mbedtls_ssl_close_notify(&ssl_ctx);
  mbedtls_ssl_free(&ssl_ctx);
  mbedtls_net_free(&net_ctx);
  mbedtls_ssl_config_free(&ssl_conf);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
  return ret;
}
