/**
 * @file lnetwork.h
 * @brief LXCLUA 完整跨平台网络服务库 - 头文件
 * 
 * 提供完整的网络服务支持：
 * - 跨平台 Socket 抽象层 (Windows/Linux/macOS/Android/Emscripten)
 * - TLS/SSL 加密传输 (SChannel / OpenSSL / SecureTransport)
 * - HTTP/HTTPS 客户端 (GET/POST/PUT/DELETE/PATCH/HEAD/OPTIONS)
 * - HTTP/HTTPS 服务器 (路由、中间件、静态文件)
 * - WebSocket 客户端和服务器
 * - URL/Query/Cookie/MIME 工具
 * - 与事件循环 (leventloop) 和 Promise (lpromise) 集成
 *
 * 平台支持：
 * - Windows: Winsock2 + SChannel (TLS)
 * - Linux: BSD Sockets + OpenSSL (TLS)
 * - macOS/iOS: BSD Sockets + SecureTransport (TLS)  
 * - Android: BSD Sockets + OpenSSL (TLS)
 * - Emscripten (WASM): WebSocket API + Fetch API
 */

#ifndef lnetwork_h
#define lnetwork_h

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "leventloop.h"
#include "lthread.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** =====================================================================
** 平台检测和常量定义
** =====================================================================
*/

/* 平台宏标准化 */
#if defined(_WIN32) || defined(_WIN64)
  #define LNET_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
  #define LNET_PLATFORM_ANDROID 1
#elif defined(__linux__)
  #define LNET_PLATFORM_LINUX 1
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define LNET_PLATFORM_IOS 1
  #else
    #define LNET_PLATFORM_MACOS 1
  #endif
#elif defined(__EMSCRIPTEN__)
  #define LNET_PLATFORM_WASM 1
#else
  #define LNET_PLATFORM_POSIX 1
#endif

/* 检测 OpenSSL 可用性 */
#if defined(LNET_PLATFORM_LINUX) || defined(LNET_PLATFORM_ANDROID)
  #if __has_include(<openssl/ssl.h>) && __has_include(<openssl/err.h>)
    #define LNET_HAVE_OPENSSL 1
  #endif
#endif

/* 缓冲区大小 */
#define LNET_BUFFER_SIZE      16384
#define LNET_MAX_HEADERS      64
#define LNET_MAX_URL_LEN      8192
#define LNET_MAX_HEADER_LEN   65536
#define LNET_MAX_BODY_CHUNK   65536
#define LNET_WS_MAX_FRAME     65536
#define LNET_WS_MAX_MESSAGE   (16 * 1024 * 1024)  /* 16MB max message */
#define LNET_SERVER_BACKLOG   128
#define LNET_MAX_ROUTES       256
#define LNET_MAX_MIDDLEWARE   32

/*
** =====================================================================
** Socket 抽象层类型定义
** =====================================================================
*/

#if defined(LNET_PLATFORM_WINDOWS)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  
  #define LNET_SOCKET          SOCKET
  #define LNET_INVALID_SOCKET  INVALID_SOCKET
  #define LNET_SOCKET_ERROR    SOCKET_ERROR
  #define lnet_closesocket(s)  closesocket(s)
  #define lnet_socklen_t       int
  #define lnet_EWOULDBLOCK     WSAEWOULDBLOCK
  #define lnet_EINPROGRESS     WSAEINPROGRESS
  
  /* SChannel TLS */
  #define SECURITY_WIN32
  #include <security.h>
  #include <schannel.h>
  #include <sspi.h>
  
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/time.h>
  #include <errno.h>
  
  #define LNET_SOCKET          int
  #define LNET_INVALID_SOCKET  (-1)
  #define LNET_SOCKET_ERROR    (-1)
  #define lnet_closesocket(s)  close(s)
  #define lnet_socklen_t       socklen_t
  #define lnet_EWOULDBLOCK     EWOULDBLOCK
  #define lnet_EINPROGRESS     EINPROGRESS
  
  #if defined(LNET_HAVE_OPENSSL)
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #include <openssl/bio.h>
  #endif
  
  #if defined(LNET_PLATFORM_MACOS) || defined(LNET_PLATFORM_IOS)
    #include <Security/Security.h>
  #endif
#endif

/* 前向声明 */
struct lnet_context;
struct lnet_socket;
struct lnet_tls;
struct lnet_http_client;
struct lnet_http_server;
struct lnet_http_request;
struct lnet_http_response;
struct lnet_websocket;

/*
** =====================================================================
** 地址族类型 (支持 IPv4/IPv6)
** =====================================================================
*/

typedef enum {
    LNET_AF_INET  = AF_INET,
    LNET_AF_INET6 = AF_INET6,
    LNET_AF_UNSPEC = AF_UNSPEC
} lnet_family_t;

