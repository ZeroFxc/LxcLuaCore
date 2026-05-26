/**
 * test_wasm_fullhost.c — 覆盖所有可从 C 调用的 host import
 *
 * 测试 lwasmtime.c 中所有不需要 anyref 的 host 回调:
 *   host.math(10种) host.math2(3种) host.fmt(5种模式)
 *   host.os_time host.os_time_table host.os_clock host.os_tmpname host.os_exit
 *   host.fs_seek(无效fd) host.fs_close(无效fd) host.read(stub)
 *
 * 通过 wasmtime.runLua2wasm() 运行, 结果通过导出函数验证。
 *
 * 编译:
 *   emcc test_wasm_fullhost.c -o test_wasm_fullhost.wasm --no-entry \
 *       -s STANDALONE_WASM=1 \
 *       -s EXPORTED_FUNCTIONS='["_main","_get_result","_get_int_result","_verify_all"]' \
 *       -O2
 */

#include <math.h>

/* ---- 从 "host" 模块导入 ---- */

__attribute__((import_module("host"), import_name("math")))
extern double host_math(int kind, double x);

__attribute__((import_module("host"), import_name("math2")))
extern double host_math2(int kind, double x, double y);

__attribute__((import_module("host"), import_name("fmt")))
extern int host_fmt(int kind, long long iv, double fv, int prec);

__attribute__((import_module("host"), import_name("os_time")))
extern long long host_os_time(void);

__attribute__((import_module("host"), import_name("os_time_table")))
extern long long host_os_time_table(long long y, long long mo, long long d,
                                     long long h, long long mi, long long s);

__attribute__((import_module("host"), import_name("os_clock")))
extern double host_os_clock(void);

__attribute__((import_module("host"), import_name("os_tmpname")))
extern int host_os_tmpname(void);

__attribute__((import_module("host"), import_name("os_exit")))
extern void host_os_exit(int code, int has_code);

__attribute__((import_module("host"), import_name("fs_seek")))
extern long long host_fs_seek(int fd, int whence, long long offset);

__attribute__((import_module("host"), import_name("fs_close")))
extern int host_fs_close(int fd);

__attribute__((import_module("host"), import_name("read")))
extern int host_read(int mode, int count);

/* ---- 全局结果存储 ---- */

/* 10 个 math 结果: idx 0..9 */
static double g_math[10];
static int    g_math_err;   /* bitmask of failures */

/* 3 个 math2 结果 */
static double g_math2[3];
static int    g_math2_err;

/* 5 个 fmt 结果 (字节数) */
static int    g_fmt_len[5];
static int    g_fmt_err;

/* 时间相关 */
static long long g_time;       /* os_time */
static double    g_clock;      /* os_clock */
static long long g_time_tbl;   /* os_time_table(2026,5,25,12,0,0) 应 > 0 */
static int       g_tmpname_len;/* os_tmpname 返回的字节数 */

/* fs 错误处理 */
static int       g_seek_err;   /* fs_seek 无效 fd, 预期 -1 */
static int       g_close_err;  /* fs_close 无效 fd, 预期 -1 */

/* read stub */
static int       g_read_result;/* read stub 预期 -1 (EOF) */

/**
 * @brief main() — 被 runLua2wasm 自动调用
 * 依次测试所有 host import, 结果存入全局变量
 */
