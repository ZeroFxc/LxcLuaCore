/**
 * test_wasm_complex.c — 复杂计算 WASM 模块（无外部导入）
 *
 * 通过 wasmtime 标准 API (newEngine→newStore→newModule→newInstance→getExport→call) 调用。
 * 测试边缘情况: 大数递归、浮点精度、迭代算法、数学恒等式等。
 *
 * 编译命令:
 *   emcc test_wasm_complex.c -o test_wasm_complex.wasm --no-entry \
 *       -s STANDALONE_WASM=1 \
 *       -s EXPORTED_FUNCTIONS='["_ackermann","_mandelbrot","_collatz_steps","_sqrt_newton",\
 *         "_det2x2","_poly_eval","_sum_range","_hamming_weight","_reverse_bits","_popcount"]' \
 *       -O2
 */

/**
 * @brief Ackermann 函数（极有限深度版）
 * Ackermann 增长极快, ack(3,6) 已经非常大, ack(4, *) 会溢出
 * @param m 第一个参数 (0..3)
 * @param n 第二个参数 (0..10)
 * @return Ackermann(m, n)
 */
int ackermann(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

/**
 * @brief Mandelbrot 迭代次数
 * 计算复数点 (cx/1000 + i*cy/1000) 在 Mandelbrot 集中的逃逸迭代次数
 * @param cx 实部(缩放: 实际值 = cx/1000)
 * @param cy 虚部(缩放: 实际值 = cy/1000)
 * @param max_iter 最大迭代次数
 * @return 逃逸所需的迭代次数, 达到 max_iter 表示未逃逸
 */
int mandelbrot(int cx, int cy, int max_iter) {
    double cr = (double)cx / 1000.0;
    double ci = (double)cy / 1000.0;
    double zx = 0.0, zy = 0.0;
    double zx2 = 0.0, zy2 = 0.0;
    int iter;
    for (iter = 0; iter < max_iter; iter++) {
        if (zx2 + zy2 > 4.0) break;
        zy = 2.0 * zx * zy + ci;
        zx = zx2 - zy2 + cr;
        zx2 = zx * zx;
        zy2 = zy * zy;
    }
    return iter;
}

/**
 * @brief Collatz 猜想步数
 * 对正整数 n 应用 Collatz 规则直到变为 1, 返回步数
 * 规则: n 为偶数 → n/2, n 为奇数 → 3n+1
 * @param n 起始正整数 (1..10^6)
 * @return 到达 1 所需的步数
 */
int collatz_steps(int n) {
    if (n <= 0) return -1;
    int steps = 0;
    /* 使用 64 位防止 3n+1 溢出 */
    long long x = n;
    while (x != 1) {
        if (x % 2 == 0) {
            x = x / 2;
        } else {
            x = 3 * x + 1;
        }
        steps++;
        if (steps > 10000) return -2;  /* 无限循环保护 */
    }
    return steps;
}

/**
 * @brief 牛顿法求平方根
 * 使用牛顿迭代法计算 sqrt(x), 内部用 double 精度
 * @param x 输入值 (x >= 0)
 * @param iterations 迭代次数 (1..20)
 * @return sqrt(x) * 1000000 取整 (即保留 6 位小数的定点值)
 */
int sqrt_newton(int x, int iterations) {
    if (x < 0) return -1;
    if (x == 0) return 0;

    double val = (double)x;
    double guess = val * 0.5;  /* 初始猜测 */

    for (int i = 0; i < iterations; i++) {
        guess = (guess + val / guess) * 0.5;
    }

    /* 返回定点: sqrt(x) * 10^6 */
    return (int)(guess * 1000000.0);
}

/**
 * @brief 2x2 矩阵行列式
 * | a  b |
 * | c  d | → a*d - b*c
 * @return 行列式值
 */
double det2x2(double a, double b, double c, double d) {
    return a * d - b * c;
}

/**
 * @brief 多项式求值 (Horner 方法)
 * 计算: coeff[0] + coeff[1]*x + coeff[2]*x^2 + coeff[3]*x^3 + coeff[4]*x^4
 * @param c0..c4 系数
 * @param x 变量值
 * @return 多项式值
 */
double poly_eval(double c0, double c1, double c2, double c3, double c4, double x) {
    /* Horner: (((c4*x + c3)*x + c2)*x + c1)*x + c0 */
    double result = c4;
    result = result * x + c3;
    result = result * x + c2;
    result = result * x + c1;
    result = result * x + c0;
    return result;
}

/**
 * @brief 区间求和 (Gauss 公式)
 * sum_range(m, n) = m + (m+1) + ... + n
 * @param m 起始值
 * @param n 结束值 (n >= m)
 * @return 求和结果
 */
long long sum_range(int m, int n) {
    if (m > n) return 0;
    /* Gauss: (m + n) * (n - m + 1) / 2 */
    long long count = (long long)(n - m + 1);
    long long total = (long long)(m + n);
    return total * count / 2;
}

/**
 * @brief Hamming weight (popcount)
 * 计算 32 位无符号整数中 1 的个数
 * @param x 输入值
 * @return 二进制中 1 的个数
 */
int hamming_weight(unsigned int x) {
    /* 经典 SWAR 算法 */
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    x = x + (x >> 8);
    x = x + (x >> 16);
    return x & 0x3F;
}

/**
 * @brief 位反转 (32-bit)
 * 将 32 位整数的位顺序完全反转
 * @param x 输入值
 * @return 位反转后的值
 */
unsigned int reverse_bits(unsigned int x) {
    x = ((x & 0x55555555u) << 1)  | ((x & 0xAAAAAAAAu) >> 1);
    x = ((x & 0x33333333u) << 2)  | ((x & 0xCCCCCCCCu) >> 2);
    x = ((x & 0x0F0F0F0Fu) << 4)  | ((x & 0xF0F0F0F0u) >> 4);
    x = ((x & 0x00FF00FFu) << 8)  | ((x & 0xFF00FF00u) >> 8);
    x = (x << 16) | (x >> 16);
    return x;
}

/**
 * @brief popcount 64-bit
 * @return 64 位值中 1 的个数
 */
int popcount(unsigned long long x) {
    /* 64-bit SWAR */
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    x = x + (x >> 8);
    x = x + (x >> 16);
    x = x + (x >> 32);
    return (int)(x & 0x7F);
}