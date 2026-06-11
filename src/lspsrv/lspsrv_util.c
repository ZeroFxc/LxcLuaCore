/*
** LXCLUA LSP - Utility functions
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "lspsrv.h"

/* ---- Memory Management ---- */

/*
 * @brief 分配内存，带完整性检查
 * @param sz 分配大小（字节）
 * @return 分配的内存指针
 */
void *lsp_alloc(size_t sz) {
    void *p = malloc(sz);
    if (!p) {
        fprintf(stderr, "[LSP] FATAL: malloc(%zu) failed\n", sz);
        exit(1);
    }
    memset(p, 0, sz);
    return p;
}

/*
 * @brief 重新分配内存
 * @param p 原内存指针
 * @param sz 新大小
 * @return 重新分配的内存指针
 */
void *lsp_realloc(void *p, size_t sz) {
    void *np = realloc(p, sz);
    if (!np) {
        fprintf(stderr, "[LSP] FATAL: realloc(%zu) failed\n", sz);
        exit(1);
    }
    return np;
}

/*
 * @brief 释放内存
 * @param p 内存指针
 */
void lsp_free(void *p) {
    if (p) free(p);
}

/*
 * @brief 复制字符串（分配新内存）
 * @param s 源字符串
 * @return 新分配字符串副本，若s为NULL则返回NULL
 */
char *lsp_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)lsp_alloc(len + 1);
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

/*
 * @brief 安全字符串拼接（动态分配）
 * @param dst 目标字符串指针（将被释放并更新）
 * @param src 要追加的源字符串
 */
char *lsp_str_append(char *dst, const char *src) {
    if (!src || !*src) return dst;
    if (!dst) return lsp_strdup(src);
    size_t dlen = strlen(dst);
    size_t slen = strlen(src);
    dst = (char *)lsp_realloc(dst, dlen + slen + 1);
    memcpy(dst + dlen, src, slen);
    dst[dlen + slen] = 0;
    return dst;
}

/*
 * @brief 使用 vsnprintf 格式化字符串
 * @param fmt 格式化字符串
 * @param ap 可变参数列表
 * @return 格式化后新分配的字符串
 */
char *lsp_vfmt(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    char *buf = (char *)lsp_alloc(n + 1);
    vsnprintf(buf, n + 1, fmt, ap);
    return buf;
}

/*
 * @brief 格式化字符串
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 格式化后新分配的字符串
 */
char *lsp_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = lsp_vfmt(fmt, ap);
    va_end(ap);
    return s;
}

/* ---- Line/Offset utilities ---- */

/*
 * @brief 将字节偏移量转换为行号和列号
 * @param text 文档文本
 * @param offset 字节偏移量
 * @param line 输出-行号（0为起始）
 * @param col 输出-列号（0为起始）
 * @return 0成功，-1失败
 */
int lsp_offset_to_linecol(const char *text, int offset, int *line, int *col) {
    if (!text || offset < 0) return -1;
    int l = 0, c = 0;
    for (int i = 0; i < offset && text[i]; i++) {
        if (text[i] == '\r' && text[i+1] == '\n') { i++; l++; c = 0; continue; }
        if (text[i] == '\n') { l++; c = 0; continue; }
        c++;
    }
    *line = l;
    *col = c;
    return 0;
}

/*
 * @brief 将行号和列号转换为字节偏移量
 * @param text 文档文本
 * @param line 行号（0为起始）
 * @param col 列号（0为起始）
 * @return 字节偏移量，-1表示失败
 */
int lsp_linecol_to_offset(const char *text, int line, int col) {
    if (!text) return -1;
    int l = 0, c = 0, offset = 0;
    while (text[offset]) {
        if (l == line && c == col) return offset;
        if (text[offset] == '\r' && text[offset+1] == '\n') { offset++; l++; c = 0; offset++; continue; }
        if (text[offset] == '\n') { l++; c = 0; offset++; continue; }
        c++;
        offset++;
    }
    if (l == line && c == col) return offset;
    return offset; /* clamp to end */
}

/*
 * @brief 判断字符是否为标识符字符（字母、数字、下划线）
 * @param c 字符（ASCII或扩展）
 * @return 1是，0否
 */
int lsp_is_ident_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/*
 * @brief 判断字符是否为标识符起始字符
 * @param c 字符
 * @return 1是，0否
 */
int lsp_is_ident_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/*
 * @brief 获取指定偏移量处的单词及其边界
 * @param text 文本
 * @param offset 偏移量（在此单词内部或边缘）
 * @param start 输出-单词起始偏移量
 * @param end 输出-单词结束偏移量
 * @return 单词字符串（新分配），若不在单词上则返回NULL
 */
char *lsp_get_word_at(const char *text, int offset, int *start, int *end) {
    if (!text || offset < 0 || offset >= (int)strlen(text)) return NULL;
    /* 向左边查找单词起始 */
    int s = offset;
    while (s > 0 && lsp_is_ident_char((unsigned char)text[s-1])) s--;
    /* 向右边查找单词结束 */
    int e = offset;
    while (text[e] && lsp_is_ident_char((unsigned char)text[e])) e++;
    if (s >= e) return NULL;
    *start = s;
    *end = e;
    int len = e - s;
    char *word = (char *)lsp_alloc(len + 1);
    memcpy(word, text + s, len);
    word[len] = 0;
    return word;
}

/*
 * @brief 计算文本文档的行偏移量缓存
 * @param text 文本
 * @param len 文本长度
 * @param out_offsets 输出-行偏移量数组
 * @param out_nlines 输出-行数
 */
void lsp_build_line_offsets(const char *text, int len, int **out_offsets, int *out_nlines) {
    int cap = 64;
    int *offsets = (int *)lsp_alloc(cap * sizeof(int));
    int nlines = 0;
    offsets[nlines++] = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (nlines >= cap) { cap *= 2; offsets = lsp_realloc(offsets, cap * sizeof(int)); }
            offsets[nlines++] = i + 1;
        } else if (text[i] == '\r' && i+1 < len && text[i+1] == '\n') {
            if (nlines >= cap) { cap *= 2; offsets = lsp_realloc(offsets, cap * sizeof(int)); }
            i++;
            offsets[nlines++] = i + 1;
        }
    }
    *out_offsets = offsets;
    *out_nlines = nlines;
}

/*
 * @brief 获取某偏移量所在行的文本
 * @param text 文档文本
 * @param offset 字节偏移量
 * @return 行文本（新分配），调用者需要释放
 */
char *lsp_get_line_text(const char *text, int offset) {
    if (!text) return NULL;
    int line_start = offset;
    while (line_start > 0 && text[line_start-1] != '\n') line_start--;
    int line_end = offset;
    while (text[line_end] && text[line_end] != '\r' && text[line_end] != '\n') line_end++;
    int len = line_end - line_start;
    char *line = (char *)lsp_alloc(len + 1);
    memcpy(line, text + line_start, len);
    line[len] = 0;
    return line;
}