typedef enum {
    LNET_SOCK_STREAM = SOCK_STREAM,
    LNET_SOCK_DGRAM  = SOCK_DGRAM
} lnet_socktype_t;

/*
** =====================================================================
** DNS 解析结果
** =====================================================================
*/

typedef struct lnet_addrinfo {
    char ip[INET6_ADDRSTRLEN];
    int port;
    lnet_family_t family;
    struct lnet_addrinfo *next;
} lnet_addrinfo;

/*
** =====================================================================
** TLS/SSL 抽象层
** =====================================================================
*/

typedef enum {
    LNET_TLS_NONE = 0,       /* 纯文本 */
    LNET_TLS_CLIENT,         /* TLS 客户端 */
    LNET_TLS_SERVER          /* TLS 服务器 */
} lnet_tls_mode_t;

typedef enum {
    LNET_TLS_OK = 0,
    LNET_TLS_WANT_READ,
    LNET_TLS_WANT_WRITE,
    LNET_TLS_ERROR,
    LNET_TLS_CLOSED
} lnet_tls_result_t;

typedef struct lnet_tls_config {
    const char *cert_file;       /* 证书文件路径 (PEM) */
    const char *key_file;        /* 私钥文件路径 (PEM) */
    const char *ca_file;         /* CA 证书文件路径 */
    int verify_peer;             /* 是否验证对端证书 */
    const char *server_name;     /* SNI 主机名 */
} lnet_tls_config;

/*
** =====================================================================
** 通用 Socket 对象
** =====================================================================
*/

typedef struct lnet_socket {
    LNET_SOCKET fd;
    lnet_family_t family;
    int is_nonblocking;
    int is_connected;
    int is_closed;
    int last_error;
    
    /* TLS 层 */
    struct lnet_tls *tls;
    lnet_tls_mode_t tls_mode;
    
    /* 缓冲区 */
    char *read_buf;
    size_t read_buf_size;
    size_t read_buf_pos;
    
    char *write_buf;
    size_t write_buf_size;
    size_t write_buf_pos;
    
    /* 关联的事件循环观察者 */
    event_loop *loop;
    ev_io_watcher io_watcher;
    
    /* 用户数据 */
    void *user_data;
    
    /* Socket 选项 */
    int recv_timeout_ms;
    int send_timeout_ms;
    int keepalive;
    int nodelay;
} lnet_socket;

/*
** =====================================================================
** HTTP 请求对象
** =====================================================================
*/

typedef struct lnet_http_header {
    char *name;
    char *value;
} lnet_http_header;

typedef struct lnet_http_request {
    /* 请求行 */
    char method[16];
    char url[LNET_MAX_URL_LEN];
    char *path;
    char *query_string;
    char *fragment;
    int http_major;
    int http_minor;
    
    /* 请求头 */
    lnet_http_header headers[LNET_MAX_HEADERS];
    int header_count;
    
    /* 请求体 */
    char *body;
    size_t body_len;
    int body_owned;  /* 是否需要释放 body */
    
    /* 连接信息 */
    char remote_ip[INET6_ADDRSTRLEN];
    int remote_port;
    char local_ip[INET6_ADDRSTRLEN];
    int local_port;
    
    /* 已解析的查询参数 */
    int query_parsed;
    
    /* 关联的 socket (用于服务器请求) */
    struct lnet_socket *sock;
    
    /* 用户数据 */
    void *user_data;
} lnet_http_request;

/*
** =====================================================================
** HTTP 响应对象
** =====================================================================
*/

typedef struct lnet_http_response {
    /* 状态行 */
    int status_code;
    char *status_message;
    int http_major;
    int http_minor;
    
    /* 响应头 */
    lnet_http_header headers[LNET_MAX_HEADERS];
    int header_count;
    
    /* 响应体 */
    char *body;
    size_t body_len;
    size_t body_capacity;
    
    /* 是否已发送头部 */
    int headers_sent;
    
    /* Cookie 设置 */
    char **set_cookies;
    int set_cookie_count;
    
    /* 关联的 socket (用于服务器响应) */
    struct lnet_socket *sock;
    
    /* 用户数据 */
    void *user_data;
} lnet_http_response;

/*
** =====================================================================
** HTTP 客户端配置
** =====================================================================
*/

typedef struct lnet_http_client_config {
    int connect_timeout_ms;       /* 连接超时 (默认 10000) */
    int recv_timeout_ms;          /* 接收超时 (默认 30000) */
    int send_timeout_ms;          /* 发送超时 (默认 30000) */
    int max_redirects;            /* 最大重定向次数 (默认 5) */
    int follow_redirects;         /* 是否跟随重定向 (默认 1) */
    int verify_ssl;               /* 是否验证 SSL 证书 (默认 1) */
    int keepalive;                /* 是否保持连接 (默认 0) */
    lnet_tls_config tls_config;   /* TLS 配置 */
    const char *proxy_host;       /* 代理主机 */
    int proxy_port;               /* 代理端口 */
    const char *user_agent;       /* 用户代理 */
    lnet_http_header *extra_headers; /* 额外请求头 */
    int extra_header_count;
} lnet_http_client_config;

