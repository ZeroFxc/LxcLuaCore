/**
 * @file libhttp.c
 * @brief LXCLUA 完整跨平台网络服务库 - 实现文件
 * 
 * 提供完整的网络服务支持：
 * - 跨平台 Socket 抽象层 (Windows/Linux/macOS/Android/Emscripten)
 * - TLS/SSL 加密传输 (SChannel / OpenSSL / SecureTransport)
 * - HTTP/HTTPS 客户端 (GET/POST/PUT/DELETE/PATCH/HEAD/OPTIONS)
 * - HTTP/HTTPS 服务器 (路由、中间件、静态文件)
 * - WebSocket 客户端和服务器 (RFC 6455)
 * - URL/Query/Cookie/MIME 工具
 */

#define LUA_LIB

#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lnetwork.h"

#include <ctype.h>
#include <stdarg.h>
#include <time.h>

/* ========================================================================
 * 前向声明和内部结构
 * ======================================================================== */

/**
 * @brief 内部 TLS 上下文结构体
 * 平台相关的 TLS 状态数据
 */
typedef struct lnet_tls {
    lnet_tls_mode_t mode;
    int handshake_done;
#if defined(LNET_PLATFORM_WINDOWS)
    /* SChannel handles */
    CredHandle cred_handle;
    CtxtHandle ctxt_handle;
    SecPkgContext_StreamSizes stream_sizes;
    BOOL cred_acquired;
    BOOL ctxt_established;
    /* Buffers for SChannel */
    SecBuffer in_buffer;
    SecBuffer out_buffer;
    uint8_t *in_data;
    size_t in_data_len;
    size_t in_data_offset;
    uint8_t *decrypt_buf;
    size_t decrypt_buf_len;
    size_t decrypt_buf_offset;
#elif defined(LNET_HAVE_OPENSSL)
    /* OpenSSL handles */
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#elif defined(LNET_PLATFORM_MACOS) || defined(LNET_PLATFORM_IOS)
    /* SecureTransport */
    SSLContextRef ssl_ctx;
#elif defined(LNET_PLATFORM_WASM)
    /* WASM: no native TLS */
    int placeholder;
#endif
} lnet_tls;

/**
 * @brief WebSocket 发送队列项
 */
typedef struct lnet_ws_send_item {
    uint8_t *data;
    size_t len;
    int owned;  /* 是否需要释放 data */
    struct lnet_ws_send_item *next;
} lnet_ws_send_item;

/**
 * @brief 服务器连接结构体 (内部)
 */
typedef struct lnet_server_conn {
    lnet_socket *sock;
    struct lnet_http_server *server;
    char *recv_buf;
    size_t recv_buf_len;
    size_t recv_buf_cap;
    int headers_parsed;
    int keepalive_count;
    double last_activity;
    struct lnet_server_conn *next;
    struct lnet_server_conn *prev;
} lnet_server_conn;

/* ========================================================================
 * SHA1 实现 (用于 WebSocket Accept Key 计算)
 * 基于 Public Domain SHA1 实现
 * ======================================================================== */

#define LNET_SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} lnet_sha1_ctx;

#define lnet_sha1_rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void lnet_sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e;
    typedef union { uint8_t c[64]; uint32_t l[16]; } CHAR64LONG16;
    CHAR64LONG16 block;
    memcpy(&block, buffer, 64);

#define blk0(i) (block.l[i] = (lnet_sha1_rol(block.l[i],24)&0xFF00FF00) | (lnet_sha1_rol(block.l[i],8)&0x00FF00FF))
#define blk(i) (block.l[i&15] = lnet_sha1_rol(block.l[(i+13)&15]^block.l[(i+8)&15]^block.l[(i+2)&15]^block.l[i&15],1))
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+lnet_sha1_rol(v,5);w=lnet_sha1_rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+lnet_sha1_rol(v,5);w=lnet_sha1_rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+lnet_sha1_rol(v,5);w=lnet_sha1_rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+lnet_sha1_rol(v,5);w=lnet_sha1_rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+lnet_sha1_rol(v,5);w=lnet_sha1_rol(w,30);

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
#undef R0
#undef R1
#undef R2
#undef R3
#undef R4
#undef blk0
#undef blk
}

static void lnet_sha1_init(lnet_sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void lnet_sha1_update(lnet_sha1_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i, j;
    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        lnet_sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64)
            lnet_sha1_transform(ctx->state, data + i);
        j = 0;
    } else i = 0;
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void lnet_sha1_final(lnet_sha1_ctx *ctx, uint8_t digest[LNET_SHA1_DIGEST_SIZE]) {
    uint32_t i;
    uint8_t finalcount[8];
    for (i = 0; i < 8; i++)
        finalcount[i] = (unsigned char)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    lnet_sha1_update(ctx, (uint8_t *)"\200", 1);
    while ((ctx->count[0] & 504) != 448)
        lnet_sha1_update(ctx, (uint8_t *)"\0", 1);
    lnet_sha1_update(ctx, finalcount, 8);
    for (i = 0; i < LNET_SHA1_DIGEST_SIZE; i++)
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    memset(ctx, 0, sizeof(*ctx));
}

/* ========================================================================
 * 全局状态 (引用计数)
 * ======================================================================== */

static int lnet_global_init_cnt = 0;

#if defined(LNET_PLATFORM_WINDOWS)
static WSADATA lnet_wsa_data;
#endif

#if defined(LNET_HAVE_OPENSSL)
static int lnet_openssl_initialized = 0;
static l_mutex_t lnet_openssl_lock;
#endif

/* ========================================================================
 * 全局初始化和清理
 * ======================================================================== */

/**
 * @brief 初始化网络库
 * 自动调用 WSAStartup (Windows) 或 OpenSSL 初始化
 */
void lnet_global_init(void) {
    if (lnet_global_init_cnt > 0) {
        lnet_global_init_cnt++;
        return;
    }
#if defined(LNET_PLATFORM_WINDOWS)
    WSAStartup(MAKEWORD(2, 2), &lnet_wsa_data);
#elif defined(LNET_HAVE_OPENSSL)
    l_mutex_init(&lnet_openssl_lock);
#endif
    lnet_global_init_cnt = 1;
}

/**
 * @brief 清理网络库
 */
void lnet_global_cleanup(void) {
    lnet_global_init_cnt--;
    if (lnet_global_init_cnt > 0) return;
    if (lnet_global_init_cnt < 0) { lnet_global_init_cnt = 0; return; }
#if defined(LNET_PLATFORM_WINDOWS)
    WSACleanup();
#elif defined(LNET_HAVE_OPENSSL)
    if (lnet_openssl_initialized) {
        lnet_openssl_initialized = 0;
        l_mutex_destroy(&lnet_openssl_lock);
    }
#endif
}

/* ========================================================================
 * Socket API 实现
 * ======================================================================== */

/**
 * @brief 创建一个新的 lnet_socket 对象
 * @param family 地址族 (LNET_AF_INET 或 LNET_AF_INET6)
 * @param type Socket 类型 (LNET_SOCK_STREAM 或 LNET_SOCK_DGRAM)
 * @param nonblocking 是否设置为非阻塞模式
 * @return 新 socket 对象，失败返回 NULL
 */
lnet_socket *lnet_socket_new(lnet_family_t family, lnet_socktype_t type, int nonblocking) {
    lnet_socket *sock = (lnet_socket *)calloc(1, sizeof(lnet_socket));
    if (!sock) return NULL;

    sock->fd = socket((int)family, (int)type, 0);
    if (sock->fd == LNET_INVALID_SOCKET) {
        free(sock);
        return NULL;
    }

    sock->family = family;
    sock->is_nonblocking = 0;
    sock->is_connected = 0;
    sock->is_closed = 0;
    sock->tls = NULL;
    sock->tls_mode = LNET_TLS_NONE;
    sock->read_buf = NULL;
    sock->read_buf_size = 0;
    sock->read_buf_pos = 0;
    sock->write_buf = NULL;
    sock->write_buf_size = 0;
    sock->write_buf_pos = 0;
    sock->loop = NULL;
    memset(&sock->io_watcher, 0, sizeof(sock->io_watcher));
    sock->user_data = NULL;
    sock->recv_timeout_ms = 30000;
    sock->send_timeout_ms = 30000;
    sock->keepalive = 0;
    sock->nodelay = 1;

    if (nonblocking) {
#if defined(LNET_PLATFORM_WINDOWS)
        u_long mode = 1;
        ioctlsocket(sock->fd, FIONBIO, &mode);
#else
        int flags = fcntl(sock->fd, F_GETFL, 0);
        fcntl(sock->fd, F_SETFL, flags | O_NONBLOCK);
#endif
        sock->is_nonblocking = 1;
    }

    /* 默认开启 SO_REUSEADDR */
    {
        int opt = 1;
        setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    }

    return sock;
}

/**
 * @brief 关闭并释放 socket 对象
 * @param sock Socket 对象指针
 */
void lnet_socket_free(lnet_socket *sock) {
    if (!sock) return;
    if (sock->tls) {
        lnet_tls_shutdown(sock);
    }
    if (sock->io_watcher.active && sock->loop) {
        ev_io_stop(sock->loop, &sock->io_watcher);
    }
    if (sock->fd != LNET_INVALID_SOCKET) {
        lnet_closesocket(sock->fd);
        sock->fd = LNET_INVALID_SOCKET;
    }
    free(sock->read_buf);
    free(sock->write_buf);
    free(sock);
}

/**
 * @brief 连接到远程地址
 * @param sock Socket 对象
 * @param host 主机名或 IP 地址
 * @param port 端口号
 * @return 0 成功，-1 失败 (错误码存入 sock->last_error)
 */
int lnet_socket_connect(lnet_socket *sock, const char *host, int port) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return -1;

    lnet_addrinfo *info = lnet_resolve(host, port, sock->family);
    if (!info) {
        sock->last_error = -1;
        return -1;
    }

    /* 尝试连接解析到的第一个地址 */
    int result = -1;
    lnet_addrinfo *cur = info;
    while (cur) {
        struct sockaddr_storage addr;
        memset(&addr, 0, sizeof(addr));
        if (cur->family == LNET_AF_INET6) {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addr;
            a6->sin6_family = AF_INET6;
            a6->sin6_port = htons((uint16_t)cur->port);
#if defined(LNET_PLATFORM_WINDOWS)
            inet_pton(AF_INET6, cur->ip, &a6->sin6_addr);
#else
            inet_pton(AF_INET6, cur->ip, &a6->sin6_addr);
#endif
            if (connect(sock->fd, (struct sockaddr *)a6, sizeof(*a6)) == 0) {
                sock->family = LNET_AF_INET6;
                result = 0;
                break;
            }
        } else {
            struct sockaddr_in *a4 = (struct sockaddr_in *)&addr;
            a4->sin_family = AF_INET;
            a4->sin_port = htons((uint16_t)cur->port);
#if defined(LNET_PLATFORM_WINDOWS)
            inet_pton(AF_INET, cur->ip, &a4->sin_addr);
#else
            inet_pton(AF_INET, cur->ip, &a4->sin_addr);
#endif
            if (connect(sock->fd, (struct sockaddr *)a4, sizeof(*a4)) == 0) {
                sock->family = LNET_AF_INET;
                result = 0;
                break;
            }
        }
        cur = cur->next;
    }

    lnet_free_addrinfo(info);

    if (result == 0) {
        sock->is_connected = 1;
        if (sock->nodelay) {
            int flag = 1;
            setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
        }
        if (sock->keepalive) {
            int flag = 1;
            setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&flag, sizeof(flag));
        }
    } else {
        sock->last_error = -1;
    }
    return result;
}

/**
 * @brief 绑定 socket 到指定地址和端口
 * @param sock Socket 对象
 * @param host 绑定地址 (NULL 或 "0.0.0.0" 表示任意地址)
 * @param port 端口号
 * @return 0 成功，-1 失败
 */
int lnet_socket_bind(lnet_socket *sock, const char *host, int port) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return -1;

    if (sock->family == LNET_AF_INET6) {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons((uint16_t)port);
        if (host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0 && strcmp(host, "::") != 0) {
#if defined(LNET_PLATFORM_WINDOWS)
            inet_pton(AF_INET6, host, &addr.sin6_addr);
#else
            inet_pton(AF_INET6, host, &addr.sin6_addr);
#endif
        } else {
            addr.sin6_addr = in6addr_any;
        }
        if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    } else {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (host && host[0] != '\0' && strcmp(host, "0.0.0.0") != 0) {
#if defined(LNET_PLATFORM_WINDOWS)
            inet_pton(AF_INET, host, &addr.sin_addr);
#else
            inet_pton(AF_INET, host, &addr.sin_addr);
#endif
        } else {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    }
    return 0;
}

/**
 * @brief 开始监听连接
 * @param sock Socket 对象
 * @param backlog 连接队列最大长度
 * @return 0 成功，-1 失败
 */
int lnet_socket_listen(lnet_socket *sock, int backlog) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return -1;
    if (backlog <= 0) backlog = LNET_SERVER_BACKLOG;
    if (listen(sock->fd, backlog) < 0) return -1;
    return 0;
}

/**
 * @brief 接受一个新连接
 * @param sock 监听 socket
 * @return 新 socket 对象，失败返回 NULL
 */
lnet_socket *lnet_socket_accept(lnet_socket *sock) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return NULL;

    if (sock->family == LNET_AF_INET6) {
        struct sockaddr_in6 addr;
        lnet_socklen_t addrlen = sizeof(addr);
        LNET_SOCKET newfd = accept(sock->fd, (struct sockaddr *)&addr, &addrlen);
        if (newfd == LNET_INVALID_SOCKET) return NULL;

        lnet_socket *newsock = (lnet_socket *)calloc(1, sizeof(lnet_socket));
        if (!newsock) { lnet_closesocket(newfd); return NULL; }
        newsock->fd = newfd;
        newsock->family = LNET_AF_INET6;
        newsock->is_connected = 1;
        newsock->recv_timeout_ms = sock->recv_timeout_ms;
        newsock->send_timeout_ms = sock->send_timeout_ms;
        newsock->nodelay = 1;
        {
            int flag = 1;
            setsockopt(newsock->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
        }
        return newsock;
    } else {
        struct sockaddr_in addr;
        lnet_socklen_t addrlen = sizeof(addr);
        LNET_SOCKET newfd = accept(sock->fd, (struct sockaddr *)&addr, &addrlen);
        if (newfd == LNET_INVALID_SOCKET) return NULL;

        lnet_socket *newsock = (lnet_socket *)calloc(1, sizeof(lnet_socket));
        if (!newsock) { lnet_closesocket(newfd); return NULL; }
        newsock->fd = newfd;
        newsock->family = LNET_AF_INET;
        newsock->is_connected = 1;
        newsock->recv_timeout_ms = sock->recv_timeout_ms;
        newsock->send_timeout_ms = sock->send_timeout_ms;
        newsock->nodelay = 1;
        {
            int flag = 1;
            setsockopt(newsock->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
        }
        return newsock;
    }
}

/**
 * @brief 发送数据 (保证完整发送所有数据)
 * @param sock Socket 对象
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return 已发送总字节数，-1 失败
 */
int lnet_socket_send(lnet_socket *sock, const void *data, size_t len) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET || sock->is_closed) return -1;
    if (len == 0) return 0;

    if (sock->tls && sock->tls->handshake_done) {
        return lnet_tls_write(sock, data, len);
    }

    size_t total_sent = 0;
    const char *ptr = (const char *)data;
    while (total_sent < len) {
        int chunk = (len - total_sent > 65536) ? 65536 : (int)(len - total_sent);
        int n = send(sock->fd, ptr + total_sent, chunk, 0);
        if (n < 0) {
#if defined(LNET_PLATFORM_WINDOWS)
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) continue;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
#endif
            sock->last_error = -1;
            return -1;
        }
        total_sent += n;
    }
    return (int)total_sent;
}

/**
 * @brief 接收数据
 * @param sock Socket 对象
 * @param buf 接收缓冲区
 * @param len 最大接收长度
 * @return 实际接收字节数，0=连接关闭，-1=错误
 */
int lnet_socket_recv(lnet_socket *sock, void *buf, size_t len) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET || sock->is_closed) return -1;
    if (len == 0) return 0;

    if (sock->tls && sock->tls->handshake_done) {
        return lnet_tls_read(sock, buf, len);
    }

    int n = recv(sock->fd, buf, (int)len, 0);
    if (n < 0) {
#if defined(LNET_PLATFORM_WINDOWS)
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            sock->last_error = lnet_EWOULDBLOCK;
            return -1;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            sock->last_error = lnet_EWOULDBLOCK;
            return -1;
        }
#endif
        sock->last_error = -1;
        return -1;
    }
    if (n == 0) sock->is_closed = 1;
    return n;
}

/**
 * @brief 设置 socket 选项
 * @param sock Socket 对象
 * @param keepalive 是否启用 TCP Keep-Alive
 * @param nodelay 是否禁用 Nagle 算法
 * @param recv_timeout_ms 接收超时 (毫秒)
 * @param send_timeout_ms 发送超时 (毫秒)
 */
void lnet_socket_setopts(lnet_socket *sock, int keepalive, int nodelay,
                         int recv_timeout_ms, int send_timeout_ms) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return;

    sock->keepalive = keepalive;
    sock->nodelay = nodelay;
    sock->recv_timeout_ms = recv_timeout_ms;
    sock->send_timeout_ms = send_timeout_ms;

    if (keepalive) {
        int opt = 1;
        setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt, sizeof(opt));
    }
    if (nodelay) {
        int opt = 1;
        setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&opt, sizeof(opt));
    }

#if defined(LNET_PLATFORM_WINDOWS)
    if (recv_timeout_ms > 0) {
        DWORD tv = (DWORD)recv_timeout_ms;
        setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    }
    if (send_timeout_ms > 0) {
        DWORD tv = (DWORD)send_timeout_ms;
        setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    }
#else
    if (recv_timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = recv_timeout_ms / 1000;
        tv.tv_usec = (recv_timeout_ms % 1000) * 1000;
        setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    if (send_timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = send_timeout_ms / 1000;
        tv.tv_usec = (send_timeout_ms % 1000) * 1000;
        setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
#endif
}

/**
 * @brief 从 socket 读取一行数据 (以 \r\n 结尾)
 * @param sock Socket 对象
 * @param buf 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return 读取的字符数 (不包含 \r\n)，-1 失败或超时
 */
int lnet_socket_readline(lnet_socket *sock, char *buf, size_t max_len) {
    if (!sock || !buf || max_len == 0) return -1;
    size_t pos = 0;
    char c;
    while (pos < max_len - 1) {
        int n = lnet_socket_recv(sock, &c, 1);
        if (n <= 0) return -1;
        if (c == '\r') {
            /* 预读下一个字符，检查是否 \n */
            int n2 = lnet_socket_recv(sock, &c, 1);
            if (n2 > 0 && c == '\n') {
                buf[pos] = '\0';
                return (int)pos;
            }
            /* 如果不是 \n，将字符放回 */
            /* 简化处理：如果下一个不是 \n，视为行结束 */
            buf[pos] = '\0';
            return (int)pos;
        }
        if (c == '\n') {
            buf[pos] = '\0';
            return (int)pos;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

/**
 * @brief 获取对端地址信息
 * @param sock Socket 对象
 * @param ip 输出 IP 字符串缓冲区
 * @param ip_size IP 缓冲区大小
 * @param port 输出端口号
 * @return 0 成功，-1 失败
 */
int lnet_socket_getpeername(lnet_socket *sock, char *ip, size_t ip_size, int *port) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return -1;

    if (sock->family == LNET_AF_INET6) {
        struct sockaddr_in6 addr;
        lnet_socklen_t len = sizeof(addr);
        if (getpeername(sock->fd, (struct sockaddr *)&addr, &len) < 0) return -1;
        if (ip) inet_ntop(AF_INET6, &addr.sin6_addr, ip, (socklen_t)ip_size);
        if (port) *port = ntohs(addr.sin6_port);
    } else {
        struct sockaddr_in addr;
        lnet_socklen_t len = sizeof(addr);
        if (getpeername(sock->fd, (struct sockaddr *)&addr, &len) < 0) return -1;
        if (ip) inet_ntop(AF_INET, &addr.sin_addr, ip, (socklen_t)ip_size);
        if (port) *port = ntohs(addr.sin_port);
    }
    return 0;
}

/**
 * @brief 获取本地地址信息
 * @param sock Socket 对象
 * @param ip 输出 IP 字符串缓冲区
 * @param ip_size IP 缓冲区大小
 * @param port 输出端口号
 * @return 0 成功，-1 失败
 */
int lnet_socket_getsockname(lnet_socket *sock, char *ip, size_t ip_size, int *port) {
    if (!sock || sock->fd == LNET_INVALID_SOCKET) return -1;

    if (sock->family == LNET_AF_INET6) {
        struct sockaddr_in6 addr;
        lnet_socklen_t len = sizeof(addr);
        if (getsockname(sock->fd, (struct sockaddr *)&addr, &len) < 0) return -1;
        if (ip) inet_ntop(AF_INET6, &addr.sin6_addr, ip, (socklen_t)ip_size);
        if (port) *port = ntohs(addr.sin6_port);
    } else {
        struct sockaddr_in addr;
        lnet_socklen_t len = sizeof(addr);
        if (getsockname(sock->fd, (struct sockaddr *)&addr, &len) < 0) return -1;
        if (ip) inet_ntop(AF_INET, &addr.sin_addr, ip, (socklen_t)ip_size);
        if (port) *port = ntohs(addr.sin_port);
    }
    return 0;
}

/**
 * @brief 通过 TLS 或明文发送完整数据 (别名函数)
 * @param sock Socket 对象
 * @param data 数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_socket_send_all(lnet_socket *sock, const void *data, size_t len) {
    int result = lnet_socket_send(sock, data, len);
    if (result < 0) return -1;
    if ((size_t)result != len) return -1;
    return 0;
}

/* ========================================================================
 * DNS API 实现
 * ======================================================================== */

/**
 * @brief DNS 解析主机名到地址列表
 * @param host 主机名或 IP 地址
 * @param port 端口号
 * @param family 地址族偏好 (LNET_AF_UNSPEC 表示任意)
 * @return 地址信息链表头，调用者负责通过 lnet_free_addrinfo 释放
 */
lnet_addrinfo *lnet_resolve(const char *host, int port, lnet_family_t family) {
    struct addrinfo hints, *res = NULL;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = (int)family;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0 || !res) return NULL;

    lnet_addrinfo *head = NULL, *tail = NULL;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        lnet_addrinfo *node = (lnet_addrinfo *)calloc(1, sizeof(lnet_addrinfo));
        if (!node) continue;

        node->port = port;
        node->family = (lnet_family_t)ai->ai_family;
        if (ai->ai_family == AF_INET6) {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ai->ai_addr;
            inet_ntop(AF_INET6, &a6->sin6_addr, node->ip, sizeof(node->ip));
        } else if (ai->ai_family == AF_INET) {
            struct sockaddr_in *a4 = (struct sockaddr_in *)ai->ai_addr;
            inet_ntop(AF_INET, &a4->sin_addr, node->ip, sizeof(node->ip));
        } else {
            free(node);
            continue;
        }

        if (!head) head = tail = node;
        else { tail->next = node; tail = node; }
    }
    freeaddrinfo(res);
    return head;
}

/**
 * @brief 释放 DNS 解析结果链表
 * @param info 链表头指针
 */
void lnet_free_addrinfo(lnet_addrinfo *info) {
    while (info) {
        lnet_addrinfo *next = info->next;
        free(info);
        info = next;
    }
}

/* ========================================================================
 * TLS API 实现
 * ======================================================================== */

/**
 * @brief 全局 TLS 初始化
 * @return 0 成功，-1 失败
 */
int lnet_tls_init(void) {
#if defined(LNET_HAVE_OPENSSL)
    l_mutex_lock(&lnet_openssl_lock);
    if (!lnet_openssl_initialized) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        lnet_openssl_initialized = 1;
    }
    l_mutex_unlock(&lnet_openssl_lock);
#endif
    return 0;
}

