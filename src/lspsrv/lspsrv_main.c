/*
** LXCLUA Language Server - Main Entry Point
** Reads JSON-RPC messages from stdin, processes them, writes responses to stdout.
** 
** The LSP protocol communicates via stdin/stdout using HTTP-style framing:
**   Content-Length: <N>\r\n\r\n<JSON-RPC body>
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include "lspsrv.h"

/* ---- Platform Detection ---- */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#define IS_WINDOWS 1
#else
#include <unistd.h>
#define IS_WINDOWS 0
#endif

/* ---- Global Server State ---- */
static void *g_server = NULL;
static int g_running = 1;

/*
 * @brief 信号处理函数，优雅退出
 * @param sig 信号编号
 */
static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/*
 * @brief 设置信号处理器
 */
static void setup_signal_handlers(void) {
#ifndef _WIN32
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
#else
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
#endif
}

/*
 * @brief 将错误信息写入stderr（不干扰LSP通信）
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
static void log_to_stderr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

#ifdef _WIN32
/*
 * @brief Windows: 从stdin读取一个完整的LSP消息帧
 * 使用 Windows ReadFile API 避免 C 运行时的缓冲问题
 * @param out_data 输出-消息正文
 * @param out_len 输出-正文长度
 * @return 0成功，-1读取错误，-2客户端关闭
 */
static int read_lsp_message(char **out_data, int *out_len) {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    char header_buf[256];
    int header_pos = 0;
    int content_length = -1;
    
    /* 逐字节读取HTTP风格的头部 */
    while (header_pos < 255) {
        char c;
        DWORD nread = 0;
        if (!ReadFile(hStdin, &c, 1, &nread, NULL) || nread == 0) {
            return -2; /* Client closed or pipe error */
        }
        header_buf[header_pos++] = c;
        header_buf[header_pos] = '\0';
        
        /* 检查是否到header结束（\r\n\r\n 或 \n\n） */
        if (header_pos >= 4 && 
            ((header_buf[header_pos-4] == '\r' && header_buf[header_pos-3] == '\n' &&
              header_buf[header_pos-2] == '\r' && header_buf[header_pos-1] == '\n') ||
             (header_buf[header_pos-2] == '\n' && header_buf[header_pos-1] == '\n'))) {
            break;
        }
    }
    
    /* 解析Content-Length */
    header_buf[header_pos] = '\0';
    const char *cl_pos = strstr(header_buf, "Content-Length:");
    if (!cl_pos) cl_pos = strstr(header_buf, "content-length:");
    if (cl_pos) {
        content_length = atoi(cl_pos + 15);
    }
    
    if (content_length <= 0 || content_length > 100 * 1024 * 1024) {
        log_to_stderr("ERROR: Invalid Content-Length: %d", content_length);
        return -1;
    }
    
    /* 读取消息正文 */
    char *body = (char *)malloc(content_length + 1);
    if (!body) {
        log_to_stderr("ERROR: malloc failed for %d bytes", content_length);
        return -1;
    }
    
    DWORD bytes_read = 0;
    while (bytes_read < (DWORD)content_length) {
        DWORD n = 0;
        if (!ReadFile(hStdin, body + bytes_read, content_length - bytes_read, &n, NULL) || n == 0) {
            log_to_stderr("ERROR: read body failed after %lu/%d bytes", bytes_read, content_length);
            free(body);
            return -2;
        }
        bytes_read += n;
    }
    
    body[content_length] = '\0';
    *out_data = body;
    *out_len = content_length;
    return 0;
}

/*
 * @brief Windows: 向stdout写入LSP响应消息
 * data 已由 jrpc_serialize 格式化为 "Content-Length: N\r\n\r\n{json}" 
 * 直接写入即可，无需再加帧头
 * @param data 已格式化的响应字符串
 * @return 0成功，-1失败
 */
static int write_lsp_message(const char *data) {
    if (!data) return 0;
    
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    size_t body_len = strlen(data);
    DWORD written = 0;
    WriteFile(hStdout, data, (DWORD)body_len, &written, NULL);
    FlushFileBuffers(hStdout);
    return 0;
}

#else
/*
 * @brief Unix: 从stdin读取一个完整的LSP消息帧
 * @param out_data 输出-消息正文
 * @param out_len 输出-正文长度
 * @return 0成功，-1读取错误，-2客户端关闭
 */
