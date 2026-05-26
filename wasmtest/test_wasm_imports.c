/**
 * test_wasm_imports.c — 测试 wasmtime 外部导入机制
 *
 * 此模块从 "host" 模块导入函数，与 lwasmtime.c 中 l2w_imports[] 注册的 28 个回调匹配。
 * 通过 wasmtime.runLua2wasm() 运行，自动链接所有 host import。
 *
 * 编译命令:
 *   emcc test_wasm_imports.c -o test_wasm_imports.wasm --no-entry \
 *       -s STANDALONE_WASM=1 \
 *       -s EXPORTED_FUNCTIONS='["_main","_get_result","_verify_imports"]' \
 *       -O2
 *
 * 测试内容:
 *   1. host.math  (kind=0..9)  — sin/cos/tan/asin/acos/atan/exp/log/log2/log10
 *   2. host.math2 (kind=0..2)  — atan2/pow/fmod
 *   3. host.fmt   (kind=0..5)  — 格式化数值到 fmt_buf
 *   4. host.os_time            — Unix 时间戳
 *   5. host.os_clock           — CPU 时间
 *   6. host.fs_seek/host.fs_close — 文件操作(无效 fd 测试错误处理)
 */

/* ---- 从 "host" 模块导入的函数声明 ---- */

/** host.math(kind, x) → f64
 *  kind: 0=sin 1=cos 2=tan 3=asin 4=acos 5=atan 6=exp 7=log 8=log2 9=log10 */
__attribute__((import_module("host"), import_name("math")))
extern double host_math(int kind, double x);

/** host.math2(kind, x, y) → f64
 *  kind: 0=atan2 1=pow 2=fmod */
__attribute__((import_module("host"), import_name("math2")))
extern double host_math2(int kind, double x, double y);

/** host.fmt(kind, iv, fv, prec) → i32
 *  格式化数值到 fmt_buf，返回写入字节数
 *  kind: 0=%d(i64) 2=%g(f64+prec) 3=%f 4=%e 5=%x */
__attribute__((import_module("host"), import_name("fmt")))
extern int host_fmt(int kind, long long iv, double fv, int prec);

/** host.os_time() → i64 — 返回当前 Unix 时间戳 */
__attribute__((import_module("host"), import_name("os_time")))
extern long long host_os_time(void);

/** host.os_clock() → f64 — 返回进程 CPU 时间(秒) */
__attribute__((import_module("host"), import_name("os_clock")))
extern double host_os_clock(void);

/** host.fs_seek(fd, whence, offset) → i64 — whence: 0=set 1=cur 2=end */
__attribute__((import_module("host"), import_name("fs_seek")))
extern long long host_fs_seek(int fd, int whence, long long offset);

/** host.fs_close(fd) → i32 — 关闭文件，0 成功 */
__attribute__((import_module("host"), import_name("fs_close")))
extern int host_fs_close(int fd);

/* ---- 全局结果存储（供导出函数 get_result / verify_imports 返回验证） ---- */

static double g_math_results[10];  /* 10 个 math 函数结果 */
static double g_math2_results[3];  /* 3 个 math2 函数结果 */
static int    g_fmt_result;        /* fmt 返回的字节数 */
static long long g_time_result;    /* os_time 返回值 */
static double g_clock_result;      /* os_clock 返回值 */
static int    g_seek_result;       /* fs_seek 返回值(预期 -1) */
static int    g_close_result;      /* fs_close 返回值(预期 -1) */

/**
 * @brief main() — 被 runLua2wasm 自动调用
 * 依次调用所有 host 导入函数，将结果存入全局变量
 */
void main(void) {
    int i;

    /* 1. 测试 host.math 全部 10 种运算 */
    double test_val = 0.5;  /* sin(0.5)≈0.4794 cos(0.5)≈0.8776 */
    int math_kinds[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (i = 0; i < 10; i++) {
        g_math_results[i] = host_math(math_kinds[i], test_val);
    }

    /* 2. 测试 host.math2 全部 3 种运算 */
    g_math2_results[0] = host_math2(0, 1.0, 1.0);   /* atan2(1,1) = pi/4 ≈ 0.785 */
    g_math2_results[1] = host_math2(1, 2.0, 10.0);   /* pow(2,10) = 1024 */
    g_math2_results[2] = host_math2(2, 10.5, 3.0);   /* fmod(10.5,3) = 1.5 */

    /* 3. 测试 host.fmt — 格式化数值 */
    g_fmt_result = host_fmt(0, 12345LL, 0.0, 0);      /* %d: 12345 → len=5 */
    g_fmt_result += host_fmt(2, 0LL, 3.14159, 4);      /* %g: 3.142 → len=5 */
    g_fmt_result += host_fmt(3, 0LL, 2.5, 2);          /* %f: 2.50 → len=4 */
    g_fmt_result += host_fmt(4, 0LL, 100.0, 3);        /* %e: 1.000e+02 → len=10 */

    /* 4. 测试 host.os_time */
    g_time_result = host_os_time();

    /* 5. 测试 host.os_clock */
    g_clock_result = host_os_clock();

    /* 6. 测试 fs_seek / fs_close 对无效 fd 的错误处理 */
    g_seek_result = (int)host_fs_seek(999, 0, 0);   /* 无效 fd → 预期 -1 */
    g_close_result = host_fs_close(999);              /* 无效 fd → 预期 -1 */
}

/**
 * @brief get_result(idx) → f64
 * 返回存储的计算结果，供 Lua 端验证
 * idx: 0..9 → math 结果, 10..12 → math2 结果, 13 → clock 结果
 */
double get_result(int idx) {
    if (idx >= 0 && idx <= 9) {
        return g_math_results[idx];
    } else if (idx >= 10 && idx <= 12) {
        return g_math2_results[idx - 10];
    } else if (idx == 13) {
        return g_clock_result;
    }
    return -999.0;
}

/**
 * @brief verify_imports() → i32
 * 验证所有 import 调用的结果是否在合理范围内
 * @return 0 表示全部通过, 非 0 表示失败(位掩码)
 */
int verify_imports(void) {
    int failures = 0;

    /* math: sin(0.5) ≈ 0.4794 */
    double diff = g_math_results[0] - 0.4794;
    if (diff < -0.01 || diff > 0.01) failures |= 1;

    /* math: cos(0.5) ≈ 0.8776 */
    diff = g_math_results[1] - 0.8776;
    if (diff < -0.01 || diff > 0.01) failures |= 2;

    /* math2: pow(2,10) == 1024 */
    if (g_math2_results[1] != 1024.0) failures |= 4;

    /* math2: fmod(10.5,3) == 1.5 */
    diff = g_math2_results[2] - 1.5;
    if (diff < -0.001 || diff > 0.001) failures |= 8;

    /* fs_seek(999) 预期 -1 */
    if (g_seek_result != -1) failures |= 16;

    /* fs_close(999) 预期 -1 */
    if (g_close_result != -1) failures |= 32;

    return failures;
}