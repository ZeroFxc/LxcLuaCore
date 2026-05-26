/**
 * basic_module.c — 基础 WASM 模块（无导入），用于回归测试标准 API
 * 编译: emcc basic_module.c -o basic_module.wasm --no-entry -s STANDALONE_WASM=1 -s EXPORTED_FUNCTIONS='["_add","_fib","_mul"]' -O2
 */
int add(int a, int b) { return a + b; }
int fib(int n) { if (n <= 1) return n; return fib(n - 1) + fib(n - 2); }
int mul(int a, int b) { return a * b; }