/**
 * @brief 全局 TLS 清理
 */
void lnet_tls_cleanup(void) {
#if defined(LNET_HAVE_OPENSSL)
    /* OpenSSL cleanup is intentionally not done here to avoid issues
       with multiple init/cleanup cycles; lnet_global_cleanup handles it */
#endif
}

#if defined(LNET_PLATFORM_WINDOWS)
/* ==============================================================
 * Windows SChannel TLS 实现
 * ============================================================== */

/**
 * @brief 为 socket 开启 TLS 客户端模式
 * @param sock Socket 对象
 * @param config TLS 配置 (server_name 用于 SNI)
 * @return 0 成功，-1 失败
 */
int lnet_tls_connect(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls_init();

    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_CLIENT;
    tls->handshake_done = 0;

    SECURITY_STATUS secStatus;
    SCHANNEL_CRED schannelCred;
    memset(&schannelCred, 0, sizeof(schannelCred));
    schannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    schannelCred.grbitEnabledProtocols = SP_PROT_TLS1_0 | SP_PROT_TLS1_1 | SP_PROT_TLS1_2;
    schannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
    if (!config || config->verify_peer) {
        schannelCred.dwFlags |= SCH_CRED_AUTO_CRED_VALIDATION;
    }

    secStatus = AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, SECPKG_CRED_OUTBOUND,
        NULL, &schannelCred, NULL, NULL, &tls->cred_handle, NULL);
    if (secStatus != SEC_E_OK) {
        free(tls);
        return -1;
    }
    tls->cred_acquired = TRUE;

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_CLIENT;
    return 0;
}

/**
 * @brief 为已连接的 socket 开启 TLS 服务器模式
 * @param sock Socket 对象
 * @param config TLS 配置 (需要证书和私钥)
 * @return 0 成功，-1 失败
 */
int lnet_tls_accept(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls_init();

    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_SERVER;
    tls->handshake_done = 0;

    SECURITY_STATUS secStatus;
    SCHANNEL_CRED schannelCred;
    memset(&schannelCred, 0, sizeof(schannelCred));
    schannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    schannelCred.grbitEnabledProtocols = SP_PROT_TLS1_0 | SP_PROT_TLS1_1 | SP_PROT_TLS1_2;
    schannelCred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

    /* 如果提供了证书文件，加载证书 */
    if (config && config->cert_file) {
        /* 简化：跳过证书加载，仅使用自签名凭据 */
    }

    secStatus = AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, SECPKG_CRED_INBOUND,
        NULL, &schannelCred, NULL, NULL, &tls->cred_handle, NULL);
    if (secStatus != SEC_E_OK) {
        free(tls);
        return -1;
    }
    tls->cred_acquired = TRUE;

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_SERVER;
    return 0;
}

/**
 * @brief TLS 握手 (非阻塞，单步执行)
 * @param sock Socket 对象
 * @return 握手状态码
 */
lnet_tls_result_t lnet_tls_handshake(lnet_socket *sock) {
    if (!sock || !sock->tls) return LNET_TLS_ERROR;
    lnet_tls *tls = sock->tls;
    if (tls->handshake_done) return LNET_TLS_OK;

    SECURITY_STATUS secStatus;
    SecBufferDesc outBufferDesc, inBufferDesc;
    SecBuffer outBuffers[1], inBuffers[2];
    DWORD dwSSPIFlags, dwSSPIOutFlags;

    dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                  ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

    if (tls->mode == LNET_TLS_CLIENT) {
        outBufferDesc.ulVersion = SECBUFFER_VERSION;
        outBufferDesc.cBuffers = 1;
        outBufferDesc.pBuffers = outBuffers;
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[0].pvBuffer = NULL;
        outBuffers[0].cbBuffer = 0;

        /* 如果已有来自服务器的数据，放入输入缓冲区 */
        if (tls->in_data && tls->in_data_len > tls->in_data_offset) {
            inBufferDesc.ulVersion = SECBUFFER_VERSION;
            inBufferDesc.cBuffers = 2;
            inBufferDesc.pBuffers = inBuffers;
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[0].pvBuffer = tls->in_data + tls->in_data_offset;
            inBuffers[0].cbBuffer = (unsigned long)(tls->in_data_len - tls->in_data_offset);
            inBuffers[1].BufferType = SECBUFFER_EMPTY;
            inBuffers[1].pvBuffer = NULL;
            inBuffers[1].cbBuffer = 0;

            secStatus = InitializeSecurityContextW(&tls->cred_handle,
                tls->ctxt_established ? &tls->ctxt_handle : NULL,
                NULL, dwSSPIFlags, 0, SECURITY_NATIVE_DREP,
                tls->ctxt_established ? &inBufferDesc : NULL,
                0, &tls->ctxt_handle, &outBufferDesc, &dwSSPIOutFlags, NULL);
        } else {
            secStatus = InitializeSecurityContextW(&tls->cred_handle,
                tls->ctxt_established ? &tls->ctxt_handle : NULL,
                NULL, dwSSPIFlags, 0, SECURITY_NATIVE_DREP,
                NULL, 0, &tls->ctxt_handle, &outBufferDesc, &dwSSPIOutFlags, NULL);
        }
    } else {
        /* 服务器端握手 */
        if (!tls->in_data || tls->in_data_len <= tls->in_data_offset) {
            return LNET_TLS_WANT_READ;
        }

        outBufferDesc.ulVersion = SECBUFFER_VERSION;
        outBufferDesc.cBuffers = 1;
        outBufferDesc.pBuffers = outBuffers;
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBuffers[0].pvBuffer = NULL;
        outBuffers[0].cbBuffer = 0;

        inBufferDesc.ulVersion = SECBUFFER_VERSION;
        inBufferDesc.cBuffers = 2;
        inBufferDesc.pBuffers = inBuffers;
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].pvBuffer = tls->in_data + tls->in_data_offset;
        inBuffers[0].cbBuffer = (unsigned long)(tls->in_data_len - tls->in_data_offset);
        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        inBuffers[1].pvBuffer = NULL;
        inBuffers[1].cbBuffer = 0;

        DWORD dwSSPIOutFlags_srv = 0;
        secStatus = AcceptSecurityContext(&tls->cred_handle,
            tls->ctxt_established ? &tls->ctxt_handle : NULL,
            &inBufferDesc, ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
            ASC_REQ_CONFIDENTIALITY | ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
            SECURITY_NATIVE_DREP, &tls->ctxt_handle,
            &outBufferDesc, &dwSSPIOutFlags_srv, NULL);
    }

    if (outBuffers[0].pvBuffer && outBuffers[0].cbBuffer > 0) {
        /* 发送握手数据到对端 */
        int sent = lnet_socket_send(sock, outBuffers[0].pvBuffer, outBuffers[0].cbBuffer);
        FreeContextBuffer(outBuffers[0].pvBuffer);
        if (sent < 0) return LNET_TLS_ERROR;
    }

    /* 处理服务端返回的额外数据 */
    if (tls->mode == LNET_TLS_CLIENT &&
        secStatus == SEC_E_OK &&
        inBufferDesc.cBuffers == 2 &&
        inBuffers[1].BufferType == SECBUFFER_EXTRA) {
        /* 有额外数据，保存下来 */
        size_t extra = inBuffers[1].cbBuffer;
        if (extra > 0 && tls->in_data) {
            tls->in_data_offset = tls->in_data_len - extra;
        }
    }

    if (secStatus == SEC_E_OK || secStatus == SEC_I_CONTINUE_NEEDED) {
        tls->ctxt_established = TRUE;

        if (secStatus == SEC_E_OK) {
            /* 握手完成，获取流大小信息 */
            QueryContextAttributesW(&tls->ctxt_handle,
                SECPKG_ATTR_STREAM_SIZES, &tls->stream_sizes);
            tls->handshake_done = 1;
            /* 清理输入缓冲区 */
            free(tls->in_data);
            tls->in_data = NULL;
            tls->in_data_len = 0;
            tls->in_data_offset = 0;
            return LNET_TLS_OK;
        }
        /* 需要继续握手 */
        if (tls->mode == LNET_TLS_CLIENT && !(outBuffers[0].pvBuffer)) {
            return LNET_TLS_WANT_READ;
        }
        return LNET_TLS_WANT_READ;
    }

    if (secStatus == SEC_I_INCOMPLETE_CREDENTIALS) {
        return LNET_TLS_WANT_READ;
    }

    return LNET_TLS_ERROR;
}

/**
 * @brief 通过 TLS 加密写入数据
 * @param sock Socket 对象
 * @param data 明文数据
 * @param len 数据长度
 * @return 成功写入的字节数，-1 失败
 */
int lnet_tls_write(lnet_socket *sock, const void *data, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    lnet_tls *tls = sock->tls;

    size_t max_msg = tls->stream_sizes.cbMaximumMessage;
    size_t header_size = tls->stream_sizes.cbHeader;
    size_t trailer_size = tls->stream_sizes.cbTrailer;
    size_t total_sent = 0;
    const uint8_t *ptr = (const uint8_t *)data;

    while (total_sent < len) {
        size_t chunk = len - total_sent;
        if (chunk > max_msg) chunk = max_msg;

        size_t buf_size = header_size + chunk + trailer_size;
        uint8_t *send_buf = (uint8_t *)malloc(buf_size);
        if (!send_buf) return -1;

        SecBuffer msg_bufs[4];
        SecBufferDesc msg_desc;
        msg_desc.ulVersion = SECBUFFER_VERSION;
        msg_desc.cBuffers = 4;
        msg_desc.pBuffers = msg_bufs;

        msg_bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        msg_bufs[0].pvBuffer = send_buf;
        msg_bufs[0].cbBuffer = (unsigned long)header_size;

        msg_bufs[1].BufferType = SECBUFFER_DATA;
        msg_bufs[1].pvBuffer = send_buf + header_size;
        msg_bufs[1].cbBuffer = (unsigned long)chunk;
        memcpy(msg_bufs[1].pvBuffer, ptr + total_sent, chunk);

        msg_bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        msg_bufs[2].pvBuffer = send_buf + header_size + chunk;
        msg_bufs[2].cbBuffer = (unsigned long)trailer_size;

        msg_bufs[3].BufferType = SECBUFFER_EMPTY;
        msg_bufs[3].pvBuffer = NULL;
        msg_bufs[3].cbBuffer = 0;

        SECURITY_STATUS status = EncryptMessage(&tls->ctxt_handle, 0, &msg_desc, 0);
        if (status != SEC_E_OK) {
            free(send_buf);
            return -1;
        }

        size_t enc_len = msg_bufs[0].cbBuffer + msg_bufs[1].cbBuffer + msg_bufs[2].cbBuffer;
        int sent = lnet_socket_send(sock, send_buf, enc_len);
        free(send_buf);
        if (sent < 0) return -1;
        total_sent += chunk;
    }
    return (int)total_sent;
}

/**
 * @brief 通过 TLS 解密读取数据
 * @param sock Socket 对象
 * @param buf 输出缓冲区
 * @param len 缓冲区大小
 * @return 读取的明文字节数，0=关闭，-1=需要更多数据
 */
int lnet_tls_read(lnet_socket *sock, void *buf, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    lnet_tls *tls = sock->tls;

    /* 如果解密缓冲区还有数据，先返回 */
    if (tls->decrypt_buf && tls->decrypt_buf_len > tls->decrypt_buf_offset) {
        size_t avail = tls->decrypt_buf_len - tls->decrypt_buf_offset;
        size_t to_copy = (avail < len) ? avail : len;
        memcpy(buf, tls->decrypt_buf + tls->decrypt_buf_offset, to_copy);
        tls->decrypt_buf_offset += to_copy;
        if (tls->decrypt_buf_offset >= tls->decrypt_buf_len) {
            free(tls->decrypt_buf);
            tls->decrypt_buf = NULL;
            tls->decrypt_buf_len = 0;
            tls->decrypt_buf_offset = 0;
        }
        return (int)to_copy;
    }

    /* 从 socket 读取加密数据 */
    uint8_t raw[LNET_BUFFER_SIZE];
    int n = recv(sock->fd, raw, (int)sizeof(raw), 0);
    if (n <= 0) return n;

    uint8_t *dec_buf = (uint8_t *)malloc((size_t)n);
    if (!dec_buf) return -1;
    memcpy(dec_buf, raw, (size_t)n);

    SecBuffer msg_bufs[4];
    SecBufferDesc msg_desc;
    msg_desc.ulVersion = SECBUFFER_VERSION;
    msg_desc.cBuffers = 4;
    msg_desc.pBuffers = msg_bufs;

    msg_bufs[0].BufferType = SECBUFFER_DATA;
    msg_bufs[0].pvBuffer = dec_buf;
    msg_bufs[0].cbBuffer = (unsigned long)n;

    msg_bufs[1].BufferType = SECBUFFER_EMPTY;
    msg_bufs[1].pvBuffer = NULL;
    msg_bufs[1].cbBuffer = 0;
    msg_bufs[2].BufferType = SECBUFFER_EMPTY;
    msg_bufs[2].pvBuffer = NULL;
    msg_bufs[2].cbBuffer = 0;
    msg_bufs[3].BufferType = SECBUFFER_EMPTY;
    msg_bufs[3].pvBuffer = NULL;
    msg_bufs[3].cbBuffer = 0;

    SECURITY_STATUS status = DecryptMessage(&tls->ctxt_handle, &msg_desc, 0, NULL);

    if (status == SEC_E_OK) {
        /* 查找解密后的数据缓冲区 */
        for (int i = 0; i < 4; i++) {
            if (msg_bufs[i].BufferType == SECBUFFER_DATA && msg_bufs[i].pvBuffer && msg_bufs[i].cbBuffer > 0) {
                size_t dec_len = msg_bufs[i].cbBuffer;
                size_t to_copy = (dec_len < len) ? dec_len : len;
                memcpy(buf, msg_bufs[i].pvBuffer, to_copy);
                if (dec_len > to_copy) {
                    tls->decrypt_buf = (uint8_t *)malloc(dec_len - to_copy);
                    if (tls->decrypt_buf) {
                        memcpy(tls->decrypt_buf, (uint8_t*)msg_bufs[i].pvBuffer + to_copy, dec_len - to_copy);
                        tls->decrypt_buf_len = dec_len - to_copy;
                        tls->decrypt_buf_offset = 0;
                    }
                }
                free(dec_buf);
                return (int)to_copy;
            }
        }
    }

    if (status == SEC_I_CONTEXT_EXPIRED) {
        free(dec_buf);
        return 0;
    }

    free(dec_buf);
    return -1;
}

/**
 * @brief 关闭 TLS 连接
 * @param sock Socket 对象
 */
void lnet_tls_shutdown(lnet_socket *sock) {
    if (!sock || !sock->tls) return;
    lnet_tls *tls = sock->tls;

    if (tls->ctxt_established) {
        /* 发送关闭通知 */
        SecBufferDesc outBufDesc;
        SecBuffer outBuf;
        DWORD dwType = SCHANNEL_SHUTDOWN;
        outBufDesc.ulVersion = SECBUFFER_VERSION;
        outBufDesc.cBuffers = 1;
        outBufDesc.pBuffers = &outBuf;
        outBuf.BufferType = SECBUFFER_TOKEN;
        outBuf.pvBuffer = &dwType;
        outBuf.cbBuffer = sizeof(dwType);
        DWORD dwSSPIFlags;
        InitializeSecurityContextW(&tls->cred_handle, &tls->ctxt_handle,
            NULL, 0, 0, SECURITY_NATIVE_DREP, NULL, 0, &tls->ctxt_handle,
            &outBufDesc, &dwSSPIFlags, NULL);
        DeleteSecurityContext(&tls->ctxt_handle);
    }
    if (tls->cred_acquired) {
        FreeCredentialsHandle(&tls->cred_handle);
    }
    free(tls->in_data);
    free(tls->decrypt_buf);
    free(tls);
    sock->tls = NULL;
    sock->tls_mode = LNET_TLS_NONE;
}

#elif defined(LNET_HAVE_OPENSSL)
/* ==============================================================
 * OpenSSL TLS 实现 (Linux/Android)
 * ============================================================== */

/**
 * @brief 为 socket 开启 TLS 客户端模式 (OpenSSL)
 * @param sock Socket 对象
 * @param config TLS 配置
 * @return 0 成功，-1 失败
 */
