/*
** libhttp.c
** HTTP library for Lua
*/

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <wininet.h>

  /* Link with ws2_32.lib is handled in Makefile */
  /* #pragma comment(lib, "ws2_32") */
  /* #pragma comment(lib, "wininet") */

  #define L_SOCKET SOCKET
  #define L_INVALID_SOCKET INVALID_SOCKET
  #define L_SOCKET_ERROR SOCKET_ERROR
  #define l_closesocket closesocket

  static void l_socket_init(void) {
      WSADATA wsaData;
      WSAStartup(MAKEWORD(2, 2), &wsaData);
  }

#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/time.h>
  #include <errno.h>

  #define L_SOCKET int
  #define L_INVALID_SOCKET -1
  #define L_SOCKET_ERROR -1
  #define l_closesocket close

  static void l_socket_init(void) {}

#endif

/*
** TLS (HTTPS) 支持：Linux / Android 使用 OpenSSL。
** Android 平台系统自带 BoringSSL（OpenSSL 1.1 兼容 API），可直接链接 libssl/libcrypto。
** 其余 POSIX 平台（macOS / Emscripten）暂未接入 TLS，保持仅 HTTP。
*/
#if defined(__linux__) || defined(__ANDROID__)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
  #include <limits.h>
#endif

#define L_HTTP_SOCKET "http.socket"

/* HTTPS 证书验证开关：0=严格验证（默认，安全），1=忽略证书错误 */
static int l_https_insecure = 0;

typedef struct {
    L_SOCKET sock;
} l_socket_ud;

/*
** ---- TLS 辅助（HTTPS）----
** 仅 Linux / Android 编译真实实现；其他 POSIX 平台提供空实现以保持单一代码路径。
*/
#if defined(__linux__) || defined(__ANDROID__)

typedef struct {
    SSL_CTX *ctx;
    SSL     *ssl;
} l_tls_t;

static void l_tls_init(void) {
    OPENSSL_init_ssl(0, NULL);
}

static int l_tls_is_ip(const char *host) {
    struct in_addr a4;
    struct in6_addr a6;
    return (inet_pton(AF_INET, host, &a4) == 1) || (inet_pton(AF_INET6, host, &a6) == 1);
}

static SSL_CTX *l_tls_ctx_new(int insecure) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    if (insecure) {
        /* 显式 set_insecure(true)：跳过证书与主机名校验（不安全，仅调试/自签名场景） */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
        return ctx;
    }

    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
#ifdef __ANDROID__
    /* Android 无 /etc/ssl/certs，系统 CA 存储在 /system/etc/security/cacerts（hash 目录） */
    if (SSL_CTX_load_verify_locations(ctx, NULL, "/system/etc/security/cacerts") != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
#else
    SSL_CTX_set_default_verify_paths(ctx);
#endif
    return ctx;
}

/* 在已连接的 socket 上完成 TLS 握手；成功返回 1，失败返回 0 */
static int l_tls_handshake(l_tls_t *tls, int sockfd, const char *host, int insecure) {
    tls->ssl = SSL_new(tls->ctx);
    if (!tls->ssl) return 0;

    SSL_set_fd(tls->ssl, sockfd);

    /* SNI：仅对域名设置，IP 地址不发送 SNI（RFC 6066） */
    if (!l_tls_is_ip(host)) {
        SSL_set_tlsext_host_name(tls->ssl, host);
    }

    /* 严格模式：校验服务器证书主机名（OpenSSL 会识别 IP 字面量并匹配 iPAddress SAN） */
    if (!insecure) {
        SSL_set1_host(tls->ssl, host);
    }

    ERR_clear_error();
    if (SSL_connect(tls->ssl) != 1) return 0;
    return 1;
}

/* 全量写出；成功返回 1。非阻塞 socket 上出现 WANT_READ/WANT_WRITE 时重试 */
static int l_tls_write_all(SSL *ssl, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > INT_MAX) chunk = INT_MAX;
        int n = SSL_write(ssl, data + sent, (int)chunk);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
        return 0;
    }
    return 1;
}