/*
** =====================================================================
** HTTP 客户端响应结果
** =====================================================================
*/

typedef struct lnet_http_client_result {
    int status_code;
    char *status_message;
    lnet_http_header *headers;
    int header_count;
    char *body;
    size_t body_len;
    char *redirect_url;
    double total_time_ms;
    double connect_time_ms;
    char *error;
} lnet_http_client_result;

/*
** =====================================================================
** HTTP 服务器路由
** =====================================================================
*/

typedef void (*lnet_route_handler)(struct lnet_http_request *req,
                                    struct lnet_http_response *res,
                                    void *user_data);

typedef void (*lnet_middleware_handler)(struct lnet_http_request *req,
                                         struct lnet_http_response *res,
                                         void *user_data,
                                         void (*next)(void *next_data),
                                         void *next_data);

typedef struct lnet_route {
    char *method;                  /* HTTP 方法或 "*" 表示所有 */
    char *path;                    /* 路径模式 (支持 :param) */
    lnet_route_handler handler;
    void *user_data;
    int is_regex;                  /* 是否为正则路径 */
} lnet_route;

typedef struct lnet_middleware_entry {
    lnet_middleware_handler handler;
    void *user_data;
} lnet_middleware_entry;

/*
** =====================================================================
** HTTP 服务器配置
** =====================================================================
*/

typedef struct lnet_http_server_config {
    const char *bind_host;         /* 绑定地址 (默认 "0.0.0.0") */
    int port;                      /* 绑定端口 (默认 8080) */
    int backlog;                   /* 连接队列 (默认 128) */
    int max_connections;           /* 最大连接数 (默认 1024) */
    int recv_timeout_ms;           /* 接收超时 (默认 30000) */
    int send_timeout_ms;           /* 发送超时 (默认 30000) */
    int keepalive_timeout_ms;      /* Keep-Alive 超时 (默认 5000) */
    int max_body_size;             /* 最大请求体大小 (默认 10MB) */
    int max_headers_size;          /* 最大请求头大小 (默认 65536) */
    int max_url_len;               /* 最大 URL 长度 (默认 8192) */
    int enable_keepalive;          /* 是否启用 Keep-Alive (默认 1) */
    lnet_tls_config tls_config;    /* TLS 配置 */
    event_loop *loop;              /* 关联的事件循环 */
} lnet_http_server_config;

/*
** =====================================================================
** HTTP 服务器对象
** =====================================================================
*/

typedef struct lnet_http_server {
    lnet_http_server_config config;
    lnet_socket *listen_sock;
    
    /* 路由表 */
    lnet_route routes[LNET_MAX_ROUTES];
    int route_count;
    
    /* 中间件链 */
    lnet_middleware_entry middlewares[LNET_MAX_MIDDLEWARE];
    int middleware_count;
    
    /* 活动连接 */
    struct lnet_server_conn *connections;
    int connection_count;
    int max_connections;
    l_mutex_t conn_lock;
    
    /* 默认处理器 */
    lnet_route_handler default_handler;
    void *default_handler_data;
    
    /* 事件循环 */
    event_loop *loop;
    
    /* 状态 */
    int running;
    int should_stop;
    
    /* Lua 状态 (用于路由回调) */
    struct lua_State *L;

    /* 接受线程 (无事件循环时使用) */
    l_thread_t accept_thread;
    int accept_thread_running;

    /* 统计 */
    uint64_t total_requests;
    uint64_t total_connections;
    uint64_t active_requests;
} lnet_http_server;

/*
** =====================================================================
** WebSocket 帧类型
** =====================================================================
*/

typedef enum {
    LNET_WS_OP_CONTINUATION = 0x0,
    LNET_WS_OP_TEXT         = 0x1,
    LNET_WS_OP_BINARY       = 0x2,
    LNET_WS_OP_CLOSE        = 0x8,
    LNET_WS_OP_PING         = 0x9,
    LNET_WS_OP_PONG         = 0xA
} lnet_ws_opcode_t;

