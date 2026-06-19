/* PCRE2 config.h - 为 LXCLUA 定制的配置 */

/* 8位字符宽度支持 */
#define SUPPORT_PCRE2_8 1

/* Unicode 支持 */
#define SUPPORT_UNICODE 1

/* JIT 编译支持 */
#define SUPPORT_JIT 1

/* 标准头文件 */
#define HAVE_ASSERT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCHAR_H 1

/* Windows 平台 */
#if defined(_WIN32)
#define HAVE_WINDOWS_H 1
#endif

/* 换行符默认值 (LF) */
#define NEWLINE_DEFAULT 2

/* 链接大小 */
#define LINK_SIZE 2

/* 限制参数 */
#define MATCH_LIMIT 10000000
#define MATCH_LIMIT_DEPTH MATCH_LIMIT
#define HEAP_LIMIT 20000000
#define PARENS_NEST_LIMIT 250
#define MAX_NAME_COUNT 10000
#define MAX_NAME_SIZE 128
#define MAX_VARLOOKBEHIND 255

/* 包信息 */
#define PACKAGE "pcre2"
#define PACKAGE_NAME "PCRE2"
#define PACKAGE_STRING "PCRE2 10.47"
#define PACKAGE_TARNAME "pcre2"
#define PACKAGE_VERSION "10.47"

/* 版本信息 */
#define PCRE2_MAJOR 10
#define PCRE2_MINOR 47
#define PCRE2_DATE 2025-10-21

/* 静态链接 */
#define PCRE2_STATIC 1
#define PCRE2_EXPORT

/* 不使用 Valgrind */
/* #undef SUPPORT_VALGRIND */

/* 不使用 BSR_ANYCRLF */
/* #undef BSR_ANYCRLF */

/* 不使用 EBCDIC */
/* #undef EBCDIC */