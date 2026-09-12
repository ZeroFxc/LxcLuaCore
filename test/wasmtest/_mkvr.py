# -*- coding: utf-8 -*-
import subprocess, os
wt = os.path.expandvars(r'%TEMP%\wasm-tools\wasm-tools-1.257.0-x86_64-windows\wasm-tools.exe')

# variant + result 返回（返回指针模式）
wat = '''(component
  (type $v (variant (case "none") (case "some" u32)))
  (type $r (result u32 (error u32)))
  (export $vX "v" (type $v))
  (export $rX "r" (type $r))
  (core module $M
    (memory (export "memory") 1)
    (data (i32.const 0) "\\40\\00\\00\\00")
    ;; variant-echo: (tag, payload) -> 返回指针指向 [tag, payload]
    (func (export "variant_echo") (param i32 i32) (result i32)
      (i32.store (i32.const 8) (local.get 0))
      (i32.store (i32.const 12) (local.get 1))
      (i32.const 8))
    ;; result-echo: (tag, payload) -> 返回指针指向 [tag, payload]
    (func (export "result_echo") (param i32 i32) (result i32)
      (i32.store (i32.const 8) (local.get 0))
      (i32.store (i32.const 12) (local.get 1))
      (i32.const 8))
    ;; variant-tag-only: 返回无载荷 variant，直接返回 tag（单值）
    (func (export "variant_tag") (param i32 i32) (result i32)
      local.get 0)
  )
  (core instance $i (instantiate $M))
  (alias core export $i "memory" (core memory $mem))
  (func (export "variant-echo") (param "x" $vX) (result $vX)
    (canon lift (core func $i "variant_echo") (memory $mem)))
  (func (export "result-echo") (param "x" $rX) (result $rX)
    (canon lift (core func $i "result_echo") (memory $mem)))
)
'''
open('_vr.wat', 'w', encoding='utf-8').write(wat)
r1 = subprocess.run([wt, 'parse', '_vr.wat', '-o', '_vr.wasm'], capture_output=True, text=True)
print('parse:', r1.returncode, (r1.stderr or '')[:200])
if r1.returncode == 0:
    r2 = subprocess.run([wt, 'validate', '_vr.wasm'], capture_output=True, text=True)
    print('validate:', r2.returncode, (r2.stderr or '')[:200])
