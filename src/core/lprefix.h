/*
** $Id: lprefix.h $
** Definitions for Lua code that must come before any other header file
** See Copyright Notice in lua.h
*/

#ifndef lprefix_h
#define lprefix_h

/*
** 日志开关宏
**  默认全部关闭（空操作，零开销）
**  需要调试时，编译时加 -DLUA_ENABLE_LOGGING 即可开启
*/
#if !defined(LUA_ENABLE_LOGGING)
  /* === 默认：关闭所有日志 === */
  #define LUA_LOGI(...)           ((void)0)
  #define LUA_LOGD(...)           ((void)0)
  #define LUA_LOGW(...)           ((void)0)
  #define LUA_LOGE(...)           ((void)0)
  #define LUA_LOGI_VA(fmt, ap)    ((void)0)
#else
  /* === 开启日志模式（调试用） === */
  #if defined(__ANDROID__)
    #include <stdio.h>
    #include <stdarg.h>
    #ifdef __cplusplus
    extern "C" {
    #endif
    extern FILE *gLuaLogFile;
    extern void lua_log_init(void);
    extern void lua_log_write(const char *prefix, const char *fmt, ...);
    #ifdef __cplusplus
    }
    #endif
    #define LUA_LOGI(...)  do { if (gLuaLogFile) lua_log_write("[I]", __VA_ARGS__); } while(0)
    #define LUA_LOGD(...)  do { if (gLuaLogFile) lua_log_write("[D]", __VA_ARGS__); } while(0)
    #define LUA_LOGW(...)  do { if (gLuaLogFile) lua_log_write("[W]", __VA_ARGS__); } while(0)
    #define LUA_LOGE(...)  do { if (gLuaLogFile) lua_log_write("[E]", __VA_ARGS__); } while(0)
    #define LUA_LOGI_VA(fmt, ap)  do { if (gLuaLogFile) { \
      char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, ap); \
      fprintf(gLuaLogFile, "[I] %s\n", buf); fflush(gLuaLogFile); } } while(0)
  #else
    #include <stdio.h>
    #define LUA_LOGI(...)  do { fprintf(stdout, "[LXCLua] " __VA_ARGS__); fprintf(stdout, "\n"); fflush(stdout); } while(0)
    #define LUA_LOGD(...)  do { fprintf(stdout, "[LXCLua:D] " __VA_ARGS__); fprintf(stdout, "\n"); fflush(stdout); } while(0)
    #define LUA_LOGW(...)  do { fprintf(stderr, "[LXCLua:W] " __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)
    #define LUA_LOGE(...)  do { fprintf(stderr, "[LXCLua:E] " __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while(0)
    #define LUA_LOGI_VA(fmt, ap)  do { vfprintf(stdout, fmt, ap); } while(0)
  #endif
#endif

/*
** Allows POSIX/XSI stuff
*/
#if !defined(LUA_USE_C89)	/* { */

#if defined(LUA_USE_MACOSX)
#define _DARWIN_C_SOURCE
#endif

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE           600
#elif _XOPEN_SOURCE == 0
#undef _XOPEN_SOURCE  /* use -D_XOPEN_SOURCE=0 to undefine it */
#endif

/*
** Allows manipulation of large files in gcc and some other compilers
*/
#if !defined(LUA_32BITS) && !defined(_FILE_OFFSET_BITS)
#define _LARGEFILE_SOURCE       1
#define _FILE_OFFSET_BITS       64
#endif

#endif				/* } */


/*
** Windows stuff
*/
#if defined(_WIN32)	/* { */

#if !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS  /* avoid warnings about ISO C functions */
#endif

#endif			/* } */


/*
** ARM64-safe strcmp - Android bionic 的 NEON 向量化 strcmp 对
** TString 字符数据（不保证 8 字节对齐）会触发 SIGSEGV/bus error/hang
** 在 Android 平台上用宏劫持所有 strcmp/strncmp 调用到安全版本。
**
** 关键：必须在 #include <string.h> 之后再 #define strcmp，否则 <string.h> 内部的
** strcmp 声明会被宏替换，破坏函数声明语法。
*/
#if defined(__ANDROID__)
  /* 先 include <string.h> 以确保 strcmp 的原型声明完整（不受宏影响） */
  #include <string.h>
  /* 然后劫持 strcmp/strncmp 宏 */
  #define strcmp(s1, s2)     safe_strcmp((s1), (s2))
  #define strncmp(s1, s2, n) safe_strncmp((s1), (s2), (n))
  /* 前置声明安全版本的函数原型（定义在 lstate.c 中） */
  #ifdef __cplusplus
  extern "C" {
  #endif
  extern int safe_strcmp(const char *s1, const char *s2);
  extern int safe_strncmp(const char *s1, const char *s2, size_t n);
  #ifdef __cplusplus
  }
  #endif
#endif

#endif
