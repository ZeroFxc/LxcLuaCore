// c_test3.c - 测试不同二进制结构
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

int main() {
    // 二进制 1: type+func+export+code (之前的版本)
    unsigned char bin1[] = {
        0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,
        0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,
        0x03,0x02,0x01,0x00,
        0x07,0x07,0x01,0x03,'a','d','d',0x00,0x00,
        0x0a,0x08,0x01,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b
    };
    size_t sz1 = sizeof(bin1);

    // 二进制 2: type+import(0)+func+export+code
    unsigned char bin2[] = {
        0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,
        0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,
        0x02,0x01,0x00,  // import section: size=1, count=0
        0x03,0x02,0x01,0x00,
        0x07,0x07,0x01,0x03,'a','d','d',0x00,0x00,
        0x0a,0x08,0x01,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b
    };
    size_t sz2 = sizeof(bin2);

    // 测试每个二进制
    wasm_config_t *cfg = wasm_config_new();
    wasm_engine_t *engine = wasm_engine_new_with_config(cfg);

    wasmtime_error_t *error;
    wasmtime_module_t *mod;

    printf("=== Testing bin1 (type+func+export+code) ===\n");
    error = wasmtime_module_new(engine, bin1, sz1, &mod);
    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("FAIL: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    } else {
        printf("OK!\n");
    }

    printf("\n=== Testing bin2 (type+import(0)+func+export+code) ===\n");
    error = wasmtime_module_new(engine, bin2, sz2, &mod);
    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("FAIL: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    } else {
        printf("OK!\n");
    }

    return 0;
}