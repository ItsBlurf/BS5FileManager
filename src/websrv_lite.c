/*
 * BS5FileManager - tiny single-purpose HTTP server.
 */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "asset.h"
#include "fs.h"
#include "transfer.h"
#include "version.h"
#include "websrv.h"


#define HEADER_MAX 65536


static int             g_websrv_srvfd = -1;
static pthread_mutex_t g_websrv_lock = PTHREAD_MUTEX_INITIALIZER;


static const char *
status_text(int status) {
  switch(status) {
  case 200: return "OK";
  case 201: return "Created";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 409: return "Conflict";
  case 500: return "Internal Server Error";
  default:  return "OK";
  }
}


int
websrv_write_all(int fd, const void *data, size_t size) {
  const char *p = data;
  while(size > 0) {
    ssize_t n = send(fd, p, size, 0);
    if(n < 0) {
      if(errno == EINTR) continue;
      return -1;
    }
    if(n == 0) return -1;
    p += n;
    size -= (size_t)n;
  }
  return 0;
}


int
websrv_send_headers(int fd, int status, const char *mime, size_t size,
                    const char *extra) {
  char header[1024];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Connection: close\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
                   "Pragma: no-cache\r\n"
                   "Expires: 0\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %lu\r\n"
                   "%s"
                   "\r\n",
                   status, status_text(status),
                   mime ? mime : "application/octet-stream",
                   (unsigned long)size,
                   extra ? extra : "");
  if(n < 0 || (size_t)n >= sizeof(header)) return -1;
  return websrv_write_all(fd, header, (size_t)n);
}


int
websrv_send(int fd, int status, const char *mime,
            const void *data, size_t size) {
  if(websrv_send_headers(fd, status, mime, size, NULL) != 0) return -1;
  if(size == 0) return 0;
  return websrv_write_all(fd, data, size);
}


int
websrv_send_text(int fd, int status, const char *mime, const char *body) {
  return websrv_send(fd, status, mime, body ? body : "",
                     body ? strlen(body) : 0);
}


static int
json_error_escape(char *out, size_t out_size, const char *message) {
  size_t pos = 0;
  const unsigned char *p = (const unsigned char *)(message ? message : "error");

  if(out_size == 0) return -1;
  for(; *p && pos + 7 < out_size; p++) {
    if(*p == '\\' || *p == '"') {
      out[pos++] = '\\';
      out[pos++] = (char)*p;
    } else if(*p == '\n') {
      out[pos++] = '\\';
      out[pos++] = 'n';
    } else if(*p == '\r') {
      out[pos++] = '\\';
      out[pos++] = 'r';
    } else if(*p == '\t') {
      out[pos++] = '\\';
      out[pos++] = 't';
    } else if(*p < 0x20) {
      int n = snprintf(out + pos, out_size - pos, "\\u%04x", *p);
      if(n < 0) return -1;
      pos += (size_t)n;
    } else {
      out[pos++] = (char)*p;
    }
  }
  out[pos] = 0;
  return 0;
}


int
websrv_send_error_json(int fd, int status, const char *message) {
  char escaped[512];
  char body[640];
  json_error_escape(escaped, sizeof(escaped), message);
  snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", escaped);
  return websrv_send_text(fd, status, "application/json", body);
}


static int
hex_value(int ch) {
  if(ch >= '0' && ch <= '9') return ch - '0';
  if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}


void
websrv_url_decode(char *out, size_t out_size, const char *in) {
  size_t pos = 0;

  if(out_size == 0) return;
  for(size_t i = 0; in && in[i] && pos + 1 < out_size; i++) {
    if(in[i] == '%' && isxdigit((unsigned char)in[i + 1]) &&
       isxdigit((unsigned char)in[i + 2])) {
      int hi = hex_value(in[i + 1]);
      int lo = hex_value(in[i + 2]);
      out[pos++] = (char)((hi << 4) | lo);
      i += 2;
    } else if(in[i] == '+') {
      out[pos++] = ' ';
    } else {
      out[pos++] = in[i];
    }
  }
  out[pos] = 0;
}


