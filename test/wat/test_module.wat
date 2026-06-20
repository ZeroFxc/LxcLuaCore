;; test_module.wat — 测试 wasmtime Lua 模块的 WASM 测试程序
;; 导出: memory, add(i32,i32)->i32, get_counter()->i32, read_mem(i32)->i32

(module
  ;; 导出 1 页内存
  (memory (export "memory") 1)
  ;; 导出可变全局变量
  (global (export "counter") (mut i32) (i32.const 0))
  ;; 导出表 (funcref)
  (table (export "ftable") 4 funcref)

  ;; add(i32, i32) -> i32
  (func (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add
  )

  ;; get_counter() -> i32
  (func (export "get_counter") (result i32)
    global.get 0
  )

  ;; inc_counter() — 将 counter 加 1
  (func (export "inc_counter")
    global.get 0
    i32.const 1
    i32.add
    global.set 0
  )

  ;; write_mem(offset, value) — 向内存写入 i32
  (func (export "write_mem") (param i32 i32)
    local.get 0
    local.get 1
    i32.store
  )

  ;; read_mem(offset) -> i32 — 从内存读取 i32
  (func (export "read_mem") (param i32) (result i32)
    local.get 0
    i32.load
  )
)