typedef enum {
    LNET_WS_CLOSE_NORMAL         = 1000,
    LNET_WS_CLOSE_GOING_AWAY     = 1001,
    LNET_WS_CLOSE_PROTOCOL_ERROR = 1002,
    LNET_WS_CLOSE_UNSUPPORTED    = 1003,
    LNET_WS_CLOSE_NO_STATUS      = 1005,
    LNET_WS_CLOSE_ABNORMAL       = 1006,
    LNET_WS_CLOSE_INVALID_DATA   = 1007,
    LNET_WS_CLOSE_POLICY         = 1008,
    LNET_WS_CLOSE_TOO_LARGE      = 1009,
    LNET_WS_CLOSE_EXTENSION      = 1010,
    LNET_WS_CLOSE_UNEXPECTED     = 1011
} lnet_ws_close_code_t;

typedef struct lnet_ws_frame {
    int fin;                       /* 是否为最后一帧 */
    lnet_ws_opcode_t opcode;
    int masked;                    /* 是否被掩码 (客户端必须 mask) */
    uint8_t mask_key[4];
    uint8_t *payload;
    size_t payload_len;
    int close_code;               /* 关闭帧的状态码 */
} lnet_ws_frame;

/*
** =====================================================================
** WebSocket 对象
** =====================================================================
*/

typedef void (*lnet_ws_on_message)(struct lnet_websocket *ws, 
                                    lnet_ws_opcode_t opcode,
                                    const uint8_t *data, size_t len,
                                    void *user_data);
typedef void (*lnet_ws_on_close)(struct lnet_websocket *ws,
                                  int code, const char *reason,
                                  void *user_data);
typedef void (*lnet_ws_on_error)(struct lnet_websocket *ws,
                                  const char *error,
                                  void *user_data);
typedef void (*lnet_ws_on_open)(struct lnet_websocket *ws,
                                 void *user_data);
typedef void (*lnet_ws_on_ping)(struct lnet_websocket *ws,
                                 const uint8_t *data, size_t len,
                                 void *user_data);

typedef struct lnet_websocket {
    lnet_socket *sock;
    int is_client;                 /* 1=客户端 0=服务器 */
    int is_open;
    int is_closing;
    
    /* 回调 */
    lnet_ws_on_message on_message;
    lnet_ws_on_close   on_close;
    lnet_ws_on_error   on_error;
    lnet_ws_on_open    on_open;
    lnet_ws_on_ping    on_ping;
    void *user_data;
    
    /* 接收缓冲区 (用于分帧重组) */
    uint8_t *recv_buf;
    size_t recv_buf_len;
    size_t recv_buf_cap;
    lnet_ws_opcode_t recv_opcode;
    
    /* 发送队列 */
    struct lnet_ws_send_item *send_queue_head;
    struct lnet_ws_send_item *send_queue_tail;
    int sending;
    
    /* URL / 路径 */
    char *url;
    char *path;
    char *protocol;
    
    /* 定时器 (心跳) */
    ev_timer ping_timer;
    int ping_interval_sec;
    
    /* 统计 */
    uint64_t messages_sent;
    uint64_t messages_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    
    /* 关联的 Lua 状态 */
    lua_State *L;
    int lua_ref;                   /* 注册表引用 */
} lnet_websocket;

/*
** =====================================================================
** Cookie 结构体
** =====================================================================
*/

typedef struct lnet_cookie {
    char *name;
    char *value;
    char *domain;
    char *path;
    char *expires;
    int max_age;
    int secure;
    int http_only;
    char *same_site;               /* "Strict" | "Lax" | "None" */
} lnet_cookie;

/*
** =====================================================================
** MIME 类型映射
** =====================================================================
*/

typedef struct lnet_mime_entry {
    const char *extension;
    const char *mime_type;
} lnet_mime_entry;

/*
** =====================================================================
** URL 解析结果
** =====================================================================
*/

typedef struct lnet_url_parts {
    char *scheme;
    char *host;
    int port;
    char *path;
    char *query;
    char *fragment;
    char *userinfo;
    char *raw_url;
} lnet_url_parts;

/*
** =====================================================================
** 全局初始化
** =====================================================================
*/

/**
 * @brief 初始化网络库 (自动调用，可多次调用)
 */
void lnet_global_init(void);

/**
 * @brief 清理网络库
 */
void lnet_global_cleanup(void);

/*
** =====================================================================
** Socket API
** =====================================================================
*/

/**
 * @brief 创建新的 socket
 * @param family 地址族 (LNET_AF_INET / LNET_AF_INET6)
 * @param type   Socket 类型
 * @param nonblocking 是否非阻塞
 * @return 新 socket，失败返回 NULL
 */
lnet_socket *lnet_socket_new(lnet_family_t family, lnet_socktype_t type, int nonblocking);

/**
 * @brief 关闭并释放 socket
 * @param sock Socket 对象
 */
void lnet_socket_free(lnet_socket *sock);

/**
 * @brief 连接到远程地址
 * @param sock Socket
 * @param host 主机名或 IP
 * @param port 端口
 * @return 0 成功，-1 失败
 */