int lnet_tls_connect(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls_init();

    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_CLIENT;
    tls->handshake_done = 0;

    tls->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls->ssl_ctx) { free(tls); return -1; }

    SSL_CTX_set_options(tls->ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

    if (config && config->verify_peer) {
        SSL_CTX_set_default_verify_paths(tls->ssl_ctx);
        SSL_CTX_set_verify(tls->ssl_ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(tls->ssl_ctx, SSL_VERIFY_NONE, NULL);
    }

    tls->ssl = SSL_new(tls->ssl_ctx);
    if (!tls->ssl) {
        SSL_CTX_free(tls->ssl_ctx);
        free(tls);
        return -1;
    }

    SSL_set_fd(tls->ssl, (int)sock->fd);
    if (config && config->server_name) {
        SSL_set_tlsext_host_name(tls->ssl, config->server_name);
    }

    int ret = SSL_connect(tls->ssl);
    if (ret != 1) {
        int err = SSL_get_error(tls->ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            SSL_free(tls->ssl);
            SSL_CTX_free(tls->ssl_ctx);
            free(tls);
            return -1;
        }
    } else {
        tls->handshake_done = 1;
    }

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_CLIENT;
    return 0;
}

/**
 * @brief 为已连接的 socket 开启 TLS 服务器模式 (OpenSSL)
 * @param sock Socket 对象
 * @param config TLS 配置 (需要证书和私钥)
 * @return 0 成功，-1 失败
 */
int lnet_tls_accept(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls_init();

    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_SERVER;
    tls->handshake_done = 0;

    tls->ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!tls->ssl_ctx) { free(tls); return -1; }

    SSL_CTX_set_options(tls->ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

    if (config && config->cert_file) {
        SSL_CTX_use_certificate_file(tls->ssl_ctx, config->cert_file, SSL_FILETYPE_PEM);
    }
    if (config && config->key_file) {
        SSL_CTX_use_PrivateKey_file(tls->ssl_ctx, config->key_file, SSL_FILETYPE_PEM);
    }

    tls->ssl = SSL_new(tls->ssl_ctx);
    if (!tls->ssl) {
        SSL_CTX_free(tls->ssl_ctx);
        free(tls);
        return -1;
    }

    SSL_set_fd(tls->ssl, (int)sock->fd);

    int ret = SSL_accept(tls->ssl);
    if (ret != 1) {
        int err = SSL_get_error(tls->ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            SSL_free(tls->ssl);
            SSL_CTX_free(tls->ssl_ctx);
            free(tls);
            return -1;
        }
    } else {
        tls->handshake_done = 1;
    }

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_SERVER;
    return 0;
}

/**
 * @brief TLS 握手 (OpenSSL 非阻塞)
 * @param sock Socket 对象
 * @return 握手状态码
 */
lnet_tls_result_t lnet_tls_handshake(lnet_socket *sock) {
    if (!sock || !sock->tls) return LNET_TLS_ERROR;
    lnet_tls *tls = sock->tls;
    if (tls->handshake_done) return LNET_TLS_OK;

    int ret;
    if (tls->mode == LNET_TLS_CLIENT)
        ret = SSL_connect(tls->ssl);
    else
        ret = SSL_accept(tls->ssl);

    if (ret == 1) {
        tls->handshake_done = 1;
        return LNET_TLS_OK;
    }

    int err = SSL_get_error(tls->ssl, ret);
    if (err == SSL_ERROR_WANT_READ) return LNET_TLS_WANT_READ;
    if (err == SSL_ERROR_WANT_WRITE) return LNET_TLS_WANT_WRITE;
    if (err == SSL_ERROR_ZERO_RETURN) return LNET_TLS_CLOSED;
    return LNET_TLS_ERROR;
}

/**
 * @brief 通过 TLS 加密写入 (OpenSSL)
 * @param sock Socket 对象
 * @param data 数据
 * @param len 长度
 * @return 写入字节数，-1 失败
 */
int lnet_tls_write(lnet_socket *sock, const void *data, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    int n = SSL_write(sock->tls->ssl, data, (int)len);
    if (n <= 0) {
        int err = SSL_get_error(sock->tls->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        return -1;
    }
    return n;
}

/**
 * @brief 通过 TLS 解密读取 (OpenSSL)
 * @param sock Socket 对象
 * @param buf 缓冲区
 * @param len 长度
 * @return 读取字节数，0=关闭，-1=需要更多数据
 */
int lnet_tls_read(lnet_socket *sock, void *buf, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    int n = SSL_read(sock->tls->ssl, buf, (int)len);
    if (n <= 0) {
        int err = SSL_get_error(sock->tls->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return -1;
        if (err == SSL_ERROR_ZERO_RETURN) return 0;
        return -1;
    }
    return n;
}

/**
 * @brief 关闭 TLS 连接 (OpenSSL)
 * @param sock Socket 对象
 */
void lnet_tls_shutdown(lnet_socket *sock) {
    if (!sock || !sock->tls) return;
    lnet_tls *tls = sock->tls;
    if (tls->ssl) {
        SSL_shutdown(tls->ssl);
        SSL_free(tls->ssl);
    }
    if (tls->ssl_ctx) {
        SSL_CTX_free(tls->ssl_ctx);
    }
    free(tls);
    sock->tls = NULL;
    sock->tls_mode = LNET_TLS_NONE;
}

#elif defined(LNET_PLATFORM_MACOS) || defined(LNET_PLATFORM_IOS)
/* ==============================================================
 * macOS/iOS SecureTransport TLS 实现
 * ============================================================== */

static OSStatus lnet_securetransport_read(SSLConnectionRef conn, void *data, size_t *dataLen) {
    lnet_socket *sock = (lnet_socket *)conn;
    int n = recv(sock->fd, data, (int)*dataLen, 0);
    if (n < 0) { *dataLen = 0; return errSecIO; }
    if (n == 0) { *dataLen = 0; return errSSLClosedGraceful; }
    *dataLen = (size_t)n;
    return noErr;
}

static OSStatus lnet_securetransport_write(SSLConnectionRef conn, const void *data, size_t *dataLen) {
    lnet_socket *sock = (lnet_socket *)conn;
    int n = send(sock->fd, data, (int)*dataLen, 0);
    if (n < 0) { *dataLen = 0; return errSecIO; }
    *dataLen = (size_t)n;
    return noErr;
}

int lnet_tls_connect(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_CLIENT;

    tls->ssl_ctx = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!tls->ssl_ctx) { free(tls); return -1; }

    SSLSetIOFuncs(tls->ssl_ctx, lnet_securetransport_read, lnet_securetransport_write);
    SSLSetConnection(tls->ssl_ctx, sock);

    if (config && config->server_name) {
        SSLSetPeerDomainName(tls->ssl_ctx, config->server_name, strlen(config->server_name));
    }

    OSStatus status = SSLHandshake(tls->ssl_ctx);
    if (status == noErr) {
        tls->handshake_done = 1;
    } else if (status != errSSLWouldBlock) {
        CFRelease(tls->ssl_ctx);
        free(tls);
        return -1;
    }

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_CLIENT;
    return 0;
}

int lnet_tls_accept(lnet_socket *sock, const lnet_tls_config *config) {
    if (!sock) return -1;
    lnet_tls *tls = (lnet_tls *)calloc(1, sizeof(lnet_tls));
    if (!tls) return -1;
    tls->mode = LNET_TLS_SERVER;

    tls->ssl_ctx = SSLCreateContext(NULL, kSSLServerSide, kSSLStreamType);
    if (!tls->ssl_ctx) { free(tls); return -1; }

    SSLSetIOFuncs(tls->ssl_ctx, lnet_securetransport_read, lnet_securetransport_write);
    SSLSetConnection(tls->ssl_ctx, sock);

    OSStatus status = SSLHandshake(tls->ssl_ctx);
    if (status == noErr) {
        tls->handshake_done = 1;
    } else if (status != errSSLWouldBlock) {
        CFRelease(tls->ssl_ctx);
        free(tls);
        return -1;
    }

    sock->tls = tls;
    sock->tls_mode = LNET_TLS_SERVER;
    return 0;
}

lnet_tls_result_t lnet_tls_handshake(lnet_socket *sock) {
    if (!sock || !sock->tls) return LNET_TLS_ERROR;
    lnet_tls *tls = sock->tls;
    if (tls->handshake_done) return LNET_TLS_OK;

    OSStatus status = SSLHandshake(tls->ssl_ctx);
    if (status == noErr) { tls->handshake_done = 1; return LNET_TLS_OK; }
    if (status == errSSLWouldBlock) return LNET_TLS_WANT_READ;
    if (status == errSSLClosedGraceful || status == errSSLClosedAbort) return LNET_TLS_CLOSED;
    return LNET_TLS_ERROR;
}

int lnet_tls_write(lnet_socket *sock, const void *data, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    size_t processed = 0;
    OSStatus status = SSLWrite(sock->tls->ssl_ctx, data, len, &processed);
    if (status == noErr) return (int)processed;
    if (status == errSSLWouldBlock) return 0;
    return -1;
}

int lnet_tls_read(lnet_socket *sock, void *buf, size_t len) {
    if (!sock || !sock->tls || !sock->tls->handshake_done) return -1;
    size_t processed = 0;
    OSStatus status = SSLRead(sock->tls->ssl_ctx, buf, len, &processed);
    if (status == noErr) return (int)processed;
    if (status == errSSLWouldBlock) return -1;
    if (status == errSSLClosedGraceful || status == errSSLClosedAbort) return 0;
    return -1;
}

void lnet_tls_shutdown(lnet_socket *sock) {
    if (!sock || !sock->tls) return;
    lnet_tls *tls = sock->tls;
    if (tls->ssl_ctx) {
        SSLClose(tls->ssl_ctx);
        CFRelease(tls->ssl_ctx);
    }
    free(tls);
    sock->tls = NULL;
    sock->tls_mode = LNET_TLS_NONE;
}

#else
/* ==============================================================
 * 其他平台 TLS 存根 (WASM 等)
 * ============================================================== */

int lnet_tls_connect(lnet_socket *sock, const lnet_tls_config *config) {
    (void)sock; (void)config;
    return -1; /* TLS not supported on this platform */
}

int lnet_tls_accept(lnet_socket *sock, const lnet_tls_config *config) {
    (void)sock; (void)config;
    return -1; /* TLS not supported on this platform */
}

lnet_tls_result_t lnet_tls_handshake(lnet_socket *sock) {
    (void)sock;
    return LNET_TLS_ERROR;
}

int lnet_tls_write(lnet_socket *sock, const void *data, size_t len) {
    (void)sock; (void)data; (void)len;
    return -1;
}

int lnet_tls_read(lnet_socket *sock, void *buf, size_t len) {
    (void)sock; (void)buf; (void)len;
    return -1;
}

void lnet_tls_shutdown(lnet_socket *sock) {
    if (!sock || !sock->tls) return;
    free(sock->tls);
    sock->tls = NULL;
    sock->tls_mode = LNET_TLS_NONE;
}
#endif

/* ========================================================================
 * HTTP 客户端
 * ======================================================================== */

/* 默认 HTTP 客户端配置 */
static const lnet_http_client_config lnet_http_default_config = {
    10000,  /* connect_timeout_ms */
    30000,  /* recv_timeout_ms */
    30000,  /* send_timeout_ms */
    5,      /* max_redirects */
    1,      /* follow_redirects */
    1,      /* verify_ssl */
    0,      /* keepalive */
    {NULL, NULL, NULL, 0, NULL}, /* tls_config */
    NULL,   /* proxy_host */
    0,      /* proxy_port */
    NULL,   /* user_agent */
    NULL,   /* extra_headers */
    0       /* extra_header_count */
};

/**
 * @brief 创建默认 HTTP 客户端配置
 * @param config 输出配置结构体
 */
void lnet_http_client_config_default(lnet_http_client_config *config) {
    if (config) memcpy(config, &lnet_http_default_config, sizeof(lnet_http_client_config));
}

/**
 * @brief 向客户端配置添加额外的请求头
 * @param config 客户端配置
 * @param name 请求头名称
 * @param value 请求头值
 * @return 0 成功，-1 头数量超限
 */
int lnet_http_client_add_header(lnet_http_client_config *config,
                                 const char *name, const char *value) {
    if (!config || !name || !value) return -1;
    if (config->extra_header_count >= LNET_MAX_HEADERS) return -1;

    lnet_http_header *new_hdrs = (lnet_http_header *)realloc(
        config->extra_headers,
        (size_t)(config->extra_header_count + 1) * sizeof(lnet_http_header));
    if (!new_hdrs) return -1;
    config->extra_headers = new_hdrs;

    config->extra_headers[config->extra_header_count].name = strdup(name);
    config->extra_headers[config->extra_header_count].value = strdup(value);
    config->extra_header_count++;
    return 0;
}

/**
 * @brief 生成随机边界字符串 (用于 multipart/form-data)
 * @param buf 输出缓冲区
 * @param len 缓冲区大小
 */
static void lnet_random_boundary(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    srand((unsigned int)time(NULL));
    for (i = 0; i < len - 1; i++) {
        buf[i] = hex[rand() % 16];
    }
    buf[len - 1] = '\0';
}

/**
 * @brief 解析 HTTP 响应状态行
 * @param line 状态行字符串
 * @param status_code 输出状态码
 * @param status_msg 输出状态文本
 * @param msg_size 状态文本缓冲区大小
 * @return 0 成功，-1 格式错误
 */
static int lnet_parse_status_line(const char *line, int *status_code,
                                   char *status_msg, size_t msg_size) {
    /* 格式: "HTTP/x.y CODE MESSAGE" */
    const char *p = line;
    while (*p && *p != ' ') p++;
    if (!*p) return -1;
    p++; /* 跳过空格 */
    *status_code = atoi(p);
    while (*p && *p != ' ') p++;
    if (*p) {
        p++;
        if (status_msg) {
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i < msg_size - 1) {
                status_msg[i++] = *p++;
            }
            status_msg[i] = '\0';
        }
    }
    return 0;
}

/**
 * @brief 连接到服务器 (含 TLS)
 * @param url URL 对象
 * @param config 客户端配置
 * @param sock_out 输出 socket
 * @return 0 成功，-1 失败
 */
static int lnet_http_connect(const lnet_url_parts *url,
                              const lnet_http_client_config *config,
                              lnet_socket **sock_out) {
    int port = url->port ? url->port : (strcmp(url->scheme, "https") == 0 ? 443 : 80);
    int is_ssl = (strcmp(url->scheme, "https") == 0) ? 1 : 0;

    lnet_socket *sock = lnet_socket_new(LNET_AF_INET, LNET_SOCK_STREAM, 0);
    if (!sock) return -1;

    if (lnet_socket_connect(sock, url->host, port) != 0) {
        lnet_socket_free(sock);
        return -1;
    }

    if (is_ssl) {
        lnet_tls_config tls_cfg;
        memset(&tls_cfg, 0, sizeof(tls_cfg));
        if (config && config->tls_config.server_name)
            tls_cfg.server_name = config->tls_config.server_name;
        else
            tls_cfg.server_name = url->host;
        tls_cfg.verify_peer = config ? config->verify_ssl : 1;

        if (lnet_tls_connect(sock, &tls_cfg) != 0) {
            lnet_socket_free(sock);
            return -1;
        }

        /* 轮询握手 */
        int max_attempts = 100;
        while (max_attempts-- > 0) {
            lnet_tls_result_t r = lnet_tls_handshake(sock);
            if (r == LNET_TLS_OK) break;
            if (r == LNET_TLS_ERROR) {
                lnet_socket_free(sock);
                return -1;
            }
#if defined(LNET_PLATFORM_WINDOWS)
            Sleep(10);
#else
            usleep(10000);
#endif
        }
        if (max_attempts <= 0) {
            lnet_socket_free(sock);
            return -1;
        }
    }

    *sock_out = sock;
    return 0;
}

/**
 * @brief 读取 HTTP 响应头 (直到有空行)
 * @param sock Socket
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param body_offset 输出 body 起始偏移
 * @return 0 成功，-1 失败
 */
static int lnet_read_http_headers(lnet_socket *sock, char *buf, size_t buf_size,
                                   size_t *body_offset) {
    size_t total = 0;
    while (total < buf_size - 1) {
        int n = lnet_socket_recv(sock, buf + total, buf_size - total - 1);
        if (n <= 0) return -1;
        total += (size_t)n;
        buf[total] = '\0';

        /* 查找 \r\n\r\n 或 \n\n */
        const char *end = strstr(buf, "\r\n\r\n");
        if (end) {
            *body_offset = (size_t)(end - buf) + 4;
            return 0;
        }
        end = strstr(buf, "\n\n");
        if (end) {
            *body_offset = (size_t)(end - buf) + 2;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 解析 HTTP 响应头，构建结果对象
 * @param raw 原始响应头数据
 * @param raw_len 数据长度
 * @param result 输出结果对象
 * @return 0 成功，-1 失败
 */
static int lnet_parse_response_headers(const char *raw, size_t raw_len,
                                        lnet_http_client_result *result) {
    const char *p = raw;
    const char *end = raw + raw_len;

    /* 解析状态行 */
    const char *line_end = (const char *)memchr(p, '\n', (size_t)(end - p));
    if (!line_end) return -1;
    char status_line[256];
    size_t slen = (size_t)(line_end - p);
    if (slen > 0 && p[slen - 1] == '\r') slen--;
    if (slen >= sizeof(status_line)) slen = sizeof(status_line) - 1;
    memcpy(status_line, p, slen);
    status_line[slen] = '\0';

    lnet_parse_status_line(status_line, &result->status_code, NULL, 0);
    p = line_end + 1;

    /* 解析响应头 */
    int hdr_cap = 16;
    result->headers = (lnet_http_header *)malloc((size_t)hdr_cap * sizeof(lnet_http_header));
    if (!result->headers) return -1;
    result->header_count = 0;

    while (p < end) {
        /* 检查是否到空行 */
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            break;
        }

        /* 解析单个头 */
        line_end = (const char *)memchr(p, '\n', (size_t)(end - p));
        if (!line_end) break;

        const char *colon = (const char *)memchr(p, ':', (size_t)(line_end - p));
        if (colon) {
            size_t name_len = (size_t)(colon - p);
            const char *val_start = colon + 1;
            while (val_start < line_end && (*val_start == ' ' || *val_start == '\t'))
                val_start++;
            size_t val_len = (size_t)(line_end - val_start);
            if (val_len > 0 && val_start[val_len - 1] == '\r') val_len--;

            if (result->header_count >= hdr_cap) {
                hdr_cap *= 2;
                lnet_http_header *new_hdrs = (lnet_http_header *)realloc(
                    result->headers, (size_t)hdr_cap * sizeof(lnet_http_header));
                if (!new_hdrs) return -1;
                result->headers = new_hdrs;
            }

            result->headers[result->header_count].name =
                (char *)malloc(name_len + 1);
            result->headers[result->header_count].value =
                (char *)malloc(val_len + 1);
            if (result->headers[result->header_count].name &&
                result->headers[result->header_count].value) {
                memcpy(result->headers[result->header_count].name, p, name_len);
                result->headers[result->header_count].name[name_len] = '\0';
                memcpy(result->headers[result->header_count].value, val_start, val_len);
                result->headers[result->header_count].value[val_len] = '\0';
                result->header_count++;
            }
        }

        p = line_end + 1;
    }

    return 0;
}

/**
 * @brief 获取响应头值 (客户端结果)
 * @param result 客户端结果
 * @param name 头名称
 * @return 头值或 NULL
 */
static const char *lnet_client_get_header(const lnet_http_client_result *result,
                                           const char *name) {
    if (!result || !name) return NULL;
    for (int i = 0; i < result->header_count; i++) {
        if (lnet_strcasecmp(result->headers[i].name, name) == 0)
            return result->headers[i].value;
    }
    return NULL;
}

/**
 * @brief 读取 chunked 编码的响应体
 * @param sock Socket
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 实际读取的 body 长度，-1 失败
 */
static ssize_t lnet_read_chunked_body(lnet_socket *sock, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total < buf_size) {
        /* 读取 chunk 大小行 */
        char line[32];
        int r = lnet_socket_readline(sock, line, sizeof(line));
        if (r < 0) break;

        long chunk_size = strtol(line, NULL, 16);
        if (chunk_size == 0) {
            /* 最终 chunk，读取尾部 */
            lnet_socket_readline(sock, line, sizeof(line)); /* 可能的尾部 header */
            break;
        }
        if (chunk_size < 0 || (size_t)chunk_size > buf_size - total)
            break;

        /* 读取 chunk 数据 */
        size_t remaining = (size_t)chunk_size;
        while (remaining > 0) {
            int n = lnet_socket_recv(sock, buf + total, remaining);
            if (n <= 0) return (ssize_t)total;
            total += n;
            remaining -= (size_t)n;
        }
        /* 读取 chunk 尾部的 \r\n */
        lnet_socket_readline(sock, line, sizeof(line));
    }
    return (ssize_t)total;
}

/**
 * @brief 读取固定长度的响应体
 * @param sock Socket
 * @param buf 输出缓冲区
 * @param content_length 期望长度
 * @return 实际读取长度，-1 失败
 */
static ssize_t lnet_read_fixed_body(lnet_socket *sock, char *buf, size_t content_length) {
    size_t total = 0;
    while (total < content_length) {
        int n = lnet_socket_recv(sock, buf + total, content_length - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

/**
 * @brief 执行 HTTP 请求
 * @param method HTTP 方法 (GET/POST/PUT/DELETE/PATCH/HEAD/OPTIONS)
 * @param url 请求 URL
 * @param body 请求体 (可为 NULL)
 * @param body_len 请求体长度
 * @param config 客户端配置 (可为 NULL 使用默认值)
 * @return 请求结果对象，调用者负责通过 lnet_http_client_result_free 释放
 */
lnet_http_client_result *lnet_http_client_request(const char *method, const char *url,
                                            const char *body, size_t body_len,
                                            const lnet_http_client_config *config) {
    if (!method || !url) return NULL;

    lnet_http_client_config cfg;
    if (config)
        memcpy(&cfg, config, sizeof(cfg));
    else
        memcpy(&cfg, &lnet_http_default_config, sizeof(cfg));

    lnet_http_client_result *result = (lnet_http_client_result *)calloc(1, sizeof(lnet_http_client_result));
    if (!result) return NULL;

    int redirect_count = 0;
    const char *current_url = url;
    char redirect_buf[LNET_MAX_URL_LEN];

    while (1) {
        lnet_url_parts *url_parts = lnet_url_parse(current_url);
        if (!url_parts) {
            result->error = strdup("Failed to parse URL");
            return result;
        }

        /* 修正端口 */
        if (url_parts->port == 0) {
            url_parts->port = (strcmp(url_parts->scheme, "https") == 0) ? 443 : 80;
        }

        lnet_socket *sock = NULL;
        if (lnet_http_connect(url_parts, &cfg, &sock) != 0) {
            lnet_url_parts_free(url_parts);
            result->error = strdup("Connection failed");
            return result;
        }

        /* 构建 HTTP 请求 */
        char req_buf[LNET_BUFFER_SIZE];
        int use_chunked = 0;
        int req_pos = snprintf(req_buf, sizeof(req_buf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s",
            method,
            url_parts->path ? url_parts->path : "/",
            url_parts->host);

        if (url_parts->port != 80 && url_parts->port != 443) {
            req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
                ":%d", url_parts->port);
        }
        req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
            "\r\n");

        /* User-Agent */
        req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
            "User-Agent: %s\r\n",
            cfg.user_agent ? cfg.user_agent : "LXCLUA-HTTP/1.0");

        /* Accept */
        req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
            "Accept: */*\r\n");

        /* Connection */
        req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
            "Connection: %s\r\n",
            cfg.keepalive ? "keep-alive" : "close");

        /* 请求体 */
        if (body && body_len > 0) {
            if (body_len > 65536 || !body) {
                use_chunked = 1;
                req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
                    "Transfer-Encoding: chunked\r\n");
            } else {
                req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
                    "Content-Length: %zu\r\n", body_len);
            }
        }

        /* 额外请求头 */
        for (int i = 0; i < cfg.extra_header_count; i++) {
            req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos,
                "%s: %s\r\n",
                cfg.extra_headers[i].name, cfg.extra_headers[i].value);
        }

        req_pos += snprintf(req_buf + req_pos, sizeof(req_buf) - (size_t)req_pos, "\r\n");

        /* 发送请求头 */
        if (lnet_socket_send(sock, req_buf, (size_t)req_pos) < 0) {
            lnet_socket_free(sock);
            lnet_url_parts_free(url_parts);
            result->error = strdup("Failed to send request");
            return result;
        }

        /* 发送请求体 */
        if (body && body_len > 0) {
            if (use_chunked) {
                size_t sent = 0;
                while (sent < body_len) {
                    size_t chunk = body_len - sent;
                    if (chunk > 8192) chunk = 8192;
                    char chunk_hdr[32];
                    int hdr_len = snprintf(chunk_hdr, sizeof(chunk_hdr), "%zx\r\n", chunk);
                    lnet_socket_send(sock, chunk_hdr, (size_t)hdr_len);
                    lnet_socket_send(sock, body + sent, chunk);
                    lnet_socket_send(sock, "\r\n", 2);
                    sent += chunk;
                }
                lnet_socket_send(sock, "0\r\n\r\n", 5);
            } else {
                lnet_socket_send(sock, body, body_len);
            }
        }

        /* 读取响应 */
        char resp_buf[LNET_MAX_HEADER_LEN];
        size_t body_offset = 0;
        if (lnet_read_http_headers(sock, resp_buf, sizeof(resp_buf), &body_offset) != 0) {
            lnet_socket_free(sock);
            lnet_url_parts_free(url_parts);
            result->error = strdup("Failed to read response headers");
            return result;
        }

        /* 解析响应头 */
        if (lnet_parse_response_headers(resp_buf, body_offset, result) != 0) {
            lnet_socket_free(sock);
            lnet_url_parts_free(url_parts);
            result->error = strdup("Failed to parse response headers");
            return result;
        }

        /* 处理重定向 */
        if (cfg.follow_redirects && redirect_count < cfg.max_redirects &&
            (result->status_code == 301 || result->status_code == 302 ||
             result->status_code == 303 || result->status_code == 307 ||
             result->status_code == 308)) {
            const char *location = lnet_client_get_header(result, "location");
            if (location && location[0]) {
                strncpy(redirect_buf, location, sizeof(redirect_buf) - 1);
                redirect_buf[sizeof(redirect_buf) - 1] = '\0';
                current_url = redirect_buf;

                /* 释放本轮结果数据 */
                lnet_http_client_result_free(result);
                memset(result, 0, sizeof(*result));
                lnet_socket_free(sock);
                lnet_url_parts_free(url_parts);
                redirect_count++;
                continue;
            }
        }

        /* 读取响应体 */
        const char *transfer_encoding = lnet_client_get_header(result, "transfer-encoding");
        const char *content_length_str = lnet_client_get_header(result, "content-length");

        if (transfer_encoding && strstr(transfer_encoding, "chunked")) {
            char *body_buf = (char *)malloc(LNET_MAX_BODY_CHUNK);
            if (body_buf) {
                ssize_t blen = lnet_read_chunked_body(sock, body_buf, LNET_MAX_BODY_CHUNK);
                if (blen > 0) {
                    result->body = body_buf;
                    result->body_len = (size_t)blen;
                } else {
                    free(body_buf);
                }
            }
        } else if (content_length_str) {
            size_t clen = (size_t)atol(content_length_str);
            if (clen > 0 && clen < LNET_MAX_BODY_CHUNK) {
                char *body_buf = (char *)malloc(clen + 1);
                if (body_buf) {
                    ssize_t blen = lnet_read_fixed_body(sock, body_buf, clen);
                    if (blen > 0) {
                        body_buf[blen] = '\0';
                        result->body = body_buf;
                        result->body_len = (size_t)blen;
                    } else {
                        free(body_buf);
                    }
                }
            }
        } else if (!content_length_str) {
            /* 无 Content-Length，读取到连接关闭 */
            char *body_buf = (char *)malloc(LNET_MAX_BODY_CHUNK);
            if (body_buf) {
                size_t btotal = 0;
                while (btotal < LNET_MAX_BODY_CHUNK - 1) {
                    int n = lnet_socket_recv(sock, body_buf + btotal,
                        LNET_MAX_BODY_CHUNK - btotal - 1);
                    if (n <= 0) break;
                    btotal += (size_t)n;
                }
                if (btotal > 0) {
                    body_buf[btotal] = '\0';
                    result->body = body_buf;
                    result->body_len = btotal;
                } else {
                    free(body_buf);
                }
            }
        }

        lnet_socket_free(sock);
        lnet_url_parts_free(url_parts);
        break;
    }

    return result;
}