void main(void) {
    int i, total;

    /* ============================================
     * 1. host.math: 全部 10 种运算
     *    kind: 0=sin 1=cos 2=tan 3=asin 4=acos 5=atan
     *          6=exp 7=log 8=log2 9=log10
     *    测试值: x = 0.5
     * ============================================ */
    double test_x = 0.5;
    for (i = 0; i < 10; i++) {
        g_math[i] = host_math(i, test_x);
    }
    g_math_err = 0;
    /* sin(0.5)≈0.4794, cos(0.5)≈0.8776, tan(0.5)≈0.5463, exp(0.5)≈1.6487 */
    /* log(0.5)≈-0.6931, log2(0.5)=-1.0, log10(0.5)≈-0.3010 */
    if (fabs(g_math[0] - 0.4794255) > 0.001)  g_math_err |= 1;
    if (fabs(g_math[1] - 0.8775825) > 0.001)  g_math_err |= 2;
    if (fabs(g_math[6] - 1.6487212) > 0.001)  g_math_err |= 64;
    if (fabs(g_math[8] + 1.0) > 0.001)         g_math_err |= 256;

    /* ============================================
     * 2. host.math2: 全部 3 种运算
     *    kind: 0=atan2 1=pow 2=fmod
     * ============================================ */
    g_math2[0] = host_math2(0, 1.0, 1.0);        /* atan2(1,1) = pi/4 ≈ 0.785398 */
    g_math2[1] = host_math2(1, 2.0, 10.0);       /* pow(2,10) = 1024 */
    g_math2[2] = host_math2(2, 17.0, 5.0);       /* fmod(17,5) = 2.0 */
    g_math2_err = 0;
    if (fabs(g_math2[0] - 0.785398) > 0.001) g_math2_err |= 1;
    if (g_math2[1] != 1024.0)                 g_math2_err |= 2;
    if (g_math2[2] != 2.0)                    g_math2_err |= 4;

    /* ============================================
     * 3. host.fmt: 全部 5 种格式化
     *    kind: 0=%d(i64) 2=%g(f64) 3=%f 4=%e 5=%x
     * ============================================ */
    total = 0;
    total += host_fmt(0, -12345LL, 0.0, 0);       /* %d: "-12345" → 6 */
    total += host_fmt(2, 0LL, 3.14159, 4);          /* %g: "3.142" → 5 */
    total += host_fmt(3, 0LL, 2.5, 2);              /* %f: "2.50" → 4 */
    total += host_fmt(4, 0LL, 1.0, 3);              /* %e: "1.000e+00" → 10 */
    total += host_fmt(5, 0xDEADBEEFLL, 0.0, 0);     /* %x: "deadbeef" → 8 */
    g_fmt_len[0] = total;  /* 总写入字节数 = 6+5+4+10+8 = 33 */
    g_fmt_err = (total == 33) ? 0 : 1;

    /* 单独测试每种 fmt */
    g_fmt_len[1] = host_fmt(0, 0LL, 0.0, 0);        /* %d: "0" → 1 */
    g_fmt_len[2] = host_fmt(2, 0LL, 0.0, 5);         /* %g: "0" → 1 */
    g_fmt_len[3] = host_fmt(5, 255LL, 0.0, 0);       /* %x: "ff" → 2 */
    g_fmt_len[4] = host_fmt(0, 9223372036854775807LL, 0.0, 0);
    /* I64_MAX: "9223372036854775807" → 19 */

    /* ============================================
     * 4. host.os_time / host.os_clock
     * ============================================ */
    g_time  = host_os_time();   /* 应返回 > 0 的时间戳 */
    g_clock = host_os_clock();  /* 应返回 >= 0 的 CPU 时间 */

    /* ============================================
     * 5. host.os_time_table
     *    2026年5月25日 12:00:00 → 应返回有效 Unix 时间戳
     * ============================================ */
    g_time_tbl = host_os_time_table(2026, 5, 25, 12, 0, 0);

    /* ============================================
     * 6. host.os_tmpname
     *    生成临时文件名, 返回写入 fmt_buf 的字节数
     * ============================================ */
    g_tmpname_len = host_os_tmpname();

    /* ============================================
     * 7. host.os_exit
     *    调用 os_exit(42, true) — callback 仅记录不真正退出
     * ============================================ */
    host_os_exit(42, 1);  /* 测试: code=42, has_code=1 (不真正退出) */
    host_os_exit(0, 0);   /* 测试: 无 code 参数 */

    /* ============================================
     * 8. host.fs_seek / host.fs_close — 无效 fd
     *    fd 999 不存在，预期返回 -1
     * ============================================ */
    g_seek_err  = (int)host_fs_seek(999, 0, 0);    /* 预期 -1 */
    g_close_err = host_fs_close(999);                /* 预期 -1 */

    /* ============================================
     * 9. host.read — stdin stub
     *    预期返回 -1 (EOF, 无 stdin 数据)
     * ============================================ */
    g_read_result = host_read(0, 10);  /* 预期 -1 */
}

/* ---- 导出函数供 Lua 端验证 ---- */

/**
 * @brief get_result(idx) → f64
 * idx 0..9: math 结果
 * idx 10..12: math2 结果
 * idx 13: clock 结果
 * idx 14: os_time 结果(转为 f64, 可能丢失精度)
 */
double get_result(int idx) {
    if (idx >= 0 && idx <= 9) return g_math[idx];
    if (idx >= 10 && idx <= 12) return g_math2[idx - 10];
    if (idx == 13) return g_clock;
    if (idx == 14) return (double)g_time;
    return -999.0;
}

/**
 * @brief get_int_result(idx) → i32
 * idx 0: fmt 总字节数 (预期 33)
 * idx 1..4: fmt 单次结果
 * idx 5: tmpname_len
 * idx 6: seek_err (预期 -1)
 * idx 7: close_err (预期 -1)
 * idx 8: read_result (预期 -1)
 * idx 9: time_tbl 符号 (正数=1,0=0,负=-1)
 */
int get_int_result(int idx) {
    switch (idx) {
        case 0: return g_fmt_len[0];      /* fmt 总字节数 */
        case 1: return g_fmt_len[1];      /* %d "0" → 1 */
        case 2: return g_fmt_len[2];      /* %g "0" → 1 */
        case 3: return g_fmt_len[3];      /* %x "ff" → 2 */
        case 4: return g_fmt_len[4];      /* %d I64_MAX → 19 */
        case 5: return g_tmpname_len;     /* tmpname 字节数 */
        case 6: return g_seek_err;        /* 预期 -1 */
        case 7: return g_close_err;       /* 预期 -1 */
        case 8: return g_read_result;     /* 预期 -1 */
        case 9: return (g_time_tbl > 0) ? 1 : ((g_time_tbl < 0) ? -1 : 0);
        case 10: return (g_time > 0) ? 1 : 0;  /* os_time 应为正 */
        default: return -999;
    }
}

/**
 * @brief verify_all() → i32
 * 在 WASM 内部做一次完整自校验
 * @return 0 全部通过; 非 0 为失败位掩码:
 *   bit0=math_err   bit1=math2_err  bit2=fmt_err
 *   bit3=time_err   bit4=time_tbl   bit5=fs_err
 *   bit6=read_stub  bit7=tmpname
 */
int verify_all(void) {
    int fail = 0;
    if (g_math_err != 0)               fail |= 1;
    if (g_math2_err != 0)              fail |= 2;
    if (g_fmt_err != 0)                fail |= 4;
    if (g_time <= 0)                   fail |= 8;
    if (g_time_tbl <= 0)               fail |= 16;
    if (g_seek_err != -1 || g_close_err != -1) fail |= 32;
    if (g_read_result != -1)           fail |= 64;
    if (g_tmpname_len <= 0)            fail |= 128;
    return fail;
}