int lnet_socket_connect(lnet_socket *sock, const char *host, int port);

/**
 * @brief 绑定到本地地址
 * @param sock Socket
 * @param host 绑定地址
 * @param port 端口
 * @return 0 成功，-1 失败
 */
int lnet_socket_bind(lnet_socket *sock, const char *host, int port);

/**
 * @brief 开始监听
 * @param sock Socket
 * @param backlog 连接队列大小
 * @return 0 成功，-1 失败
 */
int lnet_socket_listen(lnet_socket *sock, int backlog);

/**
 * @brief 接受新连接
 * @param sock 监听 socket
 * @return 新 socket，失败返回 NULL
 */
lnet_socket *lnet_socket_accept(lnet_socket *sock);

/**
 * @brief 发送数据 (保证完整发送)
 * @param sock Socket
 * @param data 数据
 * @param len 长度
 * @return 已发送字节数，-1 失败
 */
int lnet_socket_send(lnet_socket *sock, const void *data, size_t len);

/**
 * @brief 接收数据
 * @param sock Socket
 * @param buf 缓冲区
 * @param len 最大长度
 * @return 已接收字节数，0=连接关闭，-1=错误
 */
int lnet_socket_recv(lnet_socket *sock, void *buf, size_t len);

/**
 * @brief 设置 socket 选项
 * @param sock Socket
 * @param keepalive 是否启用 TCP Keep-Alive
 * @param nodelay 是否禁用 Nagle 算法
 * @param recv_timeout_ms 接收超时
 * @param send_timeout_ms 发送超时
 */
void lnet_socket_setopts(lnet_socket *sock, int keepalive, int nodelay,
                         int recv_timeout_ms, int send_timeout_ms);

/**
 * @brief 从 socket 读取一行 (以 \r\n 结束)
 * @param sock Socket
 * @param buf 缓冲区
 * @param max_len 最大长度
 * @return 读取的字符数 (不含 \r\n)，-1 失败或超时
 */
int lnet_socket_readline(lnet_socket *sock, char *buf, size_t max_len);

/**
 * @brief 获取对端地址信息
 * @param sock Socket
 * @param ip 输出 IP 缓冲区
 * @param ip_size IP 缓冲区大小
 * @param port 输出端口
 * @return 0 成功，-1 失败
 */
int lnet_socket_getpeername(lnet_socket *sock, char *ip, size_t ip_size, int *port);

/**
 * @brief 获取本地地址信息
 * @param sock Socket
 * @param ip 输出 IP 缓冲区
 * @param ip_size IP 缓冲区大小
 * @param port 输出端口
 * @return 0 成功，-1 失败
 */
int lnet_socket_getsockname(lnet_socket *sock, char *ip, size_t ip_size, int *port);

/*
** =====================================================================
** DNS API
** =====================================================================
*/

/**
 * @brief DNS 解析主机名
 * @param host 主机名
 * @param port 端口
 * @param family 地址族偏好
 * @return 地址信息链表，调用者负责释放
 */
lnet_addrinfo *lnet_resolve(const char *host, int port, lnet_family_t family);

/**
 * @brief 释放 DNS 解析结果
 * @param info 地址信息链表
 */
void lnet_free_addrinfo(lnet_addrinfo *info);

/*
** =====================================================================
** TLS API
** =====================================================================
*/

/**
 * @brief 初始化 TLS 上下文 (全局)
 * @return 0 成功，-1 失败
 */
int lnet_tls_init(void);

/**
 * @brief 清理 TLS 全局上下文
 */
void lnet_tls_cleanup(void);

/**
 * @brief 为 socket 开启 TLS 客户端模式
 * @param sock Socket
 * @param config TLS 配置
 * @return 0 成功，-1 失败
 */
int lnet_tls_connect(lnet_socket *sock, const lnet_tls_config *config);

/**
 * @brief 为已连接的 socket 开启 TLS 服务器模式
 * @param sock Socket
 * @param config TLS 配置
 * @return 0 成功，-1 失败
 */
int lnet_tls_accept(lnet_socket *sock, const lnet_tls_config *config);

/**
 * @brief TLS 握手 (非阻塞)
 * @param sock Socket
 * @return LNET_TLS_OK / LNET_TLS_WANT_READ / LNET_TLS_WANT_WRITE / LNET_TLS_ERROR
 */
lnet_tls_result_t lnet_tls_handshake(lnet_socket *sock);

/**
 * @brief TLS 加密写入
 * @param sock Socket
 * @param data 数据
 * @param len 长度
 * @return 已发送字节数，-1 失败
 */
int lnet_tls_write(lnet_socket *sock, const void *data, size_t len);

/**
 * @brief TLS 解密读取
 * @param sock Socket
 * @param buf 缓冲区
 * @param len 最大长度
 * @return 已读取字节数，0=连接关闭，-1=需要更多数据
 */