static int read_lsp_message(char **out_data, int *out_len) {
    char header_buf[256];
    int header_pos = 0;
    int content_length = -1;
    
    /* 读取HTTP风格的头部 */
    while (header_pos < 255) {
        int c = getc(stdin);
        if (c == EOF || c < 0) {
            return -2; /* Client closed */
        }
        header_buf[header_pos++] = (char)c;
        header_buf[header_pos] = '\0';
        
        /* 检查是否到header结束（\r\n\r\n 或 \n\n） */
        if (header_pos >= 4 && 
            ((header_buf[header_pos-4] == '\r' && header_buf[header_pos-3] == '\n' &&
              header_buf[header_pos-2] == '\r' && header_buf[header_pos-1] == '\n') ||
             (header_buf[header_pos-2] == '\n' && header_buf[header_pos-1] == '\n'))) {
            break;
        }
    }
    
    /* 解析Content-Length */
    header_buf[header_pos] = '\0';
    const char *cl_pos = strstr(header_buf, "Content-Length:");
    if (!cl_pos) cl_pos = strstr(header_buf, "content-length:");
    if (cl_pos) {
        content_length = atoi(cl_pos + 15);
    }
    
    if (content_length <= 0 || content_length > 100 * 1024 * 1024) {
        return -1;
    }
    
    /* 读取消息正文 */
    char *body = (char *)malloc(content_length + 1);
    if (!body) return -1;
    
    int bytes_read = 0;
    while (bytes_read < content_length) {
        int n = (int)read(0, body + bytes_read, content_length - bytes_read);
        if (n <= 0) {
            free(body);
            return -2;
        }
        bytes_read += n;
    }
    
    body[content_length] = '\0';
    *out_data = body;
    *out_len = content_length;
    return 0;
}

/*
 * @brief Unix: 向stdout写入LSP响应消息
 * data 已由 jrpc_serialize 格式化为 "Content-Length: N\r\n\r\n{json}"
 * 直接写入即可，无需再加帧头
 * @param data 已格式化的响应字符串
 * @return 0成功，-1失败
 */
static int write_lsp_message(const char *data) {
    if (!data) return 0;
    
    size_t body_len = strlen(data);
    write(1, data, body_len);
    fflush(stdout);
    return 0;
}

#endif

/*
 * @brief 主事件循环
 * 持续从stdin读取JSON-RPC消息，处理并响应
 * @return 退出码
 */
static int main_loop(void) {
    log_to_stderr("[LXCLUA-LSP] Server starting...");
    
    while (g_running) {
        char *data = NULL;
        int data_len = 0;
        
        int rc = read_lsp_message(&data, &data_len);
        if (rc == -2) {
            /* Client closed connection */
            log_to_stderr("[LXCLUA-LSP] Client disconnected");
            break;
        }
        if (rc == -1) {
            /* Error reading - try to continue */
            continue;
        }
        
        /* Process message */
        log_to_stderr("[LXCLUA-LSP] Received %d bytes", data_len);
        char *response = NULL;
        int has_response = lsp_handle_message(g_server, data, data_len, &response);
        log_to_stderr("[LXCLUA-LSP] handle_message returned %d, response_len=%d", has_response, response ? (int)strlen(response) : 0);
        if (has_response && response) {
            log_to_stderr("[LXCLUA-LSP] Sending response...");
            write_lsp_message(response);
            free(response);
        }
        free(data);
        
        /* Check if exit was requested */
        LspServer *srv = (LspServer *)g_server;
        if (srv->exit_requested) {
            log_to_stderr("[LXCLUA-LSP] Exit requested by client");
            break;
        }
    }
    
    log_to_stderr("[LXCLUA-LSP] Server stopped");
    return 0;
}

/*
 * @brief 程序入口点
 * 初始化LSP服务器并启动消息循环。
 * 
 * 使用方式（VS Code配置示例）：
 *   "languages": [{
 *     "id": "lxclua",
 *     "extensions": [".lua", ".lxclua"],
 *     "configuration": "./language-configuration.json"
 *   }],
 *   "servers": {
 *     "lxclua-lsp": {
 *       "command": "lxclua-lsp",
 *       "args": []
 *     }
 *   }
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    /* 设置信号处理 */
    setup_signal_handlers();
    
    /* 将stderr用于日志，stdin/stdout用于LSP通信 */
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    
    /* 初始化LSP服务器 */
    g_server = lsp_init();
    if (!g_server) {
        log_to_stderr("FATAL: Failed to initialize LSP server");
        return 1;
    }
    
    /* 启动主事件循环 */
    int exit_code = main_loop();
    
    /* 清理资源 */
    lsp_srv_free(g_server);
    g_server = NULL;
    
    return exit_code;
}