/**
 * @brief 释放 HTTP 客户端请求结果
 * @param result 请求结果对象
 */
void lnet_http_client_result_free(lnet_http_client_result *result) {
    if (!result) return;
    free(result->status_message);
    if (result->headers) {
        for (int i = 0; i < result->header_count; i++) {
            free(result->headers[i].name);
            free(result->headers[i].value);
        }
        free(result->headers);
    }
    free(result->body);
    free(result->redirect_url);
    free(result->error);
    free(result);
}

/* ========================================================================
 * HTTP 请求/响应 (服务器端)
 * ======================================================================== */

/**
 * @brief 获取服务器端请求头值
 * @param req HTTP 请求对象
 * @param name 请求头名称 (不区分大小写)
 * @return 头值，未找到返回 NULL
 */
const char *lnet_http_request_get_header(lnet_http_request *req, const char *name) {
    if (!req || !name) return NULL;
    for (int i = 0; i < req->header_count; i++) {
        if (lnet_strcasecmp(req->headers[i].name, name) == 0)
            return req->headers[i].value;
    }
    return NULL;
}

/**
 * @brief 从 URL 查询字符串中获取参数值
 * @param req HTTP 请求对象
 * @param key 参数名
 * @param value 输出参数值指针 (可为 NULL，仅检查是否存在)
 * @return 0 找到，-1 未找到
 */
int lnet_http_request_get_query(lnet_http_request *req, const char *key, const char **value) {
    if (!req || !key) return -1;
    if (!req->query_string) return -1;

    size_t key_len = strlen(key);
    const char *q = req->query_string;
    while (*q) {
        /* 查找键 */
        if (lnet_strncasecmp(q, key, key_len) == 0 &&
            (q[key_len] == '=' || q[key_len] == '&' || q[key_len] == '\0')) {
            if (q[key_len] == '=') {
                const char *val_start = q + key_len + 1;
                size_t val_len = 0;
                while (val_start[val_len] && val_start[val_len] != '&') val_len++;
                if (value) {
                    /* 返回解码后的值 */
                    char *decoded = lnet_url_decode(val_start, val_len);
                    *value = decoded;
                    return 0;
                }
                return 0;
            }
            if (value) *value = "";
            return 0;
        }
        /* 跳到下一个参数 */
        while (*q && *q != '&') q++;
        if (*q == '&') q++;
    }
    return -1;
}

/**
 * @brief 释放 HTTP 请求对象
 * @param req 请求对象
 */
void lnet_http_request_free(lnet_http_request *req) {
    if (!req) return;
    for (int i = 0; i < req->header_count; i++) {
        free(req->headers[i].name);
        free(req->headers[i].value);
    }
    free(req->path);
    free(req->query_string);
    free(req->fragment);
    if (req->body_owned) free(req->body);
    free(req);
}

/**
 * @brief 初始化 HTTP 响应对象
 * @param res 响应对象 (已分配内存)
 * @param sock 关联的 socket
 */
void lnet_http_response_init(lnet_http_response *res, lnet_socket *sock) {
    if (!res) return;
    memset(res, 0, sizeof(*res));
    res->status_code = 200;
    res->http_major = 1;
    res->http_minor = 1;
    res->sock = sock;
    res->body = (char *)malloc(4096);
    if (res->body) {
        res->body_capacity = 4096;
        res->body_len = 0;
        res->body[0] = '\0';
    }
}

/**
 * @brief 设置响应头
 * @param res 响应对象
 * @param name 头名称
 * @param value 头值
 */
void lnet_http_response_set_header(lnet_http_response *res,
                                    const char *name, const char *value) {
    if (!res || !name || !value) return;
    if (res->header_count >= LNET_MAX_HEADERS) return;

    /* 检查是否已存在同名头，存在则替换 */
    for (int i = 0; i < res->header_count; i++) {
        if (lnet_strcasecmp(res->headers[i].name, name) == 0) {
            free(res->headers[i].value);
            res->headers[i].value = strdup(value);
            return;
        }
    }

    res->headers[res->header_count].name = strdup(name);
    res->headers[res->header_count].value = strdup(value);
    res->header_count++;
}

/**
 * @brief 获取响应头值
 * @param res 响应对象
 * @param name 头名称
 * @return 头值，未找到返回 NULL
 */
const char *lnet_http_response_get_header(lnet_http_response *res, const char *name) {
    if (!res || !name) return NULL;
    for (int i = 0; i < res->header_count; i++) {
        if (lnet_strcasecmp(res->headers[i].name, name) == 0)
            return res->headers[i].value;
    }
    return NULL;
}

/**
 * @brief 设置 Cookie
 * @param res 响应对象
 * @param cookie Cookie 对象
 */
void lnet_http_response_set_cookie(lnet_http_response *res, const lnet_cookie *cookie) {
    if (!res || !cookie) return;
    char *set_str = lnet_cookie_build_set(cookie);
    if (!set_str) return;
    /* 保存为 Set-Cookie 字符串 */
    res->set_cookies = (char **)realloc(res->set_cookies,
        (size_t)(res->set_cookie_count + 1) * sizeof(char *));
    if (res->set_cookies) {
        res->set_cookies[res->set_cookie_count] = set_str;
        res->set_cookie_count++;
    } else {
        free(set_str);
    }
}

/**
 * @brief 发送响应头
 * @param res 响应对象
 * @param status_code HTTP 状态码
 * @param status_message 状态文本 (NULL 则自动推断)
 * @return 0 成功，-1 失败
 */
int lnet_http_response_write_head(lnet_http_response *res, int status_code,
                                   const char *status_message) {
    if (!res || !res->sock) return -1;
    if (res->headers_sent) return -1;

    res->status_code = status_code;
    if (status_message)
        res->status_message = strdup(status_message);
    else
        res->status_message = strdup(lnet_http_status_text(status_code));

    char hdr_buf[LNET_MAX_HEADER_LEN];
    int pos = snprintf(hdr_buf, sizeof(hdr_buf),
        "HTTP/1.1 %d %s\r\n",
        res->status_code, res->status_message ? res->status_message : "OK");

    for (int i = 0; i < res->header_count; i++) {
        pos += snprintf(hdr_buf + pos, sizeof(hdr_buf) - (size_t)pos,
            "%s: %s\r\n", res->headers[i].name, res->headers[i].value);
    }

    /* Set-Cookie headers */
    for (int i = 0; i < res->set_cookie_count; i++) {
        pos += snprintf(hdr_buf + pos, sizeof(hdr_buf) - (size_t)pos,
            "Set-Cookie: %s\r\n", res->set_cookies[i]);
    }

    /* Connection */
    pos += snprintf(hdr_buf + pos, sizeof(hdr_buf) - (size_t)pos,
        "Connection: close\r\n");

    pos += snprintf(hdr_buf + pos, sizeof(hdr_buf) - (size_t)pos,
        "\r\n");

    if (lnet_socket_send(res->sock, hdr_buf, (size_t)pos) < 0) return -1;
    res->headers_sent = 1;
    return 0;
}

/**
 * @brief 写入响应体数据 (追加)
 * @param res 响应对象
 * @param data 数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_http_response_write(lnet_http_response *res, const void *data, size_t len) {
    if (!res || !data || len == 0) return 0;

    /* 如果响应头未发送，先发送默认头 */
    if (!res->headers_sent) {
        lnet_http_response_set_header(res, "Content-Type", "text/html; charset=utf-8");
        if (lnet_http_response_write_head(res, 200, NULL) != 0) return -1;
    }

    /* 扩展缓冲区 */
    if (res->body_len + len + 1 > res->body_capacity) {
        size_t new_cap = res->body_capacity * 2;
        while (new_cap < res->body_len + len + 1) new_cap *= 2;
        char *new_body = (char *)realloc(res->body, new_cap);
        if (!new_body) return -1;
        res->body = new_body;
        res->body_capacity = new_cap;
    }

    memcpy(res->body + res->body_len, data, len);
    res->body_len += len;
    res->body[res->body_len] = '\0';
    return 0;
}

/**
 * @brief 写入格式化响应体数据
 * @param res 响应对象
 * @param fmt 格式化字符串
 * @return 0 成功，-1 失败
 */
int lnet_http_response_writef(lnet_http_response *res, const char *fmt, ...) {
    if (!res || !fmt) return -1;
    va_list args;
    va_start(args, fmt);
    char buf[LNET_BUFFER_SIZE];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return -1;
    return lnet_http_response_write(res, buf, (size_t)n);
}

/**
 * @brief 结束响应，发送所有缓冲的响应体数据
 * @param res 响应对象
 * @return 0 成功，-1 失败
 */
int lnet_http_response_end(lnet_http_response *res) {
    if (!res) return -1;

    /* 如果响应头未发送，先发送 */
    if (!res->headers_sent) {
        if (res->body_len > 0) {
            char cl_str[32];
            snprintf(cl_str, sizeof(cl_str), "%zu", res->body_len);
            lnet_http_response_set_header(res, "Content-Length", cl_str);
        }
        lnet_http_response_set_header(res, "Content-Type", "text/html; charset=utf-8");
        if (lnet_http_response_write_head(res, res->status_code, res->status_message) != 0)
            return -1;
    }

    /* 发送响应体 */
    if (res->body && res->body_len > 0) {
        if (lnet_socket_send(res->sock, res->body, res->body_len) < 0)
            return -1;
    }
    return 0;
}

/**
 * @brief 一次性发送完整响应
 * @param res 响应对象
 * @param status_code HTTP 状态码
 * @param body 响应体数据
 * @param body_len 响应体长度
 * @param content_type 内容类型
 * @return 0 成功，-1 失败
 */
int lnet_http_response_send(lnet_http_response *res, int status_code,
                             const void *body, size_t body_len,
                             const char *content_type) {
    if (!res) return -1;
    if (content_type)
        lnet_http_response_set_header(res, "Content-Type", content_type);
    if (body && body_len > 0) {
        char cl_str[32];
        snprintf(cl_str, sizeof(cl_str), "%zu", body_len);
        lnet_http_response_set_header(res, "Content-Length", cl_str);
    }
    if (lnet_http_response_write_head(res, status_code, NULL) != 0) return -1;
    if (body && body_len > 0) {
        if (lnet_socket_send(res->sock, body, body_len) < 0) return -1;
    }
    return 0;
}

/**
 * @brief 发送 JSON 响应
 * @param res 响应对象
 * @param status_code HTTP 状态码
 * @param json JSON 字符串
 * @return 0 成功，-1 失败
 */
int lnet_http_response_json(lnet_http_response *res, int status_code,
                             const char *json) {
    return lnet_http_response_send(res, status_code, json,
        json ? strlen(json) : 0, "application/json; charset=utf-8");
}

/**
 * @brief 发送错误响应 (带 HTML 格式错误页面)
 * @param res 响应对象
 * @param status_code HTTP 状态码
 * @param message 错误消息
 * @return 0 成功，-1 失败
 */
int lnet_http_response_error(lnet_http_response *res, int status_code,
                              const char *message) {
    if (!res) return -1;
    const char *status_text = lnet_http_status_text(status_code);
    if (!message) message = status_text;

    char html[4096];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><p>%s</p>"
        "<hr><p><em>LXCLUA HTTP Server</em></p></body></html>",
        status_code, status_text, status_code, status_text, message);

    return lnet_http_response_send(res, status_code, html, strlen(html),
        "text/html; charset=utf-8");
}

/**
 * @brief 发送文件作为响应体
 * @param res 响应对象
 * @param filepath 文件路径
 * @return 0 成功，-1 失败
 */