int lnet_tls_read(lnet_socket *sock, void *buf, size_t len);

/**
 * @brief 关闭 TLS 连接
 * @param sock Socket
 */
void lnet_tls_shutdown(lnet_socket *sock);

/**
 * @brief 通过 TLS 发送完整数据
 * @param sock Socket
 * @param data 数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_socket_send_all(lnet_socket *sock, const void *data, size_t len);

/*
** =====================================================================
** HTTP 客户端 API
** =====================================================================
*/

/**
 * @brief 执行 HTTP 请求
 * @param method HTTP 方法
 * @param url 请求 URL
 * @param body 请求体 (可为 NULL)
 * @param body_len 请求体长度
 * @param config 客户端配置 (可为 NULL 使用默认)
 * @return 请求结果，调用者负责释放
 */
lnet_http_client_result *lnet_http_client_request(const char *method, const char *url,
                                            const char *body, size_t body_len,
                                            const lnet_http_client_config *config);

/**
 * @brief 释放 HTTP 客户端请求结果
 * @param result 请求结果
 */
void lnet_http_client_result_free(lnet_http_client_result *result);

/**
 * @brief 创建默认 HTTP 客户端配置
 * @param config 输出配置
 */
void lnet_http_client_config_default(lnet_http_client_config *config);

/**
 * @brief 向配置添加额外请求头
 * @param config 配置
 * @param name 头名称
 * @param value 头值
 * @return 0 成功，-1 头数量超限
 */
int lnet_http_client_add_header(lnet_http_client_config *config,
                                 const char *name, const char *value);

/*
** =====================================================================
** HTTP 服务器 API
** =====================================================================
*/

/**
 * @brief 创建 HTTP 服务器
 * @param config 服务器配置 (可为 NULL 使用默认)
 * @return 服务器对象
 */
lnet_http_server *lnet_http_server_new(const lnet_http_server_config *config);

/**
 * @brief 释放 HTTP 服务器
 * @param server 服务器对象
 */
void lnet_http_server_free(lnet_http_server *server);

/**
 * @brief 添加路由处理器
 * @param server 服务器
 * @param method HTTP 方法 ("GET", "POST" 等)
 * @param path 路径模式
 * @param handler 处理器函数
 * @param user_data 用户数据
 * @return 0 成功，-1 路由表满
 */
int lnet_http_server_add_route(lnet_http_server *server, const char *method,
                                const char *path, lnet_route_handler handler,
                                void *user_data);

/**
 * @brief 添加中间件
 * @param server 服务器
 * @param handler 中间件函数
 * @param user_data 用户数据
 * @return 0 成功，-1 中间件链满
 */
int lnet_http_server_add_middleware(lnet_http_server *server,
                                     lnet_middleware_handler handler,
                                     void *user_data);

/**
 * @brief 设置默认路由处理器 (404 等)
 * @param server 服务器
 * @param handler 处理器
 * @param user_data 用户数据
 */
void lnet_http_server_set_default_handler(lnet_http_server *server,
                                           lnet_route_handler handler,
                                           void *user_data);

/**
 * @brief 启动 HTTP 服务器
 * @param server 服务器
 * @return 0 成功，-1 失败
 */
int lnet_http_server_start(lnet_http_server *server);

/**
 * @brief 停止 HTTP 服务器
 * @param server 服务器
 */
void lnet_http_server_stop(lnet_http_server *server);

/*
** =====================================================================
** HTTP 请求 API (服务器端)
** =====================================================================
*/

/**
 * @brief 获取请求头值
 * @param req 请求对象
 * @param name 头名称 (不区分大小写)
 * @return 头值，未找到返回 NULL
 */
const char *lnet_http_request_get_header(lnet_http_request *req, const char *name);

/**
 * @brief 解析 URL 查询字符串
 * @param req 请求对象
 * @param key 参数名
 * @param value 输出参数值 (可为 NULL)
 * @return 0 找到，-1 未找到
 */
int lnet_http_request_get_query(lnet_http_request *req, const char *key, const char **value);

/**
 * @brief 释放请求对象
 * @param req 请求
 */
void lnet_http_request_free(lnet_http_request *req);

/*
** =====================================================================
** HTTP 响应 API (服务器端)
** =====================================================================
*/

/**
 * @brief 初始化响应对象
 * @param res 响应对象
 * @param sock 关联的 socket
 */
void lnet_http_response_init(lnet_http_response *res, lnet_socket *sock);

/**
 * @brief 设置响应头
 * @param res 响应对象
 * @param name 头名称
 * @param value 头值
 */
void lnet_http_response_set_header(lnet_http_response *res,
                                    const char *name, const char *value);