/* 返回 >0 读取字节数；0 = 连接关闭/非阻塞需重试；-1 = 错误 */
static ssize_t l_tls_read(SSL *ssl, char *buf, size_t len) {
    for (;;) {
        int n = SSL_read(ssl, buf, (int)len);
        if (n > 0) return (ssize_t)n;
        int err = SSL_get_error(ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
        if (err == SSL_ERROR_ZERO_RETURN) return 0; /* 对端 close_notify */
        return -1;
    }
}

static void l_tls_cleanup(l_tls_t *tls) {
    if (tls->ssl) SSL_free(tls->ssl);
    if (tls->ctx) SSL_CTX_free(tls->ctx);
    tls->ssl = NULL;
    tls->ctx = NULL;
}

/* 将 OpenSSL 错误队列中的首条错误追加到 Lua 错误串后（不弹栈，仅推入一条消息）
** verify_result: SSL_get_verify_result() 的返回值；非 X509_V_OK 时附加具体证书验证原因 */
static void l_tls_push_error(lua_State *L, const char *prefix, long verify_result) {
    char errbuf[256];
    char detail[512];
    unsigned long e = ERR_get_error();
    const char *msg;

    if (e) {
        ERR_error_string_n(e, errbuf, sizeof(errbuf));
        msg = errbuf;
    } else {
        msg = "unknown OpenSSL error";
    }

    if (verify_result != X509_V_OK) {
        const char *reason = X509_verify_cert_error_string(verify_result);
        snprintf(detail, sizeof(detail), "%s: %s [certificate verify: %s (0x%lX)]",
                 prefix, msg, reason, (unsigned long)verify_result);
        lua_pushstring(L, detail);
    } else {
        lua_pushfstring(L, "%s: %s", prefix, msg);
    }
}

#else /* 未接入 TLS 的 POSIX 平台（macOS / Emscripten） */

typedef struct { int dummy; } l_tls_t;
#define l_tls_init() ((void)0)
#define l_tls_cleanup(t) ((void)(t))
#define l_tls_is_ip(h) 0
#define l_tls_write_all(s, d, n) 0
#define l_tls_read(s, b, n) (-1)
#define l_tls_push_error(L, p, vr) lua_pushstring(L, p)
static int l_tls_handshake(l_tls_t *tls, int sockfd, const char *host, int insecure) {
    (void)tls; (void)sockfd; (void)host; (void)insecure;
    return 0;
}

#endif

/* Helper for DNS resolution using getaddrinfo (IPv4 enforced for now) */
static int l_resolve_addr(lua_State *L, const char *host, int port, struct sockaddr_in *res_addr) {
    struct addrinfo hints, *res;
    char port_str[16];

    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* Force IPv4 as we use AF_INET sockets */
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "DNS resolution failed: %s", gai_strerror(err));
        return 0; /* Error pushed */
    }

    if (res) {
        memcpy(res_addr, res->ai_addr, sizeof(struct sockaddr_in));
        freeaddrinfo(res);
        return 1; /* Success */
    }

    lua_pushnil(L);
    lua_pushstring(L, "DNS resolution failed: no address found");
    return 0;
}

/* Helper to parse URL into host, port, path */
static int parse_url(const char *url, char *host, size_t host_len, int *port, char *path, size_t path_len, int *is_https) {
    const char *p = url;
    const char *host_start;
    size_t h_len;

    *port = 80;
    *is_https = 0;

    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
        *is_https = 1;
    } else {
        return 0; /* Invalid scheme */
    }

    host_start = p;
    while (*p && *p != ':' && *p != '/') {
        p++;
    }

    h_len = p - host_start;
    if (h_len >= host_len) return 0; /* Host too long */
    memcpy(host, host_start, h_len);
    host[h_len] = '\0';

    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = (*port * 10) + (*p - '0');
            p++;
        }
    }

    if (*p == '\0') {
        if (path_len < 2) return 0;
        strcpy(path, "/");
    } else if (*p == '/') {
        if (strlen(p) >= path_len) return 0;
        strcpy(path, p);
    } else {
        return 0; /* Invalid URL format */
    }

    return 1;
}

/*
** Existing HTTP Request Implementation (Client High-Level)
*/
#if defined(_WIN32)