int lnet_http_response_send_file(lnet_http_response *res, const char *filepath) {
    if (!res || !filepath) return -1;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        return lnet_http_response_error(res, 404, "File not found");
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < 0 || fsize > 100 * 1024 * 1024) {
        fclose(fp);
        return lnet_http_response_error(res, 500, "File too large");
    }

    const char *mime = lnet_mime_type_from_path(filepath);
    lnet_http_response_set_header(res, "Content-Type", mime);

    char cl_str[32];
    snprintf(cl_str, sizeof(cl_str), "%ld", fsize);
    lnet_http_response_set_header(res, "Content-Length", cl_str);

    if (lnet_http_response_write_head(res, 200, NULL) != 0) {
        fclose(fp);
        return -1;
    }

    char fbuf[8192];
    size_t n;
    while ((n = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
        if (lnet_socket_send(res->sock, fbuf, n) < 0) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

/**
 * @brief 释放 HTTP 响应对象
 * @param res 响应对象
 */
void lnet_http_response_free(lnet_http_response *res) {
    if (!res) return;
    free(res->status_message);
    res->status_message = NULL;
    for (int i = 0; i < res->header_count; i++) {
        free(res->headers[i].name);
        free(res->headers[i].value);
    }
    res->header_count = 0;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    res->body_capacity = 0;
    if (res->set_cookies) {
        for (int i = 0; i < res->set_cookie_count; i++)
            free(res->set_cookies[i]);
        free(res->set_cookies);
        res->set_cookies = NULL;
        res->set_cookie_count = 0;
    }
}

/* ========================================================================
 * HTTP 服务器
 * ======================================================================== */

/**
 * @brief 使用默认配置填充服务器配置
 * @param config 服务器配置输出
 */
static void lnet_http_server_config_default(lnet_http_server_config *config) {
    memset(config, 0, sizeof(*config));
    config->bind_host = "0.0.0.0";
    config->port = 8080;
    config->backlog = LNET_SERVER_BACKLOG;
    config->max_connections = 1024;
    config->recv_timeout_ms = 30000;
    config->send_timeout_ms = 30000;
    config->keepalive_timeout_ms = 5000;
    config->max_body_size = 10 * 1024 * 1024;
    config->max_headers_size = LNET_MAX_HEADER_LEN;
    config->max_url_len = LNET_MAX_URL_LEN;
    config->enable_keepalive = 1;
}

/**
 * @brief 创建 HTTP 服务器对象
 * @param config 服务器配置 (NULL 使用默认值)
 * @return 新服务器对象
 */
lnet_http_server *lnet_http_server_new(const lnet_http_server_config *config) {
    lnet_http_server *server = (lnet_http_server *)calloc(1, sizeof(lnet_http_server));
    if (!server) return NULL;

    if (config)
        memcpy(&server->config, config, sizeof(*config));
    else
        lnet_http_server_config_default(&server->config);

    server->loop = server->config.loop;
    l_mutex_init(&server->conn_lock);
    server->max_connections = server->config.max_connections;
    return server;
}

/**
 * @brief 释放 HTTP 服务器对象
 * @param server 服务器对象
 */
void lnet_http_server_free(lnet_http_server *server) {
    if (!server) return;
    lnet_http_server_stop(server);
    if (server->listen_sock) {
        lnet_socket_free(server->listen_sock);
        server->listen_sock = NULL;
    }
    l_mutex_destroy(&server->conn_lock);
    free(server);
}

/**
 * @brief 添加路由处理器
 * @param server 服务器对象
 * @param method HTTP 方法 (如 "GET", "POST", "*" 表示所有)
 * @param path 路径模式
 * @param handler 处理函数
 * @param user_data 用户数据
 * @return 0 成功，-1 路由表已满
 */
int lnet_http_server_add_route(lnet_http_server *server, const char *method,
                                const char *path, lnet_route_handler handler,
                                void *user_data) {
    if (!server || !path || !handler) return -1;
    if (server->route_count >= LNET_MAX_ROUTES) return -1;

    lnet_route *r = &server->routes[server->route_count];
    r->method = method ? strdup(method) : strdup("*");
    r->path = strdup(path);
    r->handler = handler;
    r->user_data = user_data;
    r->is_regex = (strchr(path, ':') != NULL) ? 1 : 0;
    server->route_count++;
    return 0;
}

/**
 * @brief 添加中间件
 * @param server 服务器对象
 * @param handler 中间件函数
 * @param user_data 用户数据
 * @return 0 成功，-1 中间件链已满
 */
int lnet_http_server_add_middleware(lnet_http_server *server,
                                     lnet_middleware_handler handler,
                                     void *user_data) {
    if (!server || !handler) return -1;
    if (server->middleware_count >= LNET_MAX_MIDDLEWARE) return -1;

    server->middlewares[server->middleware_count].handler = handler;
    server->middlewares[server->middleware_count].user_data = user_data;
    server->middleware_count++;
    return 0;
}

/**
 * @brief 设置默认路由处理器 (404 等)
 * @param server 服务器对象
 * @param handler 处理函数
 * @param user_data 用户数据
 */
void lnet_http_server_set_default_handler(lnet_http_server *server,
                                           lnet_route_handler handler,
                                           void *user_data) {
    if (!server) return;
    server->default_handler = handler;
    server->default_handler_data = user_data;
}

/**
 * @brief 解析 HTTP 请求的请求行
 * @param line 请求行字符串
 * @param method 输出方法
 * @param method_size 方法缓冲区大小
 * @param url 输出 URL
 * @param url_size URL 缓冲区大小
 * @return 0 成功，-1 格式错误
 */
static int lnet_parse_request_line(const char *line, char *method, size_t method_size,
                                    char *url, size_t url_size) {
    const char *p = line;
    size_t i = 0;
    /* 解析方法 */
    while (*p && *p != ' ' && i < method_size - 1)
        method[i++] = *p++;
    method[i] = '\0';
    if (!*p) return -1;
    p++;
    /* 解析 URL */
    i = 0;
    while (*p && *p != ' ' && i < url_size - 1)
        url[i++] = *p++;
    url[i] = '\0';
    return 0;
}

/**
 * @brief 解析服务器端 HTTP 请求
 * @param conn 服务器连接
 * @param req_out 输出请求对象 (由调用者释放)
 * @return 0 成功，-1 失败
 */
static int lnet_server_parse_request(lnet_server_conn *conn, lnet_http_request **req_out) {
    if (!conn || !conn->recv_buf) return -1;

    lnet_http_request *req = (lnet_http_request *)calloc(1, sizeof(lnet_http_request));
    if (!req) return -1;

    const char *data = conn->recv_buf;
    size_t data_len = conn->recv_buf_len;

    /* 查找请求行 */
    const char *line_end = (const char *)memchr(data, '\n', data_len);
    if (!line_end) { free(req); return -1; }

    char req_line[LNET_MAX_URL_LEN];
    size_t rlen = (size_t)(line_end - data);
    if (rlen > 0 && data[rlen - 1] == '\r') rlen--;
    if (rlen >= sizeof(req_line)) rlen = sizeof(req_line) - 1;
    memcpy(req_line, data, rlen);
    req_line[rlen] = '\0';

    if (lnet_parse_request_line(req_line, req->method, sizeof(req->method),
                                 req->url, sizeof(req->url)) != 0) {
        free(req);
        return -1;
    }

    /* 解析 URL 路径和查询字符串 */
    {
        char *qmark = strchr(req->url, '?');
        if (qmark) {
            *qmark = '\0';
            req->path = strdup(req->url);
            req->query_string = strdup(qmark + 1);
            *qmark = '?'; /* 恢复 */
        } else {
            req->path = strdup(req->url);
        }

        char *hash = strchr(req->path, '#');
        if (hash) {
            *hash = '\0';
            req->fragment = strdup(hash + 1);
        }
    }

    /* 解析请求头 */
    const char *p = line_end + 1;
    const char *end = data + data_len;
    req->header_count = 0;

    while (p < end) {
        if (*p == '\r' || *p == '\n') {
            /* 头结束 */
            if (*p == '\r') p++;
            if (p < end && *p == '\n') p++;
            break;
        }

        const char *hdr_end = (const char *)memchr(p, '\n', (size_t)(end - p));
        if (!hdr_end) break;

        const char *colon = (const char *)memchr(p, ':', (size_t)(hdr_end - p));
        if (colon && req->header_count < LNET_MAX_HEADERS) {
            size_t name_len = (size_t)(colon - p);
            const char *val = colon + 1;
            while (val < hdr_end && (*val == ' ' || *val == '\t')) val++;
            size_t val_len = (size_t)(hdr_end - val);
            if (val_len > 0 && val[val_len - 1] == '\r') val_len--;

            req->headers[req->header_count].name = (char *)malloc(name_len + 1);
            req->headers[req->header_count].value = (char *)malloc(val_len + 1);
            if (req->headers[req->header_count].name && req->headers[req->header_count].value) {
                memcpy(req->headers[req->header_count].name, p, name_len);
                req->headers[req->header_count].name[name_len] = '\0';
                memcpy(req->headers[req->header_count].value, val, val_len);
                req->headers[req->header_count].value[val_len] = '\0';
                req->header_count++;
            }
        }

        p = hdr_end + 1;
    }

    /* 请求体 (如果有) */
    if (p < end && *p != '\0') {
        size_t body_len = (size_t)(end - p);
        const char *cl_str = lnet_http_request_get_header(req, "content-length");
        if (cl_str) {
            size_t cl = (size_t)atol(cl_str);
            if (cl > 0 && cl <= body_len) {
                req->body = (char *)malloc(cl + 1);
                if (req->body) {
                    memcpy(req->body, p, cl);
                    req->body[cl] = '\0';
                    req->body_len = cl;
                    req->body_owned = 1;
                }
            }
        }
    }

    /* 获取对端地址 */
    {
        char ip[INET6_ADDRSTRLEN];
        int port;
        if (lnet_socket_getpeername(conn->sock, ip, sizeof(ip), &port) == 0) {
            strncpy(req->remote_ip, ip, sizeof(req->remote_ip) - 1);
            req->remote_port = port;
        }
        if (lnet_socket_getsockname(conn->sock, ip, sizeof(ip), &port) == 0) {
            strncpy(req->local_ip, ip, sizeof(req->local_ip) - 1);
            req->local_port = port;
        }
    }

    req->sock = conn->sock;
    *req_out = req;
    return 0;
}

/**
 * @brief 匹配路由
 * @param server 服务器对象
 * @param req 请求对象
 * @param handler 输出匹配的处理函数
 * @param user_data 输出用户数据
 * @return 0 匹配成功，-1 未匹配
 */
static int lnet_server_match_route(lnet_http_server *server, lnet_http_request *req,
                                    lnet_route_handler *handler, void **user_data) {
    for (int i = 0; i < server->route_count; i++) {
        lnet_route *r = &server->routes[i];
        /* 检查方法匹配 */
        if (strcmp(r->method, "*") != 0 &&
            lnet_strcasecmp(r->method, req->method) != 0)
            continue;

        /* 检查路径匹配 */
        if (r->is_regex) {
            /* 简单的 :param 匹配 */
            const char *pp = r->path;
            const char *rp = req->path;
            int match = 1;
            while (*pp && *rp) {
                if (*pp == ':') {
                    /* 跳过参数名 */
                    while (*pp && *pp != '/') pp++;
                    /* 跳过参数值 */
                    while (*rp && *rp != '/') rp++;
                } else if (*pp == *rp) {
                    pp++; rp++;
                } else {
                    match = 0;
                    break;
                }
            }
            if (match && *pp == '\0' && *rp == '\0') {
                *handler = r->handler;
                *user_data = r->user_data;
                return 0;
            }
        } else {
            if (strcmp(r->path, req->path) == 0) {
                *handler = r->handler;
                *user_data = r->user_data;
                return 0;
            }
        }
    }
    return -1;
}

/**
 * @brief 服务器 accept 回调 (用于事件循环集成)
 * @param loop 事件循环
 * @param watcher I/O 观察者
 * @param events 触发的事件
 */
static void lnet_server_on_accept(event_loop *loop, ev_io_watcher *watcher, int events) {
    (void)loop;
    (void)events;
    lnet_socket *listen_sock = (lnet_socket *)watcher->data;
    lnet_http_server *server = (lnet_http_server *)listen_sock->user_data;

    if (!server || server->should_stop) return;

    lnet_socket *client = lnet_socket_accept(listen_sock);
    if (!client) return;

    l_mutex_lock(&server->conn_lock);

    if (server->connection_count >= server->max_connections) {
        l_mutex_unlock(&server->conn_lock);
        lnet_http_response res;
        lnet_http_response_init(&res, client);
        lnet_http_response_error(&res, 503, "Service Unavailable - Too many connections");
        lnet_http_response_end(&res);
        lnet_http_response_free(&res);
        lnet_socket_free(client);
        return;
    }

    /* 创建连接对象 */
    lnet_server_conn *conn = (lnet_server_conn *)calloc(1, sizeof(lnet_server_conn));
    if (!conn) {
        l_mutex_unlock(&server->conn_lock);
        lnet_socket_free(client);
        return;
    }

    conn->sock = client;
    conn->server = server;
    conn->recv_buf_cap = LNET_BUFFER_SIZE;
    conn->recv_buf = (char *)malloc(conn->recv_buf_cap);
    conn->recv_buf_len = 0;
    conn->last_activity = time(NULL);

    /* 添加到连接链表 */
    conn->next = server->connections;
    if (server->connections) server->connections->prev = conn;
    server->connections = conn;
    server->connection_count++;
    server->total_connections++;

    l_mutex_unlock(&server->conn_lock);

    /* 读取请求并处理 */
    int n;
    while ((n = lnet_socket_recv(client, conn->recv_buf + conn->recv_buf_len,
        conn->recv_buf_cap - conn->recv_buf_len - 1)) > 0) {
        conn->recv_buf_len += (size_t)n;
        conn->recv_buf[conn->recv_buf_len] = '\0';
        conn->last_activity = time(NULL);

        /* 检查是否收到完整的 HTTP 请求头 */
        const char *hdr_end = strstr(conn->recv_buf, "\r\n\r\n");
        if (hdr_end) {
            /* 检查 Content-Length 或 chunked */
            const char *cl_str = NULL;
            const char *te_str = NULL;

            /* 简单查找 Content-Length 和 Transfer-Encoding */
            const char *search = conn->recv_buf;
            const char *search_end = hdr_end;
            while (search < search_end) {
                const char *line_start = search;
                const char *line_end2 = (const char *)memchr(search, '\n',
                    (size_t)(search_end - search));
                if (!line_end2) break;

                if (lnet_strncasecmp(line_start, "content-length:", 15) == 0) {
                    cl_str = line_start + 15;
                    while (*cl_str == ' ') cl_str++;
                }
                if (lnet_strncasecmp(line_start, "transfer-encoding:", 18) == 0) {
                    te_str = line_start + 18;
                    while (*te_str == ' ') te_str++;
                }
                search = line_end2 + 1;
            }

            size_t hdr_len = (size_t)(hdr_end - conn->recv_buf) + 4;

            if (te_str && strstr(te_str, "chunked")) {
                /* chunked: 需要读取到 0\r\n\r\n */
                if (strstr(conn->recv_buf + hdr_len, "\r\n0\r\n\r\n") ||
                    strstr(conn->recv_buf + hdr_len, "0\r\n\r\n")) {
                    break;
                }
            } else if (cl_str) {
                size_t cl = (size_t)atol(cl_str);
                if (conn->recv_buf_len >= hdr_len + cl) {
                    break;
                }
            } else {
                /* 无 Content-Length，认为头结束即请求结束 */
                break;
            }
        }
    }

    /* 解析并处理请求 */
    lnet_http_request *req = NULL;
    if (lnet_server_parse_request(conn, &req) == 0 && req) {
        lnet_http_response res_obj;
        lnet_http_response_init(&res_obj, client);

        /* 匹配路由 */
        lnet_route_handler route_h = server->default_handler;
        void *route_ud = server->default_handler_data;
        int route_matched = 0;

        if (lnet_server_match_route(server, req, &route_h, &route_ud) == 0) {
            route_matched = 1;
        }

        if (route_h) {
            /* 执行中间件 */
            for (int i = 0; i < server->middleware_count; i++) {
                server->middlewares[i].handler(req, &res_obj,
                    server->middlewares[i].user_data, NULL, NULL);
            }
            route_h(req, &res_obj, route_ud);
        } else {
            lnet_http_response_error(&res_obj, 404, "Not Found");
        }

        server->total_requests++;

        lnet_http_response_end(&res_obj);
        lnet_http_response_free(&res_obj);
        lnet_http_request_free(req);
    }

    /* 从连接列表中移除 */
    l_mutex_lock(&server->conn_lock);
    if (conn->prev) conn->prev->next = conn->next;
    else server->connections = conn->next;
    if (conn->next) conn->next->prev = conn->prev;
    server->connection_count--;
    l_mutex_unlock(&server->conn_lock);

    free(conn->recv_buf);
    free(conn);
    lnet_socket_free(client);
}

/**
 * @brief 简单线程 accept 循环 (无事件循环时使用)
 * @param arg 服务器指针
 * @return NULL
 */
static void *lnet_server_accept_thread(void *arg) {
    lnet_http_server *server = (lnet_http_server *)arg;
    if (!server || !server->listen_sock) return NULL;

    while (server->accept_thread_running && !server->should_stop) {
        lnet_socket *client = lnet_socket_accept(server->listen_sock);
        if (!client) {
            if (!server->accept_thread_running || server->should_stop) break;
            continue;
        }

        l_mutex_lock(&server->conn_lock);
        if (server->connection_count >= server->max_connections) {
            l_mutex_unlock(&server->conn_lock);
            lnet_http_response res;
            lnet_http_response_init(&res, client);
            lnet_http_response_error(&res, 503, "Service Unavailable");
            lnet_http_response_end(&res);
            lnet_http_response_free(&res);
            lnet_socket_free(client);
            continue;
        }

        /* 创建连接对象 */
        lnet_server_conn *conn = (lnet_server_conn *)calloc(1, sizeof(lnet_server_conn));
        if (!conn) {
            l_mutex_unlock(&server->conn_lock);
            lnet_socket_free(client);
            continue;
        }
        conn->sock = client;
        conn->server = server;
        conn->recv_buf_cap = LNET_BUFFER_SIZE;
        conn->recv_buf = (char *)malloc(conn->recv_buf_cap);
        conn->recv_buf_len = 0;
        conn->last_activity = time(NULL);

        conn->next = server->connections;
        if (server->connections) server->connections->prev = conn;
        server->connections = conn;
        server->connection_count++;
        server->total_connections++;
        l_mutex_unlock(&server->conn_lock);

        /* 读取请求 */
        int n;
        while ((n = lnet_socket_recv(client, conn->recv_buf + conn->recv_buf_len,
            conn->recv_buf_cap - conn->recv_buf_len - 1)) > 0) {
            conn->recv_buf_len += (size_t)n;
            conn->recv_buf[conn->recv_buf_len] = '\0';
            conn->last_activity = time(NULL);

            const char *hdr_end = strstr(conn->recv_buf, "\r\n\r\n");
            if (hdr_end) {
                size_t hdr_len = (size_t)(hdr_end - conn->recv_buf) + 4;
                const char *cl_str = NULL;

                /* 查找 Content-Length */
                const char *search = conn->recv_buf;
                while (search < hdr_end) {
                    const char *line_end2 = (const char *)memchr(search, '\n',
                        (size_t)(hdr_end - search));
                    if (!line_end2) break;
                    if (lnet_strncasecmp(search, "content-length:", 15) == 0) {
                        cl_str = search + 15;
                        while (*cl_str == ' ') cl_str++;
                    }
                    search = line_end2 + 1;
                }

                if (cl_str) {
                    size_t cl = (size_t)atol(cl_str);
                    if (conn->recv_buf_len >= hdr_len + cl) break;
                } else {
                    break;
                }
            }
        }

        /* 解析并处理请求 */
        lnet_http_request *req = NULL;
        if (lnet_server_parse_request(conn, &req) == 0 && req) {
            lnet_http_response res_obj;
            lnet_http_response_init(&res_obj, client);

            lnet_route_handler route_h = server->default_handler;
            void *route_ud = server->default_handler_data;
            int has_route = 0;

            if (lnet_server_match_route(server, req, &route_h, &route_ud) == 0) {
                has_route = 1;
            }

            if (server->L && has_route) {
                /* 有 Lua 回调：发送临时响应，避免阻塞后台线程 */
                lnet_http_response_send(&res_obj, 200,
                    "{\"status\":\"ok\",\"msg\":\"route registered, use event loop for full support\"}",
                    67, "application/json");
            } else if (route_h && route_h != server->default_handler) {
                /* 纯 C 路由处理器：直接调用 */
                route_h(req, &res_obj, route_ud);
            } else if (route_h) {
                /* 默认处理器 */
                route_h(req, &res_obj, route_ud);
            } else {
                /* 无路由匹配 */
                lnet_http_response_error(&res_obj, 404, "Not Found");
            }

            server->total_requests++;

            lnet_http_response_end(&res_obj);
            lnet_http_response_free(&res_obj);
            lnet_http_request_free(req);
        }

        /* 从连接列表移除 */
        l_mutex_lock(&server->conn_lock);
        if (conn->prev) conn->prev->next = conn->next;
        else server->connections = conn->next;
        if (conn->next) conn->next->prev = conn->prev;
        server->connection_count--;
        l_mutex_unlock(&server->conn_lock);

        free(conn->recv_buf);
        free(conn);
        lnet_socket_free(client);
    }

    return NULL;
}

/**
 * @brief 启动 HTTP 服务器
 * @param server 服务器对象
 * @return 0 成功，-1 失败
 */
int lnet_http_server_start(lnet_http_server *server) {
    if (!server) return -1;
    if (server->running) return -1;

    lnet_global_init();

    server->listen_sock = lnet_socket_new(LNET_AF_INET, LNET_SOCK_STREAM, 1);
    if (!server->listen_sock) return -1;

    if (lnet_socket_bind(server->listen_sock, server->config.bind_host,
                          server->config.port) != 0) {
        lnet_socket_free(server->listen_sock);
        server->listen_sock = NULL;
        return -1;
    }

    if (lnet_socket_listen(server->listen_sock, server->config.backlog) != 0) {
        lnet_socket_free(server->listen_sock);
        server->listen_sock = NULL;
        return -1;
    }

    server->should_stop = 0;
    server->running = 1;
    server->listen_sock->user_data = server;

    if (server->loop) {
        /* 集成事件循环 */
        memset(&server->listen_sock->io_watcher, 0, sizeof(ev_io_watcher));
        server->listen_sock->io_watcher.fd = (int)server->listen_sock->fd;
        server->listen_sock->io_watcher.events = EV_READ | EV_PERSIST;
        server->listen_sock->io_watcher.callback = lnet_server_on_accept;
        server->listen_sock->io_watcher.data = server->listen_sock;

        if (ev_io_start(server->loop, &server->listen_sock->io_watcher) != 0) {
            lnet_socket_free(server->listen_sock);
            server->listen_sock = NULL;
            server->running = 0;
            return -1;
        }
        server->listen_sock->loop = server->loop;
    } else {
        /* 无事件循环时，使用简单的线程 accept 循环 */
        server->accept_thread_running = 1;
        if (l_thread_create(&server->accept_thread, lnet_server_accept_thread, server) != 0) {
            server->accept_thread_running = 0;
            lnet_socket_free(server->listen_sock);
            server->listen_sock = NULL;
            server->running = 0;
            return -1;
        }
    }

    return 0;
}

/**
 * @brief 停止 HTTP 服务器
 * @param server 服务器对象
 */
void lnet_http_server_stop(lnet_http_server *server) {
    if (!server) return;
    server->should_stop = 1;
    server->running = 0;

    /* 停止接受线程 */
    server->accept_thread_running = 0;

    if (server->listen_sock) {
        if (server->listen_sock->io_watcher.active && server->loop) {
            ev_io_stop(server->loop, &server->listen_sock->io_watcher);
        }
        /* 关闭监听 socket 以解除阻塞的 accept */
        lnet_closesocket(server->listen_sock->fd);
        server->listen_sock->fd = LNET_INVALID_SOCKET;
    }

    /* 等待接受线程结束 */
    l_thread_join(server->accept_thread, NULL);

    if (server->listen_sock) {
        lnet_socket_free(server->listen_sock);
        server->listen_sock = NULL;
    }

    /* 关闭所有连接 */
    l_mutex_lock(&server->conn_lock);
    lnet_server_conn *conn = server->connections;
    while (conn) {
        lnet_server_conn *next = conn->next;
        lnet_socket_free(conn->sock);
        free(conn->recv_buf);
        free(conn);
        conn = next;
    }
    server->connections = NULL;
    server->connection_count = 0;
    l_mutex_unlock(&server->conn_lock);
}

/* ========================================================================
 * WebSocket 实现
 * ======================================================================== */

/** WebSocket GUID 用于握手 */
#define LNET_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/**
 * @brief 计算 WebSocket 握手 Accept 值
 * 对 key + GUID 进行 SHA1 哈希，然后 Base64 编码
 * @param key Sec-WebSocket-Key
 * @return Base64 编码的 Accept 值，调用者负责 free
 */
char *lnet_ws_compute_accept(const char *key) {
    if (!key) return NULL;
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", key, LNET_WS_GUID);

    lnet_sha1_ctx ctx;
    uint8_t digest[LNET_SHA1_DIGEST_SIZE];
    lnet_sha1_init(&ctx);
    lnet_sha1_update(&ctx, (const uint8_t *)combined, strlen(combined));
    lnet_sha1_final(&ctx, digest);

    /* Base64 编码 SHA1 结果 */
    return lnet_base64_encode(digest, LNET_SHA1_DIGEST_SIZE);
}

/**
 * @brief 生成随机 WebSocket Key (24 字符 Base64)
 * @param buf 输出缓冲区 (至少 25 字节)
 * @param len 缓冲区大小
 */
void lnet_ws_generate_key(char *buf, size_t len) {
    if (!buf || len < 17) return;
    /* 生成 16 字节随机数 */
    uint8_t raw[16];
    size_t i;
    srand((unsigned int)(time(NULL) ^ (uintptr_t)buf));
    for (i = 0; i < sizeof(raw); i++)
        raw[i] = (uint8_t)(rand() % 256);
    char *b64 = lnet_base64_encode(raw, sizeof(raw));
    if (b64) {
        strncpy(buf, b64, len - 1);
        buf[len - 1] = '\0';
        free(b64);
    }
}

/**
 * @brief 生成随机 WebSocket 掩码 key (4 字节)
 * @param mask_key 输出缓冲区 (4 字节)
 */
void lnet_ws_generate_mask(uint8_t mask_key[4]) {
    size_t i;
    srand((unsigned int)(time(NULL) ^ (uintptr_t)mask_key));
    for (i = 0; i < 4; i++)
        mask_key[i] = (uint8_t)(rand() % 256);
}

/**
 * @brief 对 WebSocket 数据应用或移除掩码
 * 掩码操作是对称的 (XOR)，应用两次恢复原始数据
 * @param data 数据
 * @param len 数据长度
 * @param mask_key 4 字节掩码 key
 */
void lnet_ws_mask_data(uint8_t *data, size_t len, const uint8_t mask_key[4]) {
    if (!data || !mask_key) return;
    size_t i;
    for (i = 0; i < len; i++)
        data[i] ^= mask_key[i % 4];
}

/*
 * WebSocket 帧构建和发送 (内部辅助)
 */

/**
 * @brief 构建并发送 WebSocket 帧
 * @param ws WebSocket 对象
 * @param opcode 操作码
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @return 0 成功，-1 失败
 */
static int lnet_ws_send_frame(lnet_websocket *ws, lnet_ws_opcode_t opcode,
                               const uint8_t *payload, size_t payload_len) {
    if (!ws || !ws->sock || !ws->is_open) return -1;

    size_t frame_size = 2; /* 最小头部 */
    size_t ext_len_size = 0;
    size_t mask_size = ws->is_client ? 4 : 0;

    if (payload_len <= 125) {
        ext_len_size = 0;
    } else if (payload_len <= 65535) {
        ext_len_size = 2;
    } else {
        ext_len_size = 8;
    }
    frame_size += ext_len_size + mask_size + payload_len;

    uint8_t *frame = (uint8_t *)malloc(frame_size);
    if (!frame) return -1;

    size_t pos = 0;
    frame[pos++] = (uint8_t)(0x80 | (opcode & 0x0F)); /* FIN=1, opcode */

    if (payload_len <= 125) {
        frame[pos++] = (uint8_t)((ws->is_client ? 0x80 : 0x00) | (uint8_t)payload_len);
    } else if (payload_len <= 65535) {
        frame[pos++] = (uint8_t)((ws->is_client ? 0x80 : 0x00) | 126);
        frame[pos++] = (uint8_t)((payload_len >> 8) & 0xFF);
        frame[pos++] = (uint8_t)(payload_len & 0xFF);
    } else {
        frame[pos++] = (uint8_t)((ws->is_client ? 0x80 : 0x00) | 127);
        frame[pos++] = (uint8_t)((payload_len >> 56) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 48) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 40) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 32) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 24) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 16) & 0xFF);
        frame[pos++] = (uint8_t)((payload_len >> 8) & 0xFF);
        frame[pos++] = (uint8_t)(payload_len & 0xFF);
    }

    if (ws->is_client) {
        uint8_t mask[4];
        lnet_ws_generate_mask(mask);
        memcpy(&frame[pos], mask, 4);
        pos += 4;
        memcpy(&frame[pos], payload, payload_len);
        lnet_ws_mask_data(&frame[pos], payload_len, mask);
    } else {
        memcpy(&frame[pos], payload, payload_len);
    }

    int sent = lnet_socket_send(ws->sock, frame, frame_size);
    free(frame);

    if (sent < 0) return -1;
    ws->messages_sent++;
    ws->bytes_sent += payload_len;
    return 0;
}