/**
 * @brief 获取响应头值
 * @param res 响应对象
 * @param name 头名称
 * @return 头值，未找到返回 NULL
 */
const char *lnet_http_response_get_header(lnet_http_response *res, const char *name);

/**
 * @brief 设置 Cookie
 * @param res 响应对象
 * @param cookie Cookie 对象
 */
void lnet_http_response_set_cookie(lnet_http_response *res, const lnet_cookie *cookie);

/**
 * @brief 写入响应头 (发送前调用一次)
 * @param res 响应对象
 * @param status_code HTTP 状态码
 * @param status_message 状态文本 (可为 NULL 自动推断)
 * @return 0 成功，-1 失败
 */
int lnet_http_response_write_head(lnet_http_response *res, int status_code,
                                   const char *status_message);

/**
 * @brief 写入响应体数据
 * @param res 响应对象
 * @param data 数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_http_response_write(lnet_http_response *res, const void *data, size_t len);

/**
 * @brief 写入格式化数据到响应体
 * @param res 响应对象
 * @param fmt 格式化字符串
 * @return 0 成功，-1 失败
 */
int lnet_http_response_writef(lnet_http_response *res, const char *fmt, ...);

/**
 * @brief 结束响应 (发送所有缓冲数据)
 * @param res 响应对象
 * @return 0 成功，-1 失败
 */
int lnet_http_response_end(lnet_http_response *res);

/**
 * @brief 发送完整响应 (一次性)
 * @param res 响应对象
 * @param status_code 状态码
 * @param body 响应体
 * @param body_len 长度
 * @param content_type 内容类型
 * @return 0 成功，-1 失败
 */
int lnet_http_response_send(lnet_http_response *res, int status_code,
                             const void *body, size_t body_len,
                             const char *content_type);

/**
 * @brief 发送 JSON 响应
 * @param res 响应对象
 * @param status_code 状态码
 * @param json JSON 字符串
 * @return 0 成功，-1 失败
 */
int lnet_http_response_json(lnet_http_response *res, int status_code,
                             const char *json);

/**
 * @brief 发送错误响应
 * @param res 响应对象
 * @param status_code 状态码
 * @param message 错误消息
 * @return 0 成功，-1 失败
 */
int lnet_http_response_error(lnet_http_response *res, int status_code,
                              const char *message);

/**
 * @brief 发送文件响应
 * @param res 响应对象
 * @param filepath 文件路径
 * @return 0 成功，-1 失败
 */
int lnet_http_response_send_file(lnet_http_response *res, const char *filepath);

/**
 * @brief 释放响应对象
 * @param res 响应对象
 */
void lnet_http_response_free(lnet_http_response *res);

/*
** =====================================================================
** WebSocket API
** =====================================================================
*/

/**
 * @brief 创建 WebSocket 客户端连接
 * @param url WebSocket URL (ws:// 或 wss://)
 * @param L Lua 状态 (用于回调)
 * @return WebSocket 对象，失败返回 NULL
 */
lnet_websocket *lnet_websocket_connect(const char *url, lua_State *L);

/**
 * @brief 从已有 HTTP 请求升级到 WebSocket (服务器端)
 * @param req HTTP 升级请求
 * @param res HTTP 响应
 * @param L Lua 状态
 * @return WebSocket 对象，失败返回 NULL
 */
lnet_websocket *lnet_websocket_upgrade(lnet_http_request *req,
                                        lnet_http_response *res,
                                        lua_State *L);

/**
 * @brief 发送 WebSocket 文本消息
 * @param ws WebSocket
 * @param data 文本数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_websocket_send_text(lnet_websocket *ws, const char *data, size_t len);

/**
 * @brief 发送 WebSocket 二进制消息
 * @param ws WebSocket
 * @param data 二进制数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_websocket_send_binary(lnet_websocket *ws, const uint8_t *data, size_t len);

/**
 * @brief 发送 WebSocket Ping
 * @param ws WebSocket
 * @param data 可选数据
 * @param len 长度
 * @return 0 成功，-1 失败
 */
int lnet_websocket_ping(lnet_websocket *ws, const uint8_t *data, size_t len);

/**
 * @brief 关闭 WebSocket 连接
 * @param ws WebSocket
 * @param code 关闭码
 * @param reason 关闭原因
 */
void lnet_websocket_close(lnet_websocket *ws, int code, const char *reason);

/**
 * @brief 释放 WebSocket 对象
 * @param ws WebSocket
 */
void lnet_websocket_free(lnet_websocket *ws);

/**
 * @brief 处理 WebSocket 数据帧 (内部使用)
 * @param ws WebSocket
 * @param frame 解析出的帧
 */
void lnet_websocket_handle_frame(lnet_websocket *ws, const lnet_ws_frame *frame);

