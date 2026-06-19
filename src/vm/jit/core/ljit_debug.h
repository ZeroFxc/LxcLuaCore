#ifndef LJIT_DEBUG_H
#define LJIT_DEBUG_H

#include <stdio.h>
#include <time.h>

/*
 * 统一调试输出宏
 * 格式: [时间戳] [模块标识] 具体内容
 * 用法: JIT_DBG(MOD, "fmt", ...)
 */

/* 获取当前时间戳字符串 (HH:MM:SS.mmm) */
static inline const char *jit_timestamp(void) {
    static char buf[32];
    static clock_t last_clock = 0;
    static double last_sec = 0.0;
    clock_t now = clock();
    if (now != last_clock) {
        last_clock = now;
        last_sec = (double)now / (double)CLOCKS_PER_SEC;
    }
    int total_sec = (int)last_sec;
    int ms = (int)((last_sec - total_sec) * 1000);
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
    return buf;
}

#ifdef JIT_VERBOSE_LOG
#define JIT_DBG(mod, fmt, ...) \
    do { \
        fprintf(stderr, "[%s] [%s] " fmt "\n", jit_timestamp(), mod, ##__VA_ARGS__); \
        fflush(stderr); \
    } while(0)
#else
#define JIT_DBG(mod, fmt, ...) do {} while(0)
#endif

/* 模块标识常量 */
#define MOD_CORE      "JIT"
#define MOD_CTL       "JIT-CTL"
#define MOD_TR        "JIT-TR"
#define MOD_ANALYZE   "JIT-ANA"
#define MOD_OPT       "JIT-OPT"
#define MOD_OPT_CONST "JIT-OPT-CONST"
#define MOD_OPT_CSE   "JIT-OPT-CSE"
#define MOD_OPT_DCE   "JIT-OPT-DCE"
#define MOD_OPT_PEEP  "JIT-OPT-PEEP"
#define MOD_OPT_INLINE "JIT-OPT-INLINE"
#define MOD_CG        "JIT-CG"
#define MOD_CG_ARITH  "JIT-CG-ARITH"
#define MOD_CG_CTRL   "JIT-CG-CTRL"
#define MOD_CG_CALL   "JIT-CG-CALL"
#define MOD_CG_TABLE  "JIT-CG-TABLE"
#define MOD_CG_CONV   "JIT-CG-CONV"
#define MOD_CG_CLOS   "JIT-CG-CLOS"
#define MOD_CG_OOP    "JIT-CG-OOP"
#define MOD_DBG       "JIT-DBG"
#define MOD_REG       "JIT-REG"
#define MOD_REG_LIVE  "JIT-REG-LIVE"
#define MOD_REG_GRAPH "JIT-REG-GRAPH"
#define MOD_REG_COLOR "JIT-REG-COLOR"
#define MOD_REG_SPILL "JIT-REG-SPILL"
#define MOD_IR        "JIT-IR"
#define MOD_IR_LIST   "JIT-IR-LIST"
#define MOD_IR_LABEL  "JIT-IR-LABEL"
#define MOD_IR_BB     "JIT-IR-BB"

#endif /* LJIT_DEBUG_H */