/**
 * @brief 创建 WebSocket 客户端连接
 * @param url WebSocket URL (ws:// 或 wss://)
 * @param L Lua 状态 (用于回调)
 * @return WebSocket 对象，失败返回 NULL
 */
lnet_websocket *lnet_websocket_connect(const char *url, lua_State *L) {
    if (!url) return NULL;

    /* 解析 URL */
    lnet_url_parts *parts = lnet_url_parse(url);
    if (!parts) return NULL;

    int is_ssl = 0;
    int port;

    if (lnet_strcasecmp(parts->scheme, "wss") == 0) {
        is_ssl = 1;
        port = parts->port ? parts->port : 443;
    } else if (lnet_strcasecmp(parts->scheme, "ws") == 0) {
        port = parts->port ? parts->port : 80;
    } else {
        lnet_url_parts_free(parts);
        return NULL;
    }

    /* 创建 socket 并连接 */
    lnet_socket *sock = lnet_socket_new(LNET_AF_INET, LNET_SOCK_STREAM, 0);
    if (!sock) {
        lnet_url_parts_free(parts);
        return NULL;
    }

    if (lnet_socket_connect(sock, parts->host, port) != 0) {
        lnet_socket_free(sock);
        lnet_url_parts_free(parts);
        return NULL;
    }

    /* TLS */
    if (is_ssl) {
        lnet_tls_config tls_cfg;
        memset(&tls_cfg, 0, sizeof(tls_cfg));
        tls_cfg.server_name = parts->host;
        if (lnet_tls_connect(sock, &tls_cfg) != 0) {
            lnet_socket_free(sock);
            lnet_url_parts_free(parts);
            return NULL;
        }
        /* 轮询握手 */
        int attempts = 100;
        while (attempts-- > 0) {
            lnet_tls_result_t r = lnet_tls_handshake(sock);
            if (r == LNET_TLS_OK) break;
            if (r == LNET_TLS_ERROR) {
                lnet_socket_free(sock);
                lnet_url_parts_free(parts);
                return NULL;
            }
#if defined(LNET_PLATFORM_WINDOWS)
            Sleep(10);
#else
            usleep(10000);
#endif
        }
    }

    /* 生成 WebSocket Key */
    char ws_key[32];
    lnet_ws_generate_key(ws_key, sizeof(ws_key));

    /* 构建升级请求 */
    char upg_req[4096];
    int req_len = snprintf(upg_req, sizeof(upg_req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        parts->path ? parts->path : "/",
        parts->host, port,
        ws_key);

    if (lnet_socket_send(sock, upg_req, (size_t)req_len) < 0) {
        lnet_socket_free(sock);
        lnet_url_parts_free(parts);
        return NULL;
    }

    /* 读取握手响应 */
    char resp_buf[8192];
    size_t resp_len = 0;
    int n;
    while (resp_len < sizeof(resp_buf) - 1) {
        n = lnet_socket_recv(sock, resp_buf + resp_len, sizeof(resp_buf) - resp_len - 1);
        if (n <= 0) break;
        resp_len += (size_t)n;
        resp_buf[resp_len] = '\0';
        if (strstr(resp_buf, "\r\n\r\n")) break;
    }

    /* 验证响应 */
    if (!strstr(resp_buf, "101") || !strstr(resp_buf, "Upgrade")) {
        lnet_socket_free(sock);
        lnet_url_parts_free(parts);
        return NULL;
    }

    /* 创建 WebSocket 对象 */
    lnet_websocket *ws = (lnet_websocket *)calloc(1, sizeof(lnet_websocket));
    if (!ws) {
        lnet_socket_free(sock);
        lnet_url_parts_free(parts);
        return NULL;
    }

    ws->sock = sock;
    ws->is_client = 1;
    ws->is_open = 1;
    ws->is_closing = 0;
    ws->url = parts->raw_url ? strdup(parts->raw_url) : strdup(url);
    ws->path = parts->path ? strdup(parts->path) : strdup("/");
    ws->L = L;

    lnet_url_parts_free(parts);
    return ws;
}

/**
 * @brief 从已有 HTTP 请求升级到 WebSocket (服务器端)
 * @param req HTTP 升级请求
 * @param res HTTP 响应
 * @param L Lua 状态
 * @return WebSocket 对象，失败返回 NULL
 */
lnet_websocket *lnet_websocket_upgrade(lnet_http_request *req,
                                        lnet_http_response *res,
                                        lua_State *L) {
    if (!req || !res || !req->sock) return NULL;

    /* 验证升级请求 */
    const char *upgrade = lnet_http_request_get_header(req, "upgrade");
    const char *ws_key = lnet_http_request_get_header(req, "sec-websocket-key");
    const char *ws_ver = lnet_http_request_get_header(req, "sec-websocket-version");

    if (!upgrade || lnet_strcasecmp(upgrade, "websocket") != 0) return NULL;
    if (!ws_key) return NULL;

    /* 计算 Accept */
    char *accept = lnet_ws_compute_accept(ws_key);
    if (!accept) return NULL;

    /* 发送 101 响应 */
    lnet_http_response_set_header(res, "Upgrade", "websocket");
    lnet_http_response_set_header(res, "Connection", "Upgrade");
    lnet_http_response_set_header(res, "Sec-WebSocket-Accept", accept);
    lnet_http_response_set_header(res, "Sec-WebSocket-Version", ws_ver ? ws_ver : "13");

    if (lnet_http_response_write_head(res, 101, "Switching Protocols") != 0) {
        free(accept);
        return NULL;
    }

    /* 刷新响应头 */
    lnet_http_response_end(res);

    free(accept);

    /* 创建 WebSocket 对象 */
    lnet_websocket *ws = (lnet_websocket *)calloc(1, sizeof(lnet_websocket));
    if (!ws) return NULL;

    ws->sock = req->sock;
    ws->is_client = 0;
    ws->is_open = 1;
    ws->is_closing = 0;
    ws->url = strdup(req->url);
    ws->path = req->path ? strdup(req->path) : strdup("/");
    ws->L = L;

    return ws;
}

/**
 * @brief 发送 WebSocket 文本消息
 * @param ws WebSocket 对象
 * @param data 文本数据
 * @param len 长度 (0 表示自动 strlen)
 * @return 0 成功，-1 失败
 */
int lnet_websocket_send_text(lnet_websocket *ws, const char *data, size_t len) {
    if (!ws || !data) return -1;
    if (len == 0) len = strlen(data);
    return lnet_ws_send_frame(ws, LNET_WS_OP_TEXT, (const uint8_t *)data, len);
}

/**
 * @brief 发送 WebSocket 二进制消息
 * @param ws WebSocket 对象
 * @param data 二进制数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_websocket_send_binary(lnet_websocket *ws, const uint8_t *data, size_t len) {
    if (!ws || !data || len == 0) return -1;
    return lnet_ws_send_frame(ws, LNET_WS_OP_BINARY, data, len);
}

/**
 * @brief 发送 WebSocket Ping
 * @param ws WebSocket 对象
 * @param data 可选数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_websocket_ping(lnet_websocket *ws, const uint8_t *data, size_t len) {
    if (!ws) return -1;
    return lnet_ws_send_frame(ws, LNET_WS_OP_PING, data ? data : (const uint8_t *)"", len);
}

/**
 * @brief 关闭 WebSocket 连接
 * @param ws WebSocket 对象
 * @param code 关闭码
 * @param reason 关闭原因 (可为 NULL)
 */
void lnet_websocket_close(lnet_websocket *ws, int code, const char *reason) {
    if (!ws || !ws->is_open || ws->is_closing) return;

    ws->is_closing = 1;
    ws->is_open = 0;

    /* 构建关闭帧 */
    uint8_t close_payload[128];
    size_t plen = 0;
    if (code > 0) {
        close_payload[0] = (uint8_t)((code >> 8) & 0xFF);
        close_payload[1] = (uint8_t)(code & 0xFF);
        plen = 2;
        if (reason) {
            size_t rlen = strlen(reason);
            if (rlen > sizeof(close_payload) - 2)
                rlen = sizeof(close_payload) - 2;
            memcpy(close_payload + 2, reason, rlen);
            plen += rlen;
        }
    }

    lnet_ws_send_frame(ws, LNET_WS_OP_CLOSE, close_payload, plen);

    /* 触发回调 */
    if (ws->on_close)
        ws->on_close(ws, code, reason, ws->user_data);
}

/**
 * @brief 释放 WebSocket 对象
 * @param ws WebSocket 对象
 */
void lnet_websocket_free(lnet_websocket *ws) {
    if (!ws) return;
    if (ws->is_open && !ws->is_closing)
        lnet_websocket_close(ws, LNET_WS_CLOSE_GOING_AWAY, "Client disconnect");

    if (ws->sock) {
        lnet_socket_free(ws->sock);
        ws->sock = NULL;
    }

    /* 释放发送队列 */
    lnet_ws_send_item *item = ws->send_queue_head;
    while (item) {
        lnet_ws_send_item *next = item->next;
        if (item->owned) free(item->data);
        free(item);
        item = next;
    }

    free(ws->recv_buf);
    free(ws->url);
    free(ws->path);
    free(ws->protocol);

    /* 释放 Lua 引用 */
    if (ws->L && ws->lua_ref) {
        luaL_unref(ws->L, LUA_REGISTRYINDEX, ws->lua_ref);
    }

    free(ws);
}

/**
 * @brief 解析单个 WebSocket 帧 (从缓冲区)
 * @param buf 数据缓冲区
 * @param buf_len 缓冲区长度
 * @param frame 输出帧对象 (调用者负责释放 payload)
 * @return 消耗的字节数，0=需要更多数据，-1=格式错误
 */
static ssize_t lnet_ws_parse_frame(const uint8_t *buf, size_t buf_len,
                                    lnet_ws_frame *frame) {
    if (buf_len < 2) return 0;

    size_t pos = 0;
    frame->fin = (buf[pos] & 0x80) ? 1 : 0;
    frame->opcode = (lnet_ws_opcode_t)(buf[pos] & 0x0F);
    pos++;

    frame->masked = (buf[pos] & 0x80) ? 1 : 0;
    size_t payload_len = buf[pos] & 0x7F;
    pos++;

    if (payload_len == 126) {
        if (buf_len < pos + 2) return 0;
        payload_len = ((size_t)buf[pos] << 8) | buf[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        if (buf_len < pos + 8) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | buf[pos + i];
        pos += 8;
    }

    if (frame->masked) {
        if (buf_len < pos + 4) return 0;
        memcpy(frame->mask_key, &buf[pos], 4);
        pos += 4;
    }

    if (buf_len < pos + payload_len) return 0;

    frame->payload = (uint8_t *)malloc(payload_len);
    if (!frame->payload) return -1;
    memcpy(frame->payload, &buf[pos], payload_len);
    frame->payload_len = payload_len;
    pos += payload_len;

    /* 去掩码 */
    if (frame->masked)
        lnet_ws_mask_data(frame->payload, frame->payload_len, frame->mask_key);

    /* 解析关闭码 */
    if (frame->opcode == LNET_WS_OP_CLOSE && frame->payload_len >= 2) {
        frame->close_code = ((int)frame->payload[0] << 8) | frame->payload[1];
    } else {
        frame->close_code = 0;
    }

    return (ssize_t)pos;
}

/**
 * @brief 处理 WebSocket 接收到的帧
 * @param ws WebSocket 对象
 * @param frame 解析出的帧 (payload 会被 ws 接管或释放)
 */
void lnet_websocket_handle_frame(lnet_websocket *ws, const lnet_ws_frame *frame) {
    if (!ws || !frame) return;

    switch (frame->opcode) {
    case LNET_WS_OP_TEXT:
    case LNET_WS_OP_BINARY:
        ws->messages_recv++;
        ws->bytes_recv += frame->payload_len;
        if (ws->on_message)
            ws->on_message(ws, frame->opcode, frame->payload, frame->payload_len, ws->user_data);
        break;

    case LNET_WS_OP_CLOSE: {
        int code = frame->close_code ? frame->close_code : LNET_WS_CLOSE_NORMAL;
        /* 回复关闭帧 */
        if (ws->is_open && !ws->is_closing) {
            ws->is_closing = 1;
            ws->is_open = 0;
            lnet_ws_send_frame(ws, LNET_WS_OP_CLOSE,
                               frame->payload, frame->payload_len >= 2 ? 2 : 0);
        }
        if (ws->on_close) {
            const char *reason = NULL;
            if (frame->payload_len > 2)
                reason = (const char *)(frame->payload + 2);
            ws->on_close(ws, code, reason, ws->user_data);
        }
        break;
    }

    case LNET_WS_OP_PING:
        /* 自动回复 PONG */
        lnet_ws_send_frame(ws, LNET_WS_OP_PONG, frame->payload, frame->payload_len);
        if (ws->on_ping)
            ws->on_ping(ws, frame->payload, frame->payload_len, ws->user_data);
        break;

    case LNET_WS_OP_PONG:
        /* PONG 由应用程序处理或不处理 */
        break;

    default:
        break;
    }
}

/* ========================================================================
 * URL 编解码
 * ======================================================================== */

/**
 * @brief URL 编码
 * @param str 原始字符串
 * @param len 长度 (size_t)-1 表示自动计算
 * @return 编码后的字符串 (调用者负责 free)
 */
char *lnet_url_encode(const char *str, size_t len) {
    if (!str) return NULL;
    if (len == (size_t)-1) len = strlen(str);

    /* 估算最大编码长度 */
    size_t max_len = len * 3 + 1;
    char *result = (char *)malloc(max_len);
    if (!result) return NULL;

    size_t j = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            result[j++] = (char)c;
        } else if (c == ' ') {
            result[j++] = '+';
        } else {
            result[j++] = '%';
            result[j++] = "0123456789ABCDEF"[c >> 4];
            result[j++] = "0123456789ABCDEF"[c & 0x0F];
        }
    }
    result[j] = '\0';
    return result;
}

/**
 * @brief URL 解码
 * @param str 编码字符串
 * @param len 长度 (size_t)-1 表示自动计算
 * @return 解码后的字符串 (调用者负责 free)
 */
