/**
 * test_wasm_edge.c — WASM 运行时边界值 / 完整性测试模块
 *
 * 测试 wasmtime 处理极端值的能力:
 *   - NaN / Inf / -Inf 浮点
 *   - I32 / I64 极值 (INT32_MIN, INT32_MAX, INT64_MIN, INT64_MAX)
 *   - 整数溢出行为
 *   - 深度递归栈
 *   - 多参数函数
 *   - 返回大值
 *
 * 编译:
 *   emcc test_wasm_edge.c -o test_wasm_edge.wasm --no-entry \
 *       -s STANDALONE_WASM=1 \
 *       -s EXPORTED_FUNCTIONS='["_ret_i32_min","_ret_i32_max","_ret_i64_min","_ret_i64_max",\
 *         "_ret_nan","_ret_inf","_ret_neg_inf","_is_nan","_is_inf",\
 *         "_add_wrap","_mul_wrap","_deep_recursion","_many_params","_identity"]' \
 *       -O2
 */

/* ---- 整数极值 ---- */

/**
 * @brief 返回 I32 最小值 (-2147483648)
 */
int ret_i32_min(void) {
    return (-2147483647 - 1);
}

/**
 * @brief 返回 I32 最大值 (2147483647)
 */
int ret_i32_max(void) {
    return 2147483647;
}

/**
 * @brief 返回 I64 最小值
 */
long long ret_i64_min(void) {
    return (-9223372036854775807LL - 1);
}

/**
 * @brief 返回 I64 最大值
 */
long long ret_i64_max(void) {
    return 9223372036854775807LL;
}

/* ---- 浮点特殊值 ---- */

/**
 * @brief 返回 NaN (0.0 / 0.0)
 */
double ret_nan(void) {
    double zero = 0.0;
    return zero / zero;
}

/**
 * @brief 返回正无穷 (1.0 / 0.0)
 */
double ret_inf(void) {
    double zero = 0.0;
    return 1.0 / zero;
}

/**
 * @brief 返回负无穷 (-1.0 / 0.0)
 */
double ret_neg_inf(void) {
    double zero = 0.0;
    return -1.0 / zero;
}

/**
 * @brief 判断是否为 NaN
 * NaN 的特性: x != x 为 true
 */
int is_nan(double x) {
    return (x != x) ? 1 : 0;
}

/**
 * @brief 判断是否为 Inf
 */
int is_inf(double x) {
    if (x != x) return 0;  /* NaN */
    double huge = 1.0e300;
    if (x > huge) return 1;
    if (x < -huge) return -1;
    return 0;
}

/* ---- 整数溢出 / 回绕 ---- */

/**
 * @brief 有符号 32 位加法(回绕)
 * INT32_MAX + 1 在 WASM I32 中会回绕到 INT32_MIN
 */
int add_wrap(void) {
    int max_val = 2147483647;
    return max_val + 1;  /* WASM 语义: 回绕到 INT32_MIN */
}

/**
 * @brief 有符号 32 位乘法(回绕)
 * 1 << 31 = -2147483648 (符号位)
 */
int mul_wrap(void) {
    return (1 << 31);  /* 预期: INT32_MIN */
}

/* ---- 深度递归 ---- */

/**
 * @brief 深度递归求和
 * sum_range(0, n) = n + sum_range(0, n-1)
 * 测试运行时能否处理较深的调用栈
 * @param n 深度
 * @return 0 到 n 的和
 */
int deep_recursion(int n) {
    if (n <= 0) return 0;
    return n + deep_recursion(n - 1);
}

/* ---- 多参数函数 ---- */

/**
 * @brief 8 个 I32 参数求和
 * 测试 wasmtime func:call 对多参数的正确传递
 */
int many_params(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}

/**
 * @brief 恒等函数 — 直接返回输入值
 * 用于测试值传递的保真度
 */
double identity(double x) {
    return x;
}