static int http_request(lua_State *L, const char *method) {
    const char *url = luaL_checkstring(L, 1);
    const char *body = NULL;
    size_t body_len = 0;
    const char *headers = NULL;

    if (strcmp(method, "POST") == 0) {
        body = luaL_optlstring(L, 2, NULL, &body_len);
        headers = luaL_optstring(L, 3, NULL);
    } else {
        headers = luaL_optstring(L, 2, NULL);
    }

    HINTERNET hInternet = InternetOpenA("LuaHTTPClient/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        lua_pushnil(L);
        lua_pushstring(L, "InternetOpen failed");
        return 2;
    }

    char host[256];
    char path[1024];
    int port;
    int is_https;

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https)) {
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "Invalid URL");
        return 2;
    }

    HINTERNET hConnect = InternetConnectA(hInternet, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "InternetConnect failed");
        return 2;
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (is_https) {
        flags |= INTERNET_FLAG_SECURE;
        /* 仅在显式设置 insecure 时才忽略证书错误（不安全，仅限调试/自签名场景） */
        if (l_https_insecure) {
            flags |= INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
        }
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, method, path, NULL, NULL, NULL, flags, 0);
    if (!hRequest) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "HttpOpenRequest failed");
        return 2;
    }

    BOOL res = HttpSendRequestA(hRequest, headers, headers ? (DWORD)strlen(headers) : 0, (LPVOID)body, (DWORD)body_len);
    if (!res) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        lua_pushnil(L);
        lua_pushstring(L, "HttpSendRequest failed");
        return 2;
    }

    /* Get Status Code */
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    HttpQueryInfoA(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &size, NULL);

    /* Read Body */
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        luaL_addlstring(&b, buffer, bytesRead);
    }

    /* 先 pushresult（body），再 push status，最后交换顺序，避免 luaL_Buffer 内部栈操作影响 status */
    luaL_pushresult(&b);
    lua_pushinteger(L, statusCode);
    lua_insert(L, -2);

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return 2;
}

#elif defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)

static int http_request(lua_State *L, const char *method) {
    const char *url = luaL_checkstring(L, 1);
    const char *body = NULL;
    size_t body_len = 0;
    const char *headers = NULL;

    if (strcmp(method, "POST") == 0) {
        body = luaL_optlstring(L, 2, NULL, &body_len);
        headers = luaL_optstring(L, 3, NULL);
    } else {
        headers = luaL_optstring(L, 2, NULL);
    }

    char host[256];
    char path[1024];
    int port;
    int is_https;

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https)) {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid URL");
        return 2;
    }

    l_tls_t tls;
    memset(&tls, 0, sizeof(tls));

#if defined(__linux__) || defined(__ANDROID__)
    if (is_https) {
        l_tls_init();
        tls.ctx = l_tls_ctx_new(l_https_insecure);
        if (!tls.ctx) {
            lua_pushnil(L);
            l_tls_push_error(L, "TLS context creation failed", X509_V_OK);
            return 2;
        }
    }
#else
    if (is_https) {
        lua_pushnil(L);
        lua_pushstring(L, "HTTPS not supported on this platform without external libraries");
        return 2;
    }
#endif

    struct sockaddr_in serv_addr;
    if (!l_resolve_addr(L, host, port, &serv_addr)) {
        l_tls_cleanup(&tls);
        return 2; /* Error already pushed */
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        l_tls_cleanup(&tls);
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        l_tls_cleanup(&tls);
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

#if defined(__linux__) || defined(__ANDROID__)
    if (is_https && !l_tls_handshake(&tls, sockfd, host, l_https_insecure)) {
        /* SSL_free 前先取证书验证结果，握手因证书失败时给出具体 X509 原因 */
        long verify_result = X509_V_OK;
        if (tls.ssl) verify_result = SSL_get_verify_result(tls.ssl);
        close(sockfd);
        l_tls_cleanup(&tls);
        lua_pushnil(L);
        l_tls_push_error(L, "TLS handshake failed", verify_result);
        return 2;
    }
#endif

    /* Construct HTTP Request */
    luaL_Buffer req;
    luaL_buffinit(L, &req);
    luaL_addstring(&req, method);
    luaL_addstring(&req, " ");
    luaL_addstring(&req, path);
    luaL_addstring(&req, " HTTP/1.0\r\n");
    luaL_addstring(&req, "Host: ");
    luaL_addstring(&req, host);
    luaL_addstring(&req, "\r\n");
    luaL_addstring(&req, "User-Agent: LuaHTTPClient/1.0\r\n");
    luaL_addstring(&req, "Connection: close\r\n");

    if (body) {
        char len_str[32];
        snprintf(len_str, sizeof(len_str), "%lu", (unsigned long)body_len);
        luaL_addstring(&req, "Content-Length: ");
        luaL_addstring(&req, len_str);
        luaL_addstring(&req, "\r\n");
    }

    if (headers) {
        luaL_addstring(&req, headers);
        if (headers[strlen(headers)-1] != '\n') {
            luaL_addstring(&req, "\r\n");
        }
    }

    luaL_addstring(&req, "\r\n");
    if (body) {
        luaL_addlstring(&req, body, body_len);
    }

    luaL_pushresult(&req);
    const char *req_str = lua_tostring(L, -1);
    size_t req_len = lua_rawlen(L, -1);

#if defined(__linux__) || defined(__ANDROID__)
    if (tls.ssl) {
        if (!l_tls_write_all(tls.ssl, req_str, req_len)) {
            l_tls_cleanup(&tls);
            close(sockfd);
            lua_pushnil(L);
            l_tls_push_error(L, "Send failed", X509_V_OK);
            return 2;
        }
    } else
#endif
    {
        ssize_t sent = 0;
        while (sent < req_len) {
            ssize_t n = send(sockfd, req_str + sent, req_len - sent, 0);
            if (n < 0) {
                close(sockfd);
                lua_pushnil(L);
                lua_pushstring(L, "Send failed");
                return 2;
            }
            sent += n;
        }
    }
    lua_pop(L, 1); /* pop request string */

    /* Read Response */
    luaL_Buffer resp;
    luaL_buffinit(L, &resp);
    char buffer[4096];
    ssize_t n;
#if defined(__linux__) || defined(__ANDROID__)
    if (tls.ssl) {
        while ((n = l_tls_read(tls.ssl, buffer, sizeof(buffer))) > 0) {
            luaL_addlstring(&resp, buffer, n);
        }
    } else
#endif
    {
        while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
            luaL_addlstring(&resp, buffer, n);
        }
    }
    l_tls_cleanup(&tls);
    close(sockfd);

    luaL_pushresult(&resp);
    const char *full_resp = lua_tostring(L, -1);
    size_t full_len = lua_rawlen(L, -1);

    /* Parse Status Code */
    int status_code = 0;
    const char *status_line_end = strstr(full_resp, "\r\n");
    if (status_line_end) {
        const char *p = full_resp;
        while (p < status_line_end && *p != ' ') p++;
        if (p < status_line_end) {
            status_code = atoi(p + 1);
        }
    }

    /* Find Body */
    const char *body_start = strstr(full_resp, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        lua_pushinteger(L, status_code);
        lua_pushlstring(L, body_start, full_len - (body_start - full_resp));
    } else {
        lua_pushinteger(L, status_code);
        lua_pushlstring(L, "", 0);
    }

    return 2;
}

