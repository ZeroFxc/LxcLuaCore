/**
 * test_wasm_module.c — 用于测试 wasmtime Lua 绑定的 WASM 模块
 *
 * 导出一组纯函数(无任何外部依赖), 供 wasmtime.newModule + instance:getExport + func:call 流程测试
 * 编译命令: emcc test_wasm_module.c -o test_wasm_module.wasm --no-entry -s STANDALONE_WASM=1 -s EXPORTED_FUNCTIONS='["_add","_fib","_mul","_factorial","_add_double","_is_prime","_gcd"]' -O2
 */

#include <math.h>

/**
 * @brief 整数加法
 * @param a 加数
 * @param b 加数
 * @return a + b
 */
int add(int a, int b) {
    return a + b;
}

/**
 * @brief 斐波那契数列(递归实现)
 * @param n 索引(n >= 0)
 * @return 第 n 个斐波那契数
 */
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

/**
 * @brief 整数乘法
 * @param a 乘数
 * @param b 乘数
 * @return a * b
 */
int mul(int a, int b) {
    return a * b;
}

/**
 * @brief 阶乘(迭代实现)
 * @param n 输入整数(n >= 0)
 * @return n! (n 的阶乘)
 */
int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

/**
 * @brief 浮点数加法
 * @param a 加数
 * @param b 加数
 * @return a + b
 */
double add_double(double a, double b) {
    return a + b;
}

/**
 * @brief 判断整数是否为质数
 * @param n 输入整数
 * @return 1 表示质数, 0 表示非质数
 */
int is_prime(int n) {
    if (n <= 1) return 0;
    if (n <= 3) return 1;
    if (n % 2 == 0 || n % 3 == 0) return 0;
    for (int i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    }
    return 1;
}

/**
 * @brief 计算两个整数的最大公约数(GCD, 欧几里得算法)
 * @param a 整数
 * @param b 整数
 * @return gcd(a, b)
 */
int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}