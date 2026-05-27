// c_test4.c - 从文件读取二进制并测试
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wasmtime.h>

int main() {
    // 写原始二进制文件
    unsigned char bin[] = {
        0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,
        0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,
        0x03,0x02,0x01,0x00,
        0x07,0x07,0x01,0x03,'a','d','d',0x00,0x00,
        0x0a,0x08,0x01,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b
    };
    size_t sz = sizeof(bin);
    
    // 十六进制查看
    printf("Binary (%zu bytes): ", sz);
    for(size_t i = 0; i < sz; i++) printf("%02X ", bin[i]);
    printf("\n");

    // 写文件
    FILE *f = fopen("test/test_raw.wasm", "wb");
    fwrite(bin, 1, sz, f);
    fclose(f);

    // 读回文件
    f = fopen("test/test_raw.wasm", "rb");
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(fsz);
    fread(buf, 1, fsz, f);
    fclose(f);
    
    printf("File read: %ld bytes\n", fsz);
    // 比较
    int match = (fsz == (long)sz);
    for(long i = 0; i < fsz && i < (long)sz; i++) {
        if(buf[i] != bin[i]) { match = 0; printf("Diff at %ld: %02X vs %02X\n", i, buf[i], bin[i]); }
    }
    printf("Match: %s\n", match ? "YES" : "NO");

    // 用 wasmtime 加载
    wasm_config_t *cfg = wasm_config_new();
    wasm_engine_t *engine = wasm_engine_new_with_config(cfg);
    wasmtime_error_t *error;
    wasmtime_module_t *mod;

    // 1. 从数组加载
    error = wasmtime_module_new(engine, bin, sz, &mod);
    printf("From array: %s\n", error ? "FAIL" : "OK");
    if(error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("  err: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    }

    // 2. 从文件缓冲区加载
    error = wasmtime_module_new(engine, buf, fsz, &mod);
    printf("From file:  %s\n", error ? "FAIL" : "OK");
    if(error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("  err: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
    }

    free(buf);
    return 0;
}