int
websrv_get_query_arg(const http_request_t *req, const char *name,
                     char *out, size_t out_size) {
  const char *p = req ? req->query : NULL;
  char key[128];
  char value[1024];

  if(out_size > 0) out[0] = 0;
  while(p && *p) {
    const char *amp = strchr(p, '&');
    size_t pair_len = amp ? (size_t)(amp - p) : strlen(p);
    const char *eq = memchr(p, '=', pair_len);
    size_t key_len = eq ? (size_t)(eq - p) : pair_len;
    size_t val_len = eq ? pair_len - key_len - 1 : 0;

    if(key_len >= sizeof(key)) key_len = sizeof(key) - 1;
    memcpy(key, p, key_len);
    key[key_len] = 0;
    websrv_url_decode(key, sizeof(key), key);

    if(!strcmp(key, name)) {
      if(val_len >= sizeof(value)) val_len = sizeof(value) - 1;
      if(eq) memcpy(value, eq + 1, val_len);
      value[val_len] = 0;
      websrv_url_decode(out, out_size, value);
      return 1;
    }

    if(!amp) break;
    p = amp + 1;
  }
  return 0;
}


static int
find_header_end(const char *buf, size_t size) {
  for(size_t i = 3; i < size; i++) {
    if(buf[i - 3] == '\r' && buf[i - 2] == '\n' &&
       buf[i - 1] == '\r' && buf[i] == '\n') {
      return (int)(i + 1);
    }
  }
  return -1;
}


static size_t
content_length_from_headers(const char *headers) {
  const char *line = strstr(headers, "\r\n");
  if(!line) return 0;
  line += 2;
  while(*line) {
    const char *next = strstr(line, "\r\n");
    size_t len = next ? (size_t)(next - line) : strlen(line);
    if(len == 0) break;
    if(len > 15 && !strncasecmp(line, "Content-Length:", 15)) {
      return (size_t)strtoull(line + 15, NULL, 10);
    }
    if(!next) break;
    line = next + 2;
  }
  return 0;
}


