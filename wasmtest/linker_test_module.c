/**
 * linker_test_module.c — 测试 wasmtime linker:defineFunc + linker:instantiate
 *
 * 从 "env" 模块导入 3 个自定义 host 函数:
 *   env.host_log(ptr, len)   → 无返回值 — host 端打印日志
 *   env.host_fetch(kind, url_ptr, url_len, buf_ptr, buf_max) → i32 — host 获取数据
 *   env.host_compute(kind, x, y) → f64 — host 端复杂计算
 *
 * 编译:
 *   emcc linker_test_module.c -o linker_test_module.wasm --no-entry \
 *       -s STANDALONE_WASM=1 \
 *       -s EXPORTED_FUNCTIONS='["_test_log","_test_fetch","_test_compute","_get_buffer","_get_url"]' \
 *       -O2
 */

#include <stddef.h>

/* ---- 从 "env" 模块导入 ---- */

__attribute__((import_module("env"), import_name("host_log")))
extern void env_host_log(int ptr, int len);

__attribute__((import_module("env"), import_name("host_fetch")))
extern int env_host_fetch(int kind, int url_ptr, int url_len, int buf_ptr, int buf_max);

__attribute__((import_module("env"), import_name("host_compute")))
extern double env_host_compute(int kind, double x, double y);

/* ---- 内部缓冲区(线性内存中的全局数组) ---- */

static char g_buffer[256];  /* 通用缓冲区 */
static char g_url[128];     /* URL 字符串 */

/**
 * @brief test_log() → i32
 * 向 host 发送一条日志消息, 返回 0 表示成功
 */
int test_log(void) {
    /* 构造日志消息 */
    g_buffer[0] = 'H'; g_buffer[1] = 'e'; g_buffer[2] = 'l'; g_buffer[3] = 'l';
    g_buffer[4] = 'o'; g_buffer[5] = ' '; g_buffer[6] = 'f'; g_buffer[7] = 'r';
    g_buffer[8] = 'o'; g_buffer[9] = 'm'; g_buffer[10] = ' '; g_buffer[11] = 'W';
    g_buffer[12] = 'A'; g_buffer[13] = 'S'; g_buffer[14] = 'M'; g_buffer[15] = '!';

    /* 调用 host import: 日志消息 = buffer 的前 16 字节 */
    env_host_log((int)(size_t)g_buffer, 16);
    return 0;
}

/**
 * @brief test_fetch() → i32
 * 构造一个模拟 URL, 调用 env.host_fetch 并返回获取的数据长度
 * @return 获取的字节数, -1 表示失败
 */
int test_fetch(void) {
    /* 构造 URL: "https://api.example.com/data" */
    const char *url = "https://api.example.com/data";
    int url_len = 0;
    while (url[url_len] && url_len < 127) {
        g_url[url_len] = url[url_len];
        url_len++;
    }
    g_url[url_len] = '\0';

    /* 调用 host import: kind=0 表示 GET 请求 */
    int result = env_host_fetch(0, (int)(size_t)g_url, url_len,
                                 (int)(size_t)g_buffer, 256);
    return result;
}

/**
 * @brief test_compute(kind, x, y) → f64
 * 委托 host 端进行复杂计算
 * @param kind 计算类型: 0=add, 1=pow, 2=hypot
 * @param x 参数1
 * @param y 参数2
 * @return 计算结果
 */
double test_compute(int kind, double x, double y) {
    return env_host_compute(kind, x, y);
}

/**
 * @brief get_buffer() → i32
 * 返回内部缓冲区的地址(线性内存偏移量)
 */
int get_buffer(void) {
    return (int)(size_t)g_buffer;
}

/**
 * @brief get_url() → i32
 * 返回 URL 缓冲区的地址
 */
int get_url(void) {
    return (int)(size_t)g_url;
}