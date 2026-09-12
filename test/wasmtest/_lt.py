# -*- coding: utf-8 -*-
import subprocess, os
wt = os.path.expandvars(r'%TEMP%\wasm-tools\wasm-tools-1.257.0-x86_64-windows\wasm-tools.exe')

# 实验：list 返回，core 返回"结果区指针"模式（(param i32 i32) -> (result i32)）
wat = '''(component
  (type $li (list u32))
  (type $li2 (list u32))
  (export $liX "li" (type $li))
  (export $li2X "li2" (type $li2))
  (core module $M
    (memory (export "memory") 1)
    (data (i32.const 0) "\\40\\00\\00\\00")  ;; bump=64
    (func $realloc (export "realloc") (param i32 i32 i32 i32) (result i32)
      (local $base i32)
      (local $aligned i32)
      (local.set $base (i32.load (i32.const 0)))
      (local.set $aligned
        (i32.and
          (i32.add (local.get $base) (i32.sub (local.get 2) (i32.const 1)))
          (i32.sub (i32.const 0) (local.get 2))))
      (i32.store (i32.const 0) (i32.add (local.get $aligned) (local.get 3)))
      (local.get $aligned))
    ;; 返回 list：core 把 [ptr,len] 写入结果区(8)，返回结果区指针
    (func (export "list_tail") (param i32 i32) (result i32)
      (i32.store (i32.const 8) (i32.const 16))
      (i32.store (i32.const 12) (i32.const 1))
      (i32.const 8))
    ;; 返回 string：把输入字符串复制一份返回
    (func (export "str_echo") (param i32 i32) (result i32)
      (local $nptr i32)
      (local $i i32)
      (local.set $nptr (call $realloc (i32.const 0) (i32.const 0) (i32.const 1) (local.get 1)))
      (block $copy_done
        (loop $copy
          (br_if $copy_done (i32.ge_u (local.get $i) (local.get 1)))
          (i32.store8 (i32.add (local.get $nptr) (local.get $i))
            (i32.load8_u (i32.add (local.get 0) (local.get $i))))
          (local.set $i (i32.add (local.get $i) (i32.const 1)))
          (br $copy)))
      (i32.store (i32.const 8) (local.get $nptr))
      (i32.store (i32.const 12) (local.get 1))
      (i32.const 8))
  )
  (core instance $i (instantiate $M))
  (alias core export $i "memory" (core memory $mem))
  (alias core export $i "realloc" (core func $realloc))
  (func (export "list-tail") (param "l" $liX) (result $li2X)
    (canon lift (core func $i "list_tail") (memory $mem) (realloc $realloc)))
  (func (export "string-echo") (param "s" string) (result string)
    (canon lift (core func $i "str_echo") (memory $mem) (realloc $realloc)))
)
'''
open('_lt.wat', 'w', encoding='utf-8').write(wat)
r1 = subprocess.run([wt, 'parse', '_lt.wat', '-o', '_lt.wasm'], capture_output=True, text=True)
print('parse:', r1.returncode, (r1.stderr or '')[:200])
if r1.returncode == 0:
    r2 = subprocess.run([wt, 'validate', '_lt.wasm'], capture_output=True, text=True)
    print('validate:', r2.returncode, (r2.stderr or '')[:200])