static int
status_request(const http_request_t *req) {
  char body[512];
  time_t now = time(NULL);
  int n = snprintf(body, sizeof(body),
                   "{\"ok\":true,"
                   "\"name\":\"BS5FileManager\","
                   "\"tag\":\"%s\","
                   "\"version\":\"%s\","
                   "\"pid\":%ld,"
                   "\"now\":%ld,"
                   "\"services\":[\"web\",\"file-manager\"],"
                   "\"port\":5905}",
                   VERSION_TAG, BUILD_VERSION, (long)getpid(), (long)now);
  if(n < 0) return -1;
  if((size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;
  return websrv_send(req->fd, 200, "application/json", body, (size_t)n);
}


static int
dispatch_request(const http_request_t *req, const char *initial_body,
                 size_t initial_size, size_t content_size) {
  if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
    if(!strcmp(req->path, "/") || !strcmp(req->path, "/files") ||
       !strcmp(req->path, "/index.html")) {
      return asset_request(req, "/files.html");
    }
    if(!strcmp(req->path, "/api/status") || !strcmp(req->path, "/api/version")) {
      return status_request(req);
    }
    if(!strcmp(req->path, "/fs") || !strncmp(req->path, "/fs/", 4)) {
      return fs_request(req, req->path);
    }
    if(!strncmp(req->path, "/api/fs/", 8)) {
      return transfer_request(req, req->path);
    }
    return asset_request(req, req->path);
  }

  if(!strcmp(req->method, "POST")) {
    if(!strcmp(req->path, "/api/fs/upload")) {
      return transfer_upload_request(req, initial_body, initial_size,
                                     content_size);
    }
    return websrv_send_error_json(req->fd, 404, "not found");
  }

  return websrv_send_error_json(req->fd, 405, "method not allowed");
}


typedef struct client_arg {
  int fd;
} client_arg_t;


static void *
client_thread(void *arg) {
  client_arg_t *client = arg;
  int fd = client->fd;
  char *buf = malloc(HEADER_MAX);
  size_t used = 0;
  int header_end = -1;

  free(client);
  if(!buf) {
    close(fd);
    return NULL;
  }

  while(used < HEADER_MAX) {
    ssize_t n = recv(fd, buf + used, HEADER_MAX - used, 0);
    if(n < 0) {
      if(errno == EINTR) continue;
      goto done;
    }
    if(n == 0) goto done;
    used += (size_t)n;
    header_end = find_header_end(buf, used);
    if(header_end >= 0) break;
  }

  if(header_end < 0) {
    websrv_send_error_json(fd, 400, "bad request");
    goto done;
  }

  buf[header_end] = 0;

  char method[8];
  char target[2048];
  if(sscanf(buf, "%7s %2047s", method, target) != 2) {
    websrv_send_error_json(fd, 400, "bad request");
    goto done;
  }

  http_request_t req;
  memset(&req, 0, sizeof(req));
  req.fd = fd;
  snprintf(req.method, sizeof(req.method), "%s", method);

  char *query = strchr(target, '?');
  if(query) {
    *query++ = 0;
    snprintf(req.query, sizeof(req.query), "%s", query);
  }
  websrv_url_decode(req.path, sizeof(req.path), target);

  size_t content_size = content_length_from_headers(buf);
  size_t initial_size = used - (size_t)header_end;
  const char *initial = buf + header_end;
  if(initial_size > content_size) initial_size = content_size;

  dispatch_request(&req, initial, initial_size, content_size);

done:
  free(buf);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  return NULL;
}


int
websrv_listen(unsigned short port, websrv_ready_cb_t ready_cb,
              void *ready_arg) {
  struct sockaddr_in server_addr;
  int srvfd;

  signal(SIGPIPE, SIG_IGN);

  srvfd = socket(AF_INET, SOCK_STREAM, 0);
  if(srvfd < 0) {
    int err = errno;
    perror("socket");
    return -err;
  }

  int on = 1;
  if(setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    int err = errno;
    perror("setsockopt");
    close(srvfd);
    return -err;
  }

#ifdef SO_REUSEPORT
  setsockopt(srvfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);

  if(bind(srvfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
    int err = errno;
    perror("bind");
    close(srvfd);
    return -err;
  }

  if(listen(srvfd, 16) != 0) {
    int err = errno;
    perror("listen");
    close(srvfd);
    return -err;
  }

  pthread_mutex_lock(&g_websrv_lock);
  g_websrv_srvfd = srvfd;
  pthread_mutex_unlock(&g_websrv_lock);

  if(ready_cb) ready_cb(port, ready_arg);

  while(1) {
    int connfd = accept(srvfd, NULL, NULL);
    if(connfd < 0) {
      if(errno == EINTR || errno == ECONNABORTED || errno == ECONNRESET) {
        continue;
      }
      int err = errno;
      perror("accept");

      pthread_mutex_lock(&g_websrv_lock);
      int active = g_websrv_srvfd == srvfd;
      if(active) g_websrv_srvfd = -1;
      pthread_mutex_unlock(&g_websrv_lock);

      if(active) close(srvfd);
      return -err;
    }

    client_arg_t *client = calloc(1, sizeof(*client));
    if(!client) {
      close(connfd);
      continue;
    }
    client->fd = connfd;

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if(pthread_create(&thread, &attr, client_thread, client) != 0) {
      close(connfd);
      free(client);
    }
    pthread_attr_destroy(&attr);
  }

  pthread_mutex_lock(&g_websrv_lock);
  g_websrv_srvfd = -1;
  pthread_mutex_unlock(&g_websrv_lock);

  return close(srvfd);
}