char *lnet_url_decode(const char *str, size_t len) {
    if (!str) return NULL;
    if (len == (size_t)-1) len = strlen(str);

    char *result = (char *)malloc(len + 1);
    if (!result) return NULL;

    size_t j = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len && isxdigit((unsigned char)str[i+1]) && isxdigit((unsigned char)str[i+2])) {
            char hex[3] = { str[i+1], str[i+2], '\0' };
            result[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (str[i] == '+') {
            result[j++] = ' ';
        } else {
            result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

/**
 * @brief 解析 URL 到各组成部分
 * @param url URL 字符串
 * @return URL 各部分结构体 (调用者负责通过 lnet_url_parts_free 释放)
 */
lnet_url_parts *lnet_url_parse(const char *url) {
    if (!url) return NULL;

    lnet_url_parts *parts = (lnet_url_parts *)calloc(1, sizeof(lnet_url_parts));
    if (!parts) return NULL;

    parts->raw_url = strdup(url);

    const char *p = url;
    const char *slash;

    /* scheme */
    slash = strstr(p, "://");
    if (slash) {
        size_t scheme_len = (size_t)(slash - p);
        parts->scheme = (char *)malloc(scheme_len + 1);
        if (parts->scheme) {
            memcpy(parts->scheme, p, scheme_len);
            parts->scheme[scheme_len] = '\0';
        }
        p = slash + 3;
    }

    /* userinfo */
    const char *at = strchr(p, '@');
    const char *colon = NULL;
    if (at) {
        size_t ui_len = (size_t)(at - p);
        colon = (const char *)memchr(p, ':', ui_len);
        if (colon) {
            parts->userinfo = (char *)malloc(ui_len + 1);
            if (parts->userinfo) {
                memcpy(parts->userinfo, p, ui_len);
                parts->userinfo[ui_len] = '\0';
            }
        }
        p = at + 1;
    }

    /* host */
    slash = strchr(p, '/');
    colon = strchr(p, ':');

    const char *host_end;
    if (slash && colon && colon < slash) {
        host_end = colon;
    } else if (slash) {
        host_end = slash;
    } else if (colon) {
        host_end = colon;
    } else {
        host_end = p + strlen(p);
    }

    size_t host_len = (size_t)(host_end - p);
    parts->host = (char *)malloc(host_len + 1);
    if (parts->host) {
        memcpy(parts->host, p, host_len);
        parts->host[host_len] = '\0';
    }

    p = host_end;

    /* port */
    if (*p == ':') {
        p++;
        parts->port = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
    }

    /* path */
    if (*p == '/') {
        const char *path_end = p;
        while (*path_end && *path_end != '?' && *path_end != '#') path_end++;
        size_t path_len = (size_t)(path_end - p);
        parts->path = (char *)malloc(path_len + 1);
        if (parts->path) {
            memcpy(parts->path, p, path_len);
            parts->path[path_len] = '\0';
        }
        p = path_end;
    } else if (*p) {
        parts->path = strdup("/");
    }

    /* query */
    if (*p == '?') {
        p++;
        const char *q_end = p;
        while (*q_end && *q_end != '#') q_end++;
        size_t q_len = (size_t)(q_end - p);
        parts->query = (char *)malloc(q_len + 1);
        if (parts->query) {
            memcpy(parts->query, p, q_len);
            parts->query[q_len] = '\0';
        }
        p = q_end;
    }

    /* fragment */
    if (*p == '#') {
        p++;
        parts->fragment = strdup(p);
    }

    return parts;
}

/**
 * @brief 释放 URL 解析结果
 * @param parts URL 各部分
 */
void lnet_url_parts_free(lnet_url_parts *parts) {
    if (!parts) return;
    free(parts->scheme);
    free(parts->host);
    free(parts->path);
    free(parts->query);
    free(parts->fragment);
    free(parts->userinfo);
    free(parts->raw_url);
    free(parts);
}

/* ========================================================================
 * Base64 编解码
 * ======================================================================== */

static const char lnet_b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * @brief Base64 编码
 * @param data 输入数据
 * @param len 输入长度
 * @return 编码结果 (调用者负责 free)
 */
char *lnet_base64_encode(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        char *r = (char *)malloc(1);
        if (r) r[0] = '\0';
        return r;
    }

    size_t out_len = ((len + 2) / 3) * 4 + 1;
    char *out = (char *)malloc(out_len);
    if (!out) return NULL;

    size_t i, j = 0;
    for (i = 0; i < len; i += 3) {
        uint32_t octet_a = i < len ? data[i] : 0;
        uint32_t octet_b = i + 1 < len ? data[i + 1] : 0;
        uint32_t octet_c = i + 2 < len ? data[i + 2] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        out[j++] = lnet_b64_table[(triple >> 18) & 0x3F];
        out[j++] = lnet_b64_table[(triple >> 12) & 0x3F];
        out[j++] = i + 1 < len ? lnet_b64_table[(triple >> 6) & 0x3F] : '=';
        out[j++] = i + 2 < len ? lnet_b64_table[triple & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}

/**
 * @brief 查找 Base64 字符的索引值
 * @param c 字符
 * @return 索引值，-1 表示无效字符
 */
static int lnet_b64_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/**
 * @brief Base64 解码
 * @param str 编码字符串
 * @param out_len 输出长度
 * @return 解码结果 (调用者负责 free)
 */
uint8_t *lnet_base64_decode(const char *str, size_t *out_len) {
    if (!str || !out_len) return NULL;

    size_t slen = strlen(str);
    /* 处理填充 */
    size_t pad = 0;
    if (slen > 0 && str[slen - 1] == '=') pad++;
    if (slen > 1 && str[slen - 2] == '=') pad++;

    size_t out_size = (slen / 4) * 3 - pad;
    uint8_t *out = (uint8_t *)malloc(out_size + 1);
    if (!out) return NULL;

    size_t i, j = 0;
    for (i = 0; i < slen; i += 4) {
        int i0 = lnet_b64_index(str[i]);
        int i1 = i + 1 < slen ? lnet_b64_index(str[i + 1]) : -1;
        int i2 = i + 2 < slen ? lnet_b64_index(str[i + 2]) : -1;
        int i3 = i + 3 < slen ? lnet_b64_index(str[i + 3]) : -1;

        if (i0 < 0 || i1 < 0) break;

        uint32_t triple = ((uint32_t)i0 << 18) + ((uint32_t)i1 << 12);
        if (i2 >= 0) triple += ((uint32_t)i2 << 6);
        if (i3 >= 0) triple += (uint32_t)i3;

        out[j++] = (uint8_t)((triple >> 16) & 0xFF);
        if (i2 >= 0 && j < out_size) out[j++] = (uint8_t)((triple >> 8) & 0xFF);
        if (i3 >= 0 && j < out_size) out[j++] = (uint8_t)(triple & 0xFF);
    }

    out[j] = '\0';
    *out_len = j;
    return out;
}

/* ========================================================================
 * Cookie 工具
 * ======================================================================== */

/**
 * @brief 解析 Cookie 字符串
 * @param cookie_str Cookie 字符串 (如 "name=value; name2=value2")
 * @param cookies 输出 Cookie 数组
 * @param max_cookies 最大 Cookie 数量
 * @return 实际解析的 Cookie 数量
 */
int lnet_cookie_parse(const char *cookie_str, lnet_cookie *cookies, int max_cookies) {
    if (!cookie_str || !cookies || max_cookies <= 0) return 0;

    int count = 0;
    const char *p = cookie_str;

    while (*p && count < max_cookies) {
        /* 跳过空白和分隔符 */
        while (*p == ' ' || *p == ';') p++;
        if (!*p) break;

        /* 查找 = */
        const char *eq = strchr(p, '=');
        if (!eq) break;

        /* 名称 */
        size_t name_len = (size_t)(eq - p);
        /* 去除尾部空格 */
        while (name_len > 0 && p[name_len - 1] == ' ') name_len--;

        cookies[count].name = (char *)malloc(name_len + 1);
        if (!cookies[count].name) break;
        memcpy(cookies[count].name, p, name_len);
        cookies[count].name[name_len] = '\0';

        /* 值 */
        eq++;
        const char *semi = strchr(eq, ';');
        size_t val_len;
        if (semi)
            val_len = (size_t)(semi - eq);
        else
            val_len = strlen(eq);

        cookies[count].value = (char *)malloc(val_len + 1);
        if (!cookies[count].value) {
            free(cookies[count].name);
            break;
        }
        memcpy(cookies[count].value, eq, val_len);
        cookies[count].value[val_len] = '\0';

        /* 初始化其他字段 */
        cookies[count].domain = NULL;
        cookies[count].path = NULL;
        cookies[count].expires = NULL;
        cookies[count].max_age = -1;
        cookies[count].secure = 0;
        cookies[count].http_only = 0;
        cookies[count].same_site = NULL;

        p = semi ? semi : eq + val_len;
        count++;
    }

    return count;
}

/**
 * @brief 构建 Set-Cookie 头字符串
 * @param cookie Cookie 对象
 * @return Set-Cookie 字符串 (调用者负责 free)
 */
char *lnet_cookie_build_set(const lnet_cookie *cookie) {
    if (!cookie || !cookie->name || !cookie->value) return NULL;

    char buf[4096];
    int pos = snprintf(buf, sizeof(buf), "%s=%s", cookie->name, cookie->value);

    if (cookie->domain && cookie->domain[0])
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; Domain=%s", cookie->domain);
    if (cookie->path && cookie->path[0])
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; Path=%s", cookie->path);
    if (cookie->expires && cookie->expires[0])
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; Expires=%s", cookie->expires);
    if (cookie->max_age >= 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; Max-Age=%d", cookie->max_age);
    if (cookie->secure)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; Secure");
    if (cookie->http_only)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; HttpOnly");
    if (cookie->same_site && cookie->same_site[0])
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "; SameSite=%s", cookie->same_site);

    return strdup(buf);
}

/**
 * @brief 构建 Cookie 请求头字符串
 * @param cookies Cookie 数组
 * @param count Cookie 数量
 * @return Cookie 头字符串 (调用者负责 free)
 */
char *lnet_cookie_build_request(const lnet_cookie *cookies, int count) {
    if (!cookies || count <= 0) return NULL;

    size_t total = 0;
    int i;
    for (i = 0; i < count; i++) {
        if (cookies[i].name && cookies[i].value)
            total += strlen(cookies[i].name) + strlen(cookies[i].value) + 3;
    }
    if (total == 0) return NULL;

    char *result = (char *)malloc(total + 1);
    if (!result) return NULL;

    size_t pos = 0;
    int first = 1;
    for (i = 0; i < count; i++) {
        if (cookies[i].name && cookies[i].value) {
            if (!first) {
                result[pos++] = ';';
                result[pos++] = ' ';
            }
            first = 0;
            size_t nlen = strlen(cookies[i].name);
            size_t vlen = strlen(cookies[i].value);
            memcpy(result + pos, cookies[i].name, nlen);
            pos += nlen;
            result[pos++] = '=';
            memcpy(result + pos, cookies[i].value, vlen);
            pos += vlen;
        }
    }
    result[pos] = '\0';
    return result;
}

/* ========================================================================
 * MIME 类型映射
 * ======================================================================== */

/**
 * @brief MIME 类型映射表
 * 常见文件扩展名与 MIME 类型的映射
 */
static const lnet_mime_entry lnet_mime_table[] = {
    {"html",    "text/html; charset=utf-8"},
    {"htm",     "text/html; charset=utf-8"},
    {"css",     "text/css; charset=utf-8"},
    {"js",      "application/javascript; charset=utf-8"},
    {"json",    "application/json; charset=utf-8"},
    {"xml",     "application/xml; charset=utf-8"},
    {"txt",     "text/plain; charset=utf-8"},
    {"csv",     "text/csv; charset=utf-8"},
    {"md",      "text/markdown; charset=utf-8"},
    {"gif",     "image/gif"},
    {"png",     "image/png"},
    {"jpg",     "image/jpeg"},
    {"jpeg",    "image/jpeg"},
    {"bmp",     "image/bmp"},
    {"webp",    "image/webp"},
    {"svg",     "image/svg+xml"},
    {"ico",     "image/x-icon"},
    {"pdf",     "application/pdf"},
    {"zip",     "application/zip"},
    {"gz",      "application/gzip"},
    {"tar",     "application/x-tar"},
    {"rar",     "application/x-rar-compressed"},
    {"7z",      "application/x-7z-compressed"},
    {"mp3",     "audio/mpeg"},
    {"wav",     "audio/wav"},
    {"ogg",     "audio/ogg"},
    {"aac",     "audio/aac"},
    {"mp4",     "video/mp4"},
    {"webm",    "video/webm"},
    {"avi",     "video/x-msvideo"},
    {"mov",     "video/quicktime"},
    {"woff",    "font/woff"},
    {"woff2",   "font/woff2"},
    {"ttf",     "font/ttf"},
    {"otf",     "font/otf"},
    {"wasm",    "application/wasm"},
    {"lua",     "text/x-lua; charset=utf-8"},
    {"c",       "text/x-c; charset=utf-8"},
    {"h",       "text/x-c; charset=utf-8"},
    {"cpp",     "text/x-c++src; charset=utf-8"},
    {"hpp",     "text/x-c++hdr; charset=utf-8"},
    {"py",      "text/x-python; charset=utf-8"},
    {"sh",      "text/x-sh; charset=utf-8"},
    {"bat",     "text/x-bat; charset=utf-8"},
    {"ps1",     "text/x-powershell; charset=utf-8"},
    {"yaml",    "application/x-yaml; charset=utf-8"},
    {"yml",     "application/x-yaml; charset=utf-8"},
    {"toml",    "application/toml; charset=utf-8"},
    {NULL,      NULL}
};

/**
 * @brief 根据文件扩展名获取 MIME 类型
 * @param ext 扩展名 (不含点号，如 "html")
 * @return MIME 类型字符串
 */
const char *lnet_mime_type(const char *ext) {
    if (!ext) return "application/octet-stream";
    const lnet_mime_entry *e;
    for (e = lnet_mime_table; e->extension; e++) {
        if (lnet_strcasecmp(ext, e->extension) == 0)
            return e->mime_type;
    }
    return "application/octet-stream";
}

/**
 * @brief 根据文件路径获取 MIME 类型
 * @param path 文件路径
 * @return MIME 类型字符串
 */
const char *lnet_mime_type_from_path(const char *path) {
    if (!path) return "application/octet-stream";
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    return lnet_mime_type(dot + 1);
}

/* ========================================================================
 * HTTP 状态码描述
 * ======================================================================== */

/**
 * @brief HTTP 状态码描述映射项
 */
typedef struct {
    int code;
    const char *text;
} lnet_status_entry;

static const lnet_status_entry lnet_status_table[] = {
    {100, "Continue"},
    {101, "Switching Protocols"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {204, "No Content"},
    {206, "Partial Content"},
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {413, "Payload Too Large"},
    {414, "URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Range Not Satisfiable"},
    {422, "Unprocessable Entity"},
    {429, "Too Many Requests"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {0, NULL}
};

/**
 * @brief 获取 HTTP 状态码的文字描述
 * @param code HTTP 状态码
 * @return 描述字符串，未知状态码返回 "Unknown"
 */
const char *lnet_http_status_text(int code) {
    const lnet_status_entry *e;
    for (e = lnet_status_table; e->text; e++) {
        if (e->code == code) return e->text;
    }
    return "Unknown";
}

/* ========================================================================
 * 字符串工具 (不区分大小写比较)
 * ======================================================================== */

/**
 * @brief 不区分大小写比较字符串
 * @param a 字符串 A
 * @param b 字符串 B
 * @return 0 相等，非 0 不相等
 */
int lnet_strcasecmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/**
 * @brief 不区分大小写比较字符串 (指定最大比较长度)
 * @param a 字符串 A
 * @param b 字符串 B
 * @param n 最大比较长度
 * @return 0 相等，非 0 不相等
 */
int lnet_strncasecmp(const char *a, const char *b, size_t n) {
    if (a == b || n == 0) return 0;
    if (!a) return -1;
    if (!b) return 1;
    size_t i;
    for (i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* ========================================================================
 * Lua 绑定
 * ======================================================================== */

/** @brief lnet_socket 的 Lua userdata 元表注册键 */
#define LNET_SOCKET_MT "lnet.socket"
/** @brief lnet_http_server 的 Lua userdata 元表注册键 */
#define LNET_SERVER_MT "lnet.http_server"
/** @brief lnet_websocket 的 Lua userdata 元表注册键 */
#define LNET_WEBSOCKET_MT "lnet.websocket"
/** @brief lnet_http_request 的 Lua userdata 元表注册键 */
#define LNET_REQUEST_MT "lnet.http.request"
/** @brief lnet_http_response 的 Lua userdata 元表注册键 */
#define LNET_RESPONSE_MT "lnet.http.response"

/**
 * @brief Lua 路由回调数据
 * 存储 Lua 回调和状态，用于从 C 路由处理器调用 Lua 函数
 */
typedef struct lnet_lua_route_data {
    lua_State *L;          /* Lua 状态 */
    int callback_ref;      /* 注册表中的回调函数引用 */
} lnet_lua_route_data;

/**
 * @brief 请求 userdata 包装
 * 将 C 的 lnet_http_request 封装为 Lua userdata
 */
typedef struct lnet_req_ud {
    lnet_http_request *req;  /* C 请求对象 */
    lua_State *L;            /* Lua 状态 */
} lnet_req_ud;

/**
 * @brief 响应 userdata 包装
 * 将 C 的 lnet_http_response 封装为 Lua userdata，提供可调用的方法
 */
typedef struct lnet_res_ud {
    lnet_http_response *res;  /* C 响应对象 */
    lua_State *L;             /* Lua 状态 */
} lnet_res_ud;

/* ========================================================================
 * Lua 请求 userdata (req) 方法
 * ======================================================================== */

/**
 * @brief 获取请求头的 Lua 方法
 * req:get_header(name) 获取指定请求头的值
 * @param L Lua 状态 (req userdata, name)
 * @return 1 (header 值或 nil)
 */
static int l_http_req_get_header(lua_State *L) {
    lnet_req_ud *ud = (lnet_req_ud *)luaL_checkudata(L, 1, LNET_REQUEST_MT);
    const char *name = luaL_checkstring(L, 2);
    for (int i = 0; i < ud->req->header_count; i++) {
        if (lnet_strcasecmp(ud->req->headers[i].name, name) == 0) {
            lua_pushstring(L, ud->req->headers[i].value);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

/**
 * @brief 获取请求头的 Lua 属性方法 (通过 __index 访问)
 * req.headers 返回所有请求头的 table
 * @param L Lua 状态 (req userdata, key)
 * @return 1 (值)
 */
static int l_http_req_index(lua_State *L) {
    lnet_req_ud *ud = (lnet_req_ud *)luaL_checkudata(L, 1, LNET_REQUEST_MT);
    const char *key = luaL_checkstring(L, 2);

    if (strcmp(key, "url") == 0) {
        lua_pushstring(L, ud->req->url);
    } else if (strcmp(key, "method") == 0) {
        lua_pushstring(L, ud->req->method);
    } else if (strcmp(key, "remote_ip") == 0) {
        lua_pushstring(L, ud->req->remote_ip);
    } else if (strcmp(key, "remote_port") == 0) {
        lua_pushinteger(L, ud->req->remote_port);
    } else if (strcmp(key, "path") == 0) {
        lua_pushstring(L, ud->req->path ? ud->req->path : "");
    } else if (strcmp(key, "query_string") == 0) {
        lua_pushstring(L, ud->req->query_string ? ud->req->query_string : "");
    } else if (strcmp(key, "headers") == 0) {
        /* 返回所有请求头的 table */
        lua_newtable(L);
        for (int i = 0; i < ud->req->header_count; i++) {
            lua_pushstring(L, ud->req->headers[i].value);
            lua_setfield(L, -2, ud->req->headers[i].name);
        }
    } else if (strcmp(key, "body") == 0) {
        if (ud->req->body) {
            lua_pushlstring(L, ud->req->body, ud->req->body_len);
        } else {
            lua_pushliteral(L, "");
        }
    } else if (strcmp(key, "get_header") == 0) {
        lua_pushcfunction(L, l_http_req_get_header);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* --- 请求 userdata 方法注册表 --- */
static const luaL_Reg lnet_req_methods[] = {
    {"get_header",  l_http_req_get_header},
    {NULL, NULL}
};

/* ========================================================================
 * Lua 响应 userdata (res) 方法
 * ======================================================================== */

/**
 * @brief 响应: 写入响应头
 * res:write_head(status_code)
 * res:write_head(status_code, headers_table)
 * @param L Lua 状态 (res userdata, status_code[, headers_table])
 * @return 0
 */
static int l_http_res_write_head(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    int status_code = (int)luaL_checkinteger(L, 2);

    /* 如果第三个参数是 headers table，先设置响应头 */
    if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            const char *hname = lua_tostring(L, -2);
            const char *hvalue = lua_tostring(L, -1);
            if (hname && hvalue) {
                lnet_http_response_set_header(ud->res, hname, hvalue);
            }
            lua_pop(L, 1);
        }
    }

    lnet_http_response_write_head(ud->res, status_code, NULL);
    return 0;
}

/**
 * @brief 响应: 写入响应体数据
 * res:write(data)
 * @param L Lua 状态 (res userdata, data)
 * @return 0
 */
static int l_http_res_write(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    lnet_http_response_write(ud->res, data, len);
    return 0;
}

/**
 * @brief 响应: 结束响应
 * res:finish()
 * @param L Lua 状态 (res userdata)
 * @return 0
 */
static int l_http_res_finish(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    lnet_http_response_end(ud->res);
    return 0;
}

/**
 * @brief 响应: 设置单个响应头
 * res:set_header(name, value)
 * @param L Lua 状态 (res userdata, name, value)
 * @return 0
 */
static int l_http_res_set_header(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    const char *name = luaL_checkstring(L, 2);
    const char *value = luaL_checkstring(L, 3);
    lnet_http_response_set_header(ud->res, name, value);
    return 0;
}

/**
 * @brief 响应: 发送 JSON 响应
 * res:json(status_code, json_str)
 * @param L Lua 状态 (res userdata, status_code, json_str)
 * @return 0
 */
static int l_http_res_json(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    int status_code = (int)luaL_checkinteger(L, 2);
    const char *json_str = luaL_checkstring(L, 3);
    lnet_http_response_json(ud->res, status_code, json_str);
    return 0;
}

/**
 * @brief 响应: 发送错误响应
 * res:error(status_code, message)
 * @param L Lua 状态 (res userdata, status_code, message)
 * @return 0
 */
static int l_http_res_error(lua_State *L) {
    lnet_res_ud *ud = (lnet_res_ud *)luaL_checkudata(L, 1, LNET_RESPONSE_MT);
    int status_code = (int)luaL_checkinteger(L, 2);
    const char *message = luaL_optstring(L, 3, "");
    lnet_http_response_error(ud->res, status_code, message);
    return 0;
}

/**
 * @brief 响应 userdata 的 __index 元方法
 * 支持属性访问和方法调用
 * @param L Lua 状态
 * @return 1 (属性值或方法)
 */
static int l_http_res_index(lua_State *L) {
    (void)luaL_checkudata(L, 1, LNET_RESPONSE_MT);  /* 验证类型 */
    const char *key = luaL_checkstring(L, 2);

    if (strcmp(key, "write_head") == 0) {
        lua_pushcfunction(L, l_http_res_write_head);
    } else if (strcmp(key, "write") == 0) {
        lua_pushcfunction(L, l_http_res_write);
    } else if (strcmp(key, "finish") == 0) {
        lua_pushcfunction(L, l_http_res_finish);
    } else if (strcmp(key, "set_header") == 0) {
        lua_pushcfunction(L, l_http_res_set_header);
    } else if (strcmp(key, "json") == 0) {
        lua_pushcfunction(L, l_http_res_json);
    } else if (strcmp(key, "error") == 0) {
        lua_pushcfunction(L, l_http_res_error);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* --- 响应 userdata 方法注册表 --- */
static const luaL_Reg lnet_res_methods[] = {
    {"write_head",  l_http_res_write_head},
    {"write",       l_http_res_write},
    {"finish",      l_http_res_finish},
    {"set_header",  l_http_res_set_header},
    {"json",        l_http_res_json},
    {"error",       l_http_res_error},
    {NULL, NULL}
};

/* ========================================================================
 * Lua 路由回调 C 包装
 * ======================================================================== */

/**
 * @brief C 路由处理器，将请求转发到 Lua 回调函数
 * 创建请求和响应的 userdata，然后调用 Lua 回调
 * @param req HTTP 请求对象
 * @param res HTTP 响应对象
 * @param user_data lnet_lua_route_data 指针
 */
static void lnet_server_lua_route_handler(lnet_http_request *req,
                                           lnet_http_response *res,
                                           void *user_data) {
    lnet_lua_route_data *rdata = (lnet_lua_route_data *)user_data;
    lua_State *L = rdata->L;

    /* 将 Lua 回调函数压入栈 */
    lua_rawgeti(L, LUA_REGISTRYINDEX, rdata->callback_ref);

    /* 创建请求 userdata */
    lnet_req_ud *req_ud = (lnet_req_ud *)lua_newuserdata(L, sizeof(lnet_req_ud));
    req_ud->req = req;
    req_ud->L = L;
    luaL_getmetatable(L, LNET_REQUEST_MT);
    lua_setmetatable(L, -2);

    /* 创建响应 userdata */
    lnet_res_ud *res_ud = (lnet_res_ud *)lua_newuserdata(L, sizeof(lnet_res_ud));
    res_ud->res = res;
    res_ud->L = L;
    luaL_getmetatable(L, LNET_RESPONSE_MT);
    lua_setmetatable(L, -2);

    /* 调用 Lua 回调: callback(req_ud, res_ud) */
    if (lua_pcall(L, 2, 0, 0) != 0) {
        /* 回调出错时写入错误响应 */
        const char *err = lua_tostring(L, -1);
        if (!err) err = "unknown error";
        lnet_http_response_error(res, 500, err);
        lnet_http_response_end(res);
        lua_pop(L, 1);
    }
}

/* --- Lua Socket 方法 --- */

/**
 * @brief 调用 lnet_socket_connect 的 Lua 包装
 * @param L Lua 状态
 * @return 1 (成功) 或 nil,error (失败)
 */
static int l_http_socket_connect(lua_State *L) {
    lnet_socket *sock = (lnet_socket *)luaL_checkudata(L, 1, LNET_SOCKET_MT);
    const char *host = luaL_checkstring(L, 2);
    int port = luaL_checkinteger(L, 3);
    if (lnet_socket_connect(sock, host, port) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "connect failed");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

/**
 * @brief 调用 lnet_socket_send 的 Lua 包装
 * @param L Lua 状态
 * @return 1 (成功) 或 nil,error (失败)
 */
static int l_http_socket_send(lua_State *L) {
    lnet_socket *sock = (lnet_socket *)luaL_checkudata(L, 1, LNET_SOCKET_MT);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    int n = lnet_socket_send(sock, data, len);
    if (n < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "send failed");
        return 2;
    }
    lua_pushinteger(L, n);
    return 1;
}

/**
 * @brief 调用 lnet_socket_recv 的 Lua 包装
 * @param L Lua 状态
 * @return 1 (数据) 或 nil,error (失败)
 */
static int l_http_socket_recv(lua_State *L) {
    lnet_socket *sock = (lnet_socket *)luaL_checkudata(L, 1, LNET_SOCKET_MT);
    int maxlen = (int)luaL_optinteger(L, 2, LNET_BUFFER_SIZE);
    char *buf = (char *)malloc((size_t)maxlen);
    if (!buf) {
        lua_pushnil(L);
        lua_pushstring(L, "out of memory");
        return 2;
    }
    int n = lnet_socket_recv(sock, buf, (size_t)maxlen);
    if (n <= 0) {
        free(buf);
        lua_pushnil(L);
        lua_pushstring(L, n == 0 ? "connection closed" : "recv failed");
        return 2;
    }
    lua_pushlstring(L, buf, (size_t)n);
    free(buf);
    return 1;
}

/**
 * @brief 关闭 socket
 * @param L Lua 状态
 * @return 0
 */
static int l_http_socket_close(lua_State *L) {
    lnet_socket *sock = (lnet_socket *)luaL_checkudata(L, 1, LNET_SOCKET_MT);
    lnet_socket_free(sock);
    return 0;
}

/* --- Lua HTTP 客户端方法 --- */

/**
 * @brief 执行 HTTP 请求的 Lua 包装
 * @param L Lua 状态
 *   参数: method, url[, body[, config_table]]
 *   config_table 字段: timeout, headers, etc.
 * @return 1 (result table) 或 nil,error
 */
static int l_http_request(lua_State *L) {
    const char *method = luaL_checkstring(L, 1);
    const char *url = luaL_checkstring(L, 2);
    const char *body = NULL;
    size_t body_len = 0;

    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        body = luaL_checklstring(L, 3, &body_len);
    }

    lnet_http_client_config config;
    lnet_http_client_config_default(&config);

    /* 从 table 读取配置 */
    if (lua_gettop(L) >= 4 && lua_istable(L, 4)) {
        lua_getfield(L, 4, "timeout");
        if (lua_isinteger(L, -1)) {
            int t = (int)lua_tointeger(L, -1);
            config.connect_timeout_ms = t;
            config.recv_timeout_ms = t;
            config.send_timeout_ms = t;
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "headers");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                const char *hname = lua_tostring(L, -2);
                const char *hvalue = lua_tostring(L, -1);
                if (hname && hvalue)
                    lnet_http_client_add_header(&config, hname, hvalue);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "user_agent");
        if (lua_isstring(L, -1))
            config.user_agent = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    lnet_http_client_result *result = lnet_http_client_request(method, url, body, body_len, &config);

    /* 释放动态分配的请求头 */
    if (config.extra_headers) {
        for (int i = 0; i < config.extra_header_count; i++) {
            free(config.extra_headers[i].name);
            free(config.extra_headers[i].value);
        }
        free(config.extra_headers);
    }

    if (!result) {
        lua_pushnil(L);
        lua_pushstring(L, "request failed");
        return 2;
    }

    if (result->error) {
        lua_pushnil(L);
        lua_pushstring(L, result->error);
        lnet_http_client_result_free(result);
        return 2;
    }

    /* 构建返回 table */
    lua_newtable(L);
    lua_pushinteger(L, result->status_code);
    lua_setfield(L, -2, "status_code");

    if (result->body) {
        lua_pushlstring(L, result->body, result->body_len);
        lua_setfield(L, -2, "body");
    }

    /* 响应头 */
    lua_newtable(L);
    for (int i = 0; i < result->header_count; i++) {
        lua_pushstring(L, result->headers[i].value);
        lua_setfield(L, -2, result->headers[i].name);
    }
    lua_setfield(L, -2, "headers");

    lnet_http_client_result_free(result);
    return 1;
}

/* --- Lua HTTP 服务器方法 --- */

/**
 * @brief 创建 HTTP 服务器的 Lua 包装
 * @param L Lua 状态
 *   lhttp.server({ host="0.0.0.0", port=8080 })
 * @return 1 (server userdata)
 */
static int l_http_server_new(lua_State *L) {
    lnet_http_server_config config;
    memset(&config, 0, sizeof(config));
    config.bind_host = "0.0.0.0";
    config.port = 8080;
    config.backlog = LNET_SERVER_BACKLOG;
    config.max_connections = 1024;

    if (lua_gettop(L) >= 1 && lua_istable(L, 1)) {
        lua_getfield(L, 1, "host");
        if (lua_isstring(L, -1)) config.bind_host = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "port");
        if (lua_isinteger(L, -1)) config.port = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }

    lnet_http_server *server = lnet_http_server_new(&config);
    if (!server) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create server");
        return 2;
    }

    /* 创建 userdata */
    lnet_http_server **ud = (lnet_http_server **)lua_newuserdata(L, sizeof(lnet_http_server *));
    *ud = server;
    luaL_getmetatable(L, LNET_SERVER_MT);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief 添加路由的 Lua 包装
 * @param L Lua 状态
 *   server:route("GET", "/path", function(req, res) end)
 * @return 1 (server)
 */
static int l_http_server_route(lua_State *L) {
    lnet_http_server **ud = (lnet_http_server **)luaL_checkudata(L, 1, LNET_SERVER_MT);
    lnet_http_server *server = *ud;
    const char *method = luaL_checkstring(L, 2);
    const char *path = luaL_checkstring(L, 3);
    luaL_checktype(L, 4, LUA_TFUNCTION);

    /* 保存 Lua 状态到服务器对象，供后续路由回调使用 */
    server->L = L;

    /* 保存回调函数到注册表 */
    lua_pushvalue(L, 4);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* 创建路由回调数据 */
    lnet_lua_route_data *rdata = (lnet_lua_route_data *)malloc(sizeof(lnet_lua_route_data));
    rdata->L = L;
    rdata->callback_ref = ref;

    /* 将 C 包装函数注册为路由处理器 */
    lnet_http_server_add_route(server, method, path,
        lnet_server_lua_route_handler, (void *)rdata);
    lua_pushvalue(L, 1);
    return 1;
}

/**
 * @brief 启动服务器的 Lua 包装
 * @param L Lua 状态
 * @return 1 (success boolean)
 */
static int l_http_server_start(lua_State *L) {
    lnet_http_server **ud = (lnet_http_server **)luaL_checkudata(L, 1, LNET_SERVER_MT);
    lnet_http_server *server = *ud;

    /* 可选端口参数 */
    if (lua_gettop(L) >= 2 && lua_isinteger(L, 2)) {
        server->config.port = (int)lua_tointeger(L, 2);
    }

    /* 可选主机参数 */
    if (lua_gettop(L) >= 3 && lua_isstring(L, 3)) {
        server->config.bind_host = lua_tostring(L, 3);
    }

    /* 可选回调参数 (兼容旧接口) */
    if (lua_gettop(L) >= 2 && lua_isfunction(L, 2)) {
        /* 保存回调但不阻塞 */
        lua_pushvalue(L, 2);
        /* callback_ref 由 server 持有 */
    }

    int ok = lnet_http_server_start(server);
    lua_pushboolean(L, ok == 0 ? 1 : 0);
    return 1;
}

/**
 * @brief 停止服务器的 Lua 包装
 * @param L Lua 状态
 * @return 0
 */
static int l_http_server_stop(lua_State *L) {
    lnet_http_server **ud = (lnet_http_server **)luaL_checkudata(L, 1, LNET_SERVER_MT);
    lnet_http_server *server = *ud;
    lnet_http_server_stop(server);
    return 0;
}

/* --- Lua URL 工具方法 --- */

/**
 * @brief URL 编码的 Lua 包装
 * @param L Lua 状态
 * @return 1 (encoded string)
 */
static int l_http_url_encode(lua_State *L) {
    size_t len;
    const char *str = luaL_checklstring(L, 1, &len);
    char *encoded = lnet_url_encode(str, len);
    if (!encoded) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, encoded);
    free(encoded);
    return 1;
}

/**
 * @brief URL 解码的 Lua 包装
 * @param L Lua 状态
 * @return 1 (decoded string)
 */
static int l_http_url_decode(lua_State *L) {
    size_t len;
    const char *str = luaL_checklstring(L, 1, &len);
    char *decoded = lnet_url_decode(str, len);
    if (!decoded) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, decoded);
    free(decoded);
    return 1;
}

/**
 * @brief Base64 编码的 Lua 包装
 * @param L Lua 状态
 * @return 1 (base64 string)
 */
static int l_http_base64_encode(lua_State *L) {
    size_t len;
    const char *data = luaL_checklstring(L, 1, &len);
    char *encoded = lnet_base64_encode((const uint8_t *)data, len);
    if (!encoded) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, encoded);
    free(encoded);
    return 1;
}

/**
 * @brief Base64 解码的 Lua 包装
 * @param L Lua 状态
 * @return 1 (decoded string)
 */
static int l_http_base64_decode(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    size_t out_len;
    uint8_t *decoded = lnet_base64_decode(str, &out_len);
    if (!decoded) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char *)decoded, out_len);
    free(decoded);
    return 1;
}

/**
 * @brief URL 解析的 Lua 包装
 * @param L Lua 状态
 * @return 1 (table with scheme, host, port, path, query, fragment)
 */
static int l_http_url_parse(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    lnet_url_parts *parts = lnet_url_parse(url);
    if (!parts) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    if (parts->scheme) { lua_pushstring(L, parts->scheme); lua_setfield(L, -2, "scheme"); }
    if (parts->host)   { lua_pushstring(L, parts->host);   lua_setfield(L, -2, "host"); }
    if (parts->port)   { lua_pushinteger(L, parts->port);   lua_setfield(L, -2, "port"); }
    if (parts->path)   { lua_pushstring(L, parts->path);   lua_setfield(L, -2, "path"); }
    if (parts->query)  { lua_pushstring(L, parts->query);  lua_setfield(L, -2, "query"); }
    if (parts->fragment) { lua_pushstring(L, parts->fragment); lua_setfield(L, -2, "fragment"); }

    lnet_url_parts_free(parts);
    return 1;
}

/* --- Lua Cookie 方法 --- */

/**
 * @brief Cookie 解析的 Lua 包装
 * @param L Lua 状态
 * @return 1 (table of cookies)
 */
static int l_http_cookie_parse(lua_State *L) {
    const char *str = luaL_checkstring(L, 1);
    lnet_cookie cookies[64];
    int n = lnet_cookie_parse(str, cookies, 64);

    lua_newtable(L);
    for (int i = 0; i < n; i++) {
        lua_newtable(L);
        lua_pushstring(L, cookies[i].name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, cookies[i].value);
        lua_setfield(L, -2, "value");
        lua_rawseti(L, -2, i + 1);

        free(cookies[i].name);
        free(cookies[i].value);
    }
    return 1;
}

/* --- Lua MIME 方法 --- */

/**
 * @brief MIME 类型查询的 Lua 包装
 * @param L Lua 状态
 * @return 1 (MIME 类型字符串)
 */
static int l_http_mime_type(lua_State *L) {
    const char *ext_or_path = luaL_checkstring(L, 1);
    const char *mime = lnet_mime_type_from_path(ext_or_path);
    lua_pushstring(L, mime);
    return 1;
}

/* --- Lua WebSocket 方法 --- */

/**
 * @brief WebSocket 连接的 Lua 包装
 * @param L Lua 状态
 * @return 1 (ws userdata) 或 nil,error
 */
static int l_http_websocket_connect(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);
    lnet_websocket *ws = lnet_websocket_connect(url, L);
    if (!ws) {
        lua_pushnil(L);
        lua_pushstring(L, "websocket connect failed");
        return 2;
    }

    lnet_websocket **ud = (lnet_websocket **)lua_newuserdata(L, sizeof(lnet_websocket *));
    *ud = ws;
    luaL_getmetatable(L, LNET_WEBSOCKET_MT);
    lua_setmetatable(L, -2);

    ws->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 1;
}

/**
 * @brief WebSocket 发送文本的 Lua 包装
 * @param L Lua 状态
 * @return 1 (success boolean)
 */
static int l_http_ws_send(lua_State *L) {
    lnet_websocket **ud = (lnet_websocket **)luaL_checkudata(L, 1, LNET_WEBSOCKET_MT);
    size_t len;
    const char *data = luaL_checklstring(L, 2, &len);
    int ok = lnet_websocket_send_text(*ud, data, len);
    lua_pushboolean(L, ok == 0 ? 1 : 0);
    return 1;
}

/**
 * @brief WebSocket 关闭的 Lua 包装
 * @param L Lua 状态
 * @return 0
 */
static int l_http_ws_close(lua_State *L) {
    lnet_websocket **ud = (lnet_websocket **)luaL_checkudata(L, 1, LNET_WEBSOCKET_MT);
    int code = (int)luaL_optinteger(L, 2, LNET_WS_CLOSE_NORMAL);
    const char *reason = luaL_optstring(L, 3, NULL);
    lnet_websocket_close(*ud, code, reason);
    return 0;
}

/* --- Lua MIME 类型查询 --- */

/**
 * @brief 获取 MIME 类型的 Lua 包装
 * @param L Lua 状态
 * @return 1 (MIME 字符串)
 */
static int l_http_get_mime(lua_State *L) {
    const char *ext = luaL_checkstring(L, 1);
    lua_pushstring(L, lnet_mime_type(ext));
    return 1;
}

/* --- 库函数列表 --- */

static const luaL_Reg lnet_socket_methods[] = {
    {"connect", l_http_socket_connect},
    {"send",    l_http_socket_send},
    {"recv",    l_http_socket_recv},
    {"close",   l_http_socket_close},
    {NULL, NULL}
};

static const luaL_Reg lnet_server_methods[] = {
    {"route",   l_http_server_route},
    {"start",   l_http_server_start},
    {"stop",    l_http_server_stop},
    {NULL, NULL}
};

static const luaL_Reg lnet_websocket_methods[] = {
    {"send",    l_http_ws_send},
    {"close",   l_http_ws_close},
    {NULL, NULL}
};

/* --- 子表: http.url --- */
static const luaL_Reg lnet_url_funcs[] = {
    {"encode",   l_http_url_encode},
    {"decode",   l_http_url_decode},
    {"parse",    l_http_url_parse},
    {NULL, NULL}
};

/* --- 子表: http.base64 --- */
static const luaL_Reg lnet_base64_funcs[] = {
    {"encode",   l_http_base64_encode},
    {"decode",   l_http_base64_decode},
    {NULL, NULL}
};

/* --- 子表: http.cookie --- */
static const luaL_Reg lnet_cookie_funcs[] = {
    {"parse",    l_http_cookie_parse},
    {NULL, NULL}
};

/* --- 子表: http.mime --- */
static const luaL_Reg lnet_mime_funcs[] = {
    {"type",     l_http_get_mime},
    {"from_path", l_http_mime_type},
    {NULL, NULL}
};

static const luaL_Reg lnet_http_funcs[] = {
    {"request",         l_http_request},
    {"server",          l_http_server_new},
    {"websocket",       l_http_websocket_connect},
    {NULL, NULL}
};

/**
 * @brief 打开 http 库 (Lua C API)
 * @param L Lua 状态
 * @return 1 (库 table)
 */
LUAMOD_API int luaopen_http(lua_State *L) {
    lnet_global_init();

    /* 注册 socket 元表 */
    luaL_newmetatable(L, LNET_SOCKET_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, lnet_socket_methods, 0);
    lua_pop(L, 1);

    /* 注册服务器元表 */
    luaL_newmetatable(L, LNET_SERVER_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, lnet_server_methods, 0);
    lua_pop(L, 1);

    /* 注册请求元表 (LNET_REQUEST_MT) */
    luaL_newmetatable(L, LNET_REQUEST_MT);
    /* 设置 __index 元方法为 l_http_req_index */
    lua_pushcfunction(L, l_http_req_index);
    lua_setfield(L, -2, "__index");
    /* 同时也注册方法到元表 (允许 req:get_header() 调用) */
    luaL_setfuncs(L, lnet_req_methods, 0);
    lua_pop(L, 1);

    /* 注册响应元表 (LNET_RESPONSE_MT) */
    luaL_newmetatable(L, LNET_RESPONSE_MT);
    /* 设置 __index 元方法为 l_http_res_index */
    lua_pushcfunction(L, l_http_res_index);
    lua_setfield(L, -2, "__index");
    /* 同时也注册方法到元表 (允许 res:write_head() 等调用) */
    luaL_setfuncs(L, lnet_res_methods, 0);
    lua_pop(L, 1);

    /* 注册 WebSocket 元表 */
    luaL_newmetatable(L, LNET_WEBSOCKET_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, lnet_websocket_methods, 0);
    lua_pop(L, 1);

    /* 创建库表 (顶层函数: request, server, websocket) */
    luaL_newlib(L, lnet_http_funcs);

    /* -- http.url 子表 -- */
    luaL_newlib(L, lnet_url_funcs);
    lua_setfield(L, -2, "url");

    /* -- http.base64 子表 -- */
    luaL_newlib(L, lnet_base64_funcs);
    lua_setfield(L, -2, "base64");

    /* -- http.cookie 子表 -- */
    luaL_newlib(L, lnet_cookie_funcs);
    lua_setfield(L, -2, "cookie");

    /* -- http.mime 子表 -- */
    luaL_newlib(L, lnet_mime_funcs);
    lua_setfield(L, -2, "mime");

    return 1;
}
