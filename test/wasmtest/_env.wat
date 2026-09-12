(component
  (import "wasi:cli/environment@0.2.0" (instance $wasi_env
    (export "get-environment" (func (result (list (tuple string string)))))
    (export "get-arguments" (func (result (list string))))
    (export "initial-cwd" (func (result (option string))))
  ))
  (core module $MemMod
    (memory (export "memory") 1)
    (data (i32.const 0) "\40\00\00\00")  ;; bump=64
    (func (export "realloc") (param i32 i32 i32 i32) (result i32)
      (local $base i32)
      (local $aligned i32)
      (local.set $base (i32.load (i32.const 0)))
      (local.set $aligned
        (i32.and
          (i32.add (local.get $base) (i32.sub (local.get 2) (i32.const 1)))
          (i32.sub (i32.const 0) (local.get 2))))
      (i32.store (i32.const 0) (i32.add (local.get $aligned) (local.get 3)))
      (local.get $aligned))
  )
  (core instance $mem (instantiate $MemMod))
  (alias core export $mem "memory" (core memory $mem))
  (alias core export $mem "realloc" (core func $realloc))
  (core func $lower_getenv (canon lower (func $wasi_env "get-environment")
    (memory $mem) (realloc $realloc)))
  (core instance $env_core
    (export "lower-getenv" (func $lower_getenv))
    (export "memory" (memory $mem))
  )
  (core module $Mod
    (import "env" "memory" (memory 1))
    ;; canon lower：返回 list 用 retptr 模式，core 传地址，宿主写 [ptr,len]
    (func $getenv (import "env" "lower-getenv") (param i32))
    (func (export "mod-env-count") (result i32)
      (i32.const 64)    ;; retptr
      (call $getenv)
      (i32.load (i32.const 68)))  ;; len = 元素个数
    (func (export "mod-env-ptr") (result i32)
      (i32.const 64) (call $getenv) (i32.load (i32.const 64)))
    (func (export "mod-env-len") (result i32)
      (i32.const 64) (call $getenv) (i32.load (i32.const 68)))
  )
  (core instance $m (instantiate $Mod (with "env" (instance $env_core))))
  (func $env_count (result u32) (canon lift (core func $m "mod-env-count")))
  (func $env_ptr (result u32) (canon lift (core func $m "mod-env-ptr")))
  (func $env_len (result u32) (canon lift (core func $m "mod-env-len")))
  (export "env-count" (func $env_count))
  (export "env-ptr" (func $env_ptr))
  (export "env-len" (func $env_len))
)