/*
** =====================================================================
** URL 工具 API
** =====================================================================
*/

/**
 * @brief URL 编码
 * @param str 原始字符串
 * @param len 长度 (-1 自动计算)
 * @return 编码后的字符串 (调用者负责 free)
 */
char *lnet_url_encode(const char *str, size_t len);

/**
 * @brief URL 解码
 * @param str 编码字符串
 * @param len 长度 (-1 自动计算)
 * @return 解码后的字符串 (调用者负责 free)
 */
char *lnet_url_decode(const char *str, size_t len);

/**
 * @brief 解析 URL
 * @param url URL 字符串
 * @return URL 各部分，调用者负责释放
 */
lnet_url_parts *lnet_url_parse(const char *url);

/**
 * @brief 释放 URL 解析结果
 * @param parts URL 各部分
 */
void lnet_url_parts_free(lnet_url_parts *parts);

/**
 * @brief Base64 编码
 * @param data 输入数据
 * @param len 输入长度
 * @return 编码结果 (调用者负责 free)
 */
char *lnet_base64_encode(const uint8_t *data, size_t len);

/**
 * @brief Base64 解码
 * @param str 编码字符串
 * @param out_len 输出长度
 * @return 解码结果 (调用者负责 free)
 */
uint8_t *lnet_base64_decode(const char *str, size_t *out_len);

/*
** =====================================================================
** Cookie 工具 API
** =====================================================================
*/

/**
 * @brief 解析 Cookie 字符串
 * @param cookie_str Cookie 字符串
 * @param cookies 输出 Cookie 数组 (调用者负责释放)
 * @param max_cookies 最大 Cookie 数
 * @return 实际解析的 Cookie 数
 */
int lnet_cookie_parse(const char *cookie_str, lnet_cookie *cookies, int max_cookies);

/**
 * @brief 构建 Set-Cookie 字符串
 * @param cookie Cookie 对象
 * @return Set-Cookie 字符串 (调用者负责 free)
 */
char *lnet_cookie_build_set(const lnet_cookie *cookie);

/**
 * @brief 构建 Cookie 请求头字符串
 * @param cookies Cookie 数组
 * @param count Cookie 数量
 * @return Cookie 头字符串 (调用者负责 free)
 */
char *lnet_cookie_build_request(const lnet_cookie *cookies, int count);

/*
** =====================================================================
** MIME 类型 API
** =====================================================================
*/

/**
 * @brief 根据文件扩展名获取 MIME 类型
 * @param ext 扩展名 (不含点号，如 "html")
 * @return MIME 类型字符串，未找到返回 "application/octet-stream"
 */
const char *lnet_mime_type(const char *ext);

/**
 * @brief 根据文件路径获取 MIME 类型
 * @param path 文件路径
 * @return MIME 类型字符串
 */
const char *lnet_mime_type_from_path(const char *path);

/*
** =====================================================================
** 工具函数
** =====================================================================
*/

/**
 * @brief 获取常见的 HTTP 状态码描述
 * @param code 状态码
 * @return 描述字符串
 */
const char *lnet_http_status_text(int code);

/**
 * @brief 生成 WebSocket 握手 Accept 值
 * @param key Sec-WebSocket-Key 值
 * @return Accept 值 (调用者负责 free)
 */
char *lnet_ws_compute_accept(const char *key);

/**
 * @brief 生成随机 WebSocket Key
 * @param buf 输出缓冲区
 * @param len 缓冲区大小 (最少 25)
 */
void lnet_ws_generate_key(char *buf, size_t len);

/**
 * @brief 生成随机掩码 key (4 字节)
 * @param mask_key 输出缓冲区 (4 字节)
 */
void lnet_ws_generate_mask(uint8_t mask_key[4]);

/**
 * @brief 对 WebSocket 数据进行掩码/去掩码操作
 * @param data 数据
 * @param len 长度
 * @param mask_key 掩码 key (4 字节)
 */
void lnet_ws_mask_data(uint8_t *data, size_t len, const uint8_t mask_key[4]);

/**
 * @brief 不区分大小写比较字符串
 * @param a 字符串 A
 * @param b 字符串 B
 * @return 0 相等
 */
int lnet_strcasecmp(const char *a, const char *b);

/**
 * @brief 不区分大小写比较字符串 (指定长度)
 * @param a 字符串 A
 * @param b 字符串 B
 * @param n 最大比较长度
 * @return 0 相等
 */
int lnet_strncasecmp(const char *a, const char *b, size_t n);

/*
** =====================================================================
** Lua 绑定 API
** =====================================================================
*/

/**
 * @brief 打开 http 库 (Lua C API)
 * @param L Lua 状态
 * @return 1
 */
LUAMOD_API int luaopen_http(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif /* lnetwork_h */