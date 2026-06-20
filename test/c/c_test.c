// c_test.c - 直接使用 wasmtime C API 加载硬编码 WASM 二进制
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

int main() {
    unsigned char bin[] = {
        0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,
        0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,
        0x03,0x02,0x01,0x00,
        0x07,0x07,0x01,0x03,'a','d','d',0x00,0x00,
        0x0a,0x08,0x01,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b
    };
    size_t bin_size = sizeof(bin);
    printf("Binary size: %zu\n", bin_size);

    wasm_config_t *cfg = wasm_config_new();
    wasmtime_config_wasm_gc_set(cfg, true);
    wasmtime_config_wasm_reference_types_set(cfg, true);
    wasm_engine_t *engine = wasm_engine_new_with_config(cfg);
    printf("[OK] engine\n");

    wasmtime_error_t *error = NULL;
    wasmtime_module_t *mod = NULL;
    error = wasmtime_module_new(engine, bin, bin_size, &mod);
    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("Module error: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 1;
    }
    printf("[OK] module\n");

    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    wasmtime_context_t *ctx = wasmtime_store_context(store);
    printf("[OK] store+context\n");

    wasmtime_instance_t inst;
    wasm_trap_t *trap = NULL;
    error = wasmtime_instance_new(ctx, mod, NULL, 0, &inst, &trap);
    if (error) {
        wasm_name_t msg;
        wasmtime_error_message(error, &msg);
        printf("Instance error: %.*s\n", (int)msg.size, msg.data);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(error);
        return 1;
    }
    printf("[OK] instance\n");

    // Get export
    wasmtime_extern_t item;
    bool found = wasmtime_instance_export_get(ctx, &inst, "add", 3, &item);
    if (!found) { printf("export not found\n"); return 1; }
    printf("[OK] export: kind=%d\n", item.kind);

    // Call
    wasmtime_val_t args[2] = {
        { .kind = WASMTIME_I32, .of = { .i32 = 100 } },
        { .kind = WASMTIME_I32, .of = { .i32 = 200 } },
    };
    wasmtime_val_t results[1];
    trap = NULL;
    error = wasmtime_func_call(ctx, &item.of.func, args, 2, results, 1, &trap);
    if (error || trap) {
        printf("Call failed\n");
        return 1;
    }
    printf("add(100,200) = %d\n", results[0].of.i32);
    printf("[ALL OK]\n");
    return 0;
}