# -*- coding: utf-8 -*-
import subprocess, os
wt = os.path.expandvars(r'%TEMP%\wasm-tools\wasm-tools-1.257.0-x86_64-windows\wasm-tools.exe')

wat = '''(component
  (type $color (enum "red" "green" "blue"))
  (type $perm (flags "a" "b" "c"))
  (type $pair (record (field "a" u32) (field "b" u32)))
  (export $colorX "color" (type $color))
  (export $permX "perm" (type $perm))
  (export $pairX "pair" (type $pair))
  (core module $M
    (memory (export "memory") 1)
    (func (export "bool_not") (param i32) (result i32) local.get 0 i32.eqz)
    (func (export "add_s32") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
    (func (export "add_u32") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
    (func (export "mul_f32") (param f32 f32) (result f32) local.get 0 local.get 1 f32.mul)
    (func (export "add_f64") (param f64 f64) (result f64) local.get 0 local.get 1 f64.add)
    (func (export "char_next") (param i32) (result i32) local.get 0 i32.const 1 i32.add)
    (func (export "option_plus10") (param i32 i32) (result i32)
      local.get 0
      if (result i32)
        local.get 1 i32.const 10 i32.add
      else
        i32.const 0
      end)
    (func (export "enum_passthru") (param i32) (result i32) local.get 0)
    (func (export "flags_count") (param i32) (result i32) local.get 0 i32.popcnt)
    (func (export "record_sum") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
    (func (export "tuple_sum") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
  )
  (core instance $i (instantiate $M))
  (alias core export $i "memory" (core memory $mem))
  (func (export "bool-not") (param "x" bool) (result bool) (canon lift (core func $i "bool_not") (memory $mem)))
  (func (export "add-s32") (param "a" s32) (param "b" s32) (result s32) (canon lift (core func $i "add_s32") (memory $mem)))
  (func (export "add-u32") (param "a" u32) (param "b" u32) (result u32) (canon lift (core func $i "add_u32") (memory $mem)))
  (func (export "mul-f32") (param "a" f32) (param "b" f32) (result f32) (canon lift (core func $i "mul_f32") (memory $mem)))
  (func (export "add-f64") (param "a" f64) (param "b" f64) (result f64) (canon lift (core func $i "add_f64") (memory $mem)))
  (func (export "char-next") (param "c" char) (result char) (canon lift (core func $i "char_next") (memory $mem)))
  (func (export "option-plus10") (param "o" (option u32)) (result u32) (canon lift (core func $i "option_plus10") (memory $mem)))
  (func (export "enum-passthru") (param "e" $colorX) (result $colorX) (canon lift (core func $i "enum_passthru") (memory $mem)))
  (func (export "flags-count") (param "f" $permX) (result u32) (canon lift (core func $i "flags_count") (memory $mem)))
  (func (export "record-sum") (param "r" $pairX) (result u32) (canon lift (core func $i "record_sum") (memory $mem)))
  (func (export "tuple-sum") (param "t" (tuple u32 u32)) (result u32) (canon lift (core func $i "tuple_sum") (memory $mem)))
)
'''
open('_types.wat', 'w', encoding='utf-8').write(wat)
r1 = subprocess.run([wt, 'parse', '_types.wat', '-o', '_types.wasm'], capture_output=True, text=True)
print('parse:', r1.returncode, (r1.stderr or '')[:150])
if r1.returncode == 0:
    r2 = subprocess.run([wt, 'validate', '_types.wasm'], capture_output=True, text=True)
    print('validate:', r2.returncode, (r2.stderr or '')[:150])
    print('size:', os.path.getsize('_types.wasm'))