#else

/* Placeholder for other platforms */
static int http_request(lua_State *L, const char *method) {
    lua_pushnil(L);
    lua_pushstring(L, "Platform not supported");
    return 2;
}

#endif

static int l_http_get(lua_State *L) {
    return http_request(L, "GET");
}

static int l_http_post(lua_State *L) {
    return http_request(L, "POST");
}

/*
** 设置 HTTPS 证书验证模式
** 参数: insecure (boolean) - true=忽略证书错误（不安全），false=严格验证（默认）
*/
static int l_http_set_insecure(lua_State *L) {
    int insecure = lua_toboolean(L, 1);
    l_https_insecure = insecure ? 1 : 0;
    return 0;
}

/*
** Socket API Implementation
*/

static l_socket_ud *l_check_socket(lua_State *L, int index) {
    return (l_socket_ud *)luaL_checkudata(L, index, L_HTTP_SOCKET);
}

static int l_socket_close(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    if (ud->sock != L_INVALID_SOCKET) {
        l_closesocket(ud->sock);
        ud->sock = L_INVALID_SOCKET;
    }
    return 0;
}

static int l_socket_accept(lua_State *L) {
    l_socket_ud *server = l_check_socket(L, 1);
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    if (server->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    L_SOCKET newsock = accept(server->sock, (struct sockaddr *)&cli_addr, &clilen);
    if (newsock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Accept failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = newsock;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static int l_socket_recv(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    size_t len = (size_t)luaL_optinteger(L, 2, 4096);

    if (ud->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    char *buffer = (char *)malloc(len);
    if (!buffer) {
        return luaL_error(L, "Out of memory");
    }

    int n = recv(ud->sock, buffer, (int)len, 0);
    if (n > 0) {
        lua_pushlstring(L, buffer, n);
        free(buffer);
        return 1;
    } else if (n == 0) {
        free(buffer);
        return 0; /* Connection closed */
    } else {
        free(buffer);
        lua_pushnil(L);
        lua_pushstring(L, "Receive error");
        return 2;
    }
}

static int l_socket_send(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    size_t sent = 0;

    if (ud->sock == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket is closed");
        return 2;
    }

    while (sent < len) {
        size_t remaining = len - sent;
        int chunk;
        int n;

        /* Clamp chunk size to ensure it fits in int (required by Windows send) and avoiding huge buffers */
        chunk = (remaining > 65536) ? 65536 : (int)remaining;

        n = send(ud->sock, data + sent, chunk, 0);
        if (n < 0) {
            lua_pushnil(L);
            lua_pushstring(L, "Send error");
            lua_pushinteger(L, (lua_Integer)sent); /* Return partial count as 3rd result */
            return 3;
        }
        sent += n;
    }

    lua_pushinteger(L, (lua_Integer)sent);
    return 1;
}

static int l_socket_settimeout(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    int ms = (int)(luaL_checknumber(L, 2) * 1000); /* seconds to ms */

#ifdef _WIN32
    DWORD timeout = ms;
    setsockopt(ud->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(ud->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(ud->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(ud->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return 0;
}

static int l_socket_bind(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (strcmp(host, "*") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (!l_resolve_addr(L, host, port, &addr)) {
            return 2; /* Error pushed by l_resolve_addr */
        }
    }

    if (bind(ud->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Bind failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_listen(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    int backlog = (int)luaL_optinteger(L, 2, 5);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    if (listen(ud->sock, backlog) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Listen failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_connect(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *host = luaL_checkstring(L, 2);
    int port = (int)luaL_checkinteger(L, 3);

    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    if (!l_resolve_addr(L, host, port, &addr)) {
        return 2;
    }

    if (connect(ud->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int l_socket_shutdown(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    const char *how_str = luaL_optstring(L, 2, "both");
    int how = 2; /* SD_BOTH or SHUT_RDWR */

#ifdef _WIN32
    if (strcmp(how_str, "read") == 0) how = SD_RECEIVE;
    else if (strcmp(how_str, "write") == 0) how = SD_SEND;
    else how = SD_BOTH;
#else
    if (strcmp(how_str, "read") == 0) how = SHUT_RD;
    else if (strcmp(how_str, "write") == 0) how = SHUT_WR;
    else how = SHUT_RDWR;
#endif

    if (ud->sock != L_INVALID_SOCKET) {
        shutdown(ud->sock, how);
    }
    return 0;
}

static int l_socket_getsockname(lua_State *L) {
    l_socket_ud *ud = l_check_socket(L, 1);
    if (ud->sock == L_INVALID_SOCKET) return luaL_error(L, "Socket closed");

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (getsockname(ud->sock, (struct sockaddr *)&addr, &len) < 0) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, inet_ntoa(addr.sin_addr));
    lua_pushinteger(L, ntohs(addr.sin_port));
    return 2;
}

/* Constructor: http.server(port) */
static int l_http_server(lua_State *L) {
    int port = (int)luaL_checkinteger(L, 1);

    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Bind failed");
        return 2;
    }

    if (listen(sockfd, 5) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Listen failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

/* Constructor: http.client(host, port) */
static int l_http_socket_new(lua_State *L) {
    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    /* Set SO_REUSEADDR by default to avoid "Address already in use" annoyances during dev */
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static int l_http_client(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);

    struct sockaddr_in serv_addr;
    if (!l_resolve_addr(L, host, port, &serv_addr)) {
        return 2;
    }

    L_SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == L_INVALID_SOCKET) {
        lua_pushnil(L);
        lua_pushstring(L, "Socket creation failed");
        return 2;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        l_closesocket(sockfd);
        lua_pushnil(L);
        lua_pushstring(L, "Connection failed");
        return 2;
    }

    l_socket_ud *ud = (l_socket_ud *)lua_newuserdata(L, sizeof(l_socket_ud));
    ud->sock = sockfd;
    luaL_getmetatable(L, L_HTTP_SOCKET);
    lua_setmetatable(L, -2);

    return 1;
}

static const luaL_Reg httplib[] = {
    {"get", l_http_get},
    {"post", l_http_post},
    {"set_insecure", l_http_set_insecure},
    {"server", l_http_server},
    {"client", l_http_client},
    {"socket", l_http_socket_new},
    {NULL, NULL}
};

static const luaL_Reg socket_methods[] = {
    {"bind", l_socket_bind},
    {"listen", l_socket_listen},
    {"connect", l_socket_connect},
    {"accept", l_socket_accept},
    {"recv", l_socket_recv},
    {"send", l_socket_send},
    {"close", l_socket_close},
    {"shutdown", l_socket_shutdown},
    {"getsockname", l_socket_getsockname},
    {"settimeout", l_socket_settimeout},
    {"__gc", l_socket_close},
    {NULL, NULL}
};

LUAMOD_API int luaopen_http(lua_State *L) {
    l_socket_init();
    l_tls_init();

    luaL_newmetatable(L, L_HTTP_SOCKET);
    lua_pushvalue(L, -1); /* push metatable */
    lua_setfield(L, -2, "__index"); /* metatable.__index = metatable */
    luaL_setfuncs(L, socket_methods, 0);
    lua_pop(L, 1);

    luaL_newlib(L, httplib);
    return 1;
}
