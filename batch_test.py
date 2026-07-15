"""批量编译测试 LXCLUA 特殊语法"""
import subprocess
import os
import sys

test_dir = "test"
tests = [
    # Pipe/Lambda
    "test_pipe_basic.lua", "test_pipe_method.lua", "test_pipe_method2.lua",
    "test_pipe_method3.lua", "test_pipe_method4.lua", "test_pipe_noparam.lua",
    "test_pipe_lambda2.lua", "test_pipe_min.lua", "test_pipe_self_simple.lua",
    "test_lambda_pipe.lua", "test_lambda_inline.lua",
    # Compound
    "test_compound_logic.lua",
    # Switch
    "test_switch1.lua",
    # Guard
    "test_guard.lua",
    # Let
    "test_let.lua", "test_let_simple.lua",
    # Destructuring
    "test_destructuring.lua",
    # Do
    "test_do_simple.lua", "test_do_simple2.lua", "test_do_stmt.lua",
    "test_do_expr.lua", "test_do_assign.lua", "test_do_local.lua",
    "test_do_local_empty.lua", "test_do_empty.lua", "test_do_str.lua",
    # Range
    "test_range.lua", "test_range_simple.lua", "test_range_tmp.lua",
    "test_range_debug.lua", "test_range_delete_for.lua",
    # Chain
    "test_chain.lua", "test_chain2.lua", "test_chain_min.lua",
    "test_chain_minimal.lua",
    # Method shorthand
    "test_method_shorthand.lua",
    # Keyword label
    "test_keyword_label.lua",
    # Template
    "test_template.lua",
    # Enum
    "test_enum.lua",
    # Trait
    "test_trait.lua", "test_trait_verify.lua",
    # Decorator
    "test_decorator.lua",
    # Type hinting
    "test_type_hinting.lua",
    # Export
    "test_export_toplevel.lua", "test_export_module.lua",
    "test_require_export.lua",
    # Async
    "test_async_debug.lua", "test_async_debug2.lua", "test_async_debug3.lua",
    "test_async_debug4.lua", "test_async_debug5.lua", "test_async_debug6.lua",
    "test_async_dump.lua",
    # Await
    "test_await_debug2.lua", "test_await_debug3.lua",
    # Extension
    "test_extension.lua",
    # Cond expr
    "test_cond_expr.lua",
    # Concat
    "test_concat.lua", "test_concat2.lua", "test_concat3.lua",
    # Warn
    "test_warn.lua",
    # Compile
    "test_compile.lua",
]

ok = []
fail = []
for t in tests:
    path = os.path.join(test_dir, t)
    out = path.replace(".lua", ".luac")
    try:
        r = subprocess.run(["luac.exe", "-o", out, path], capture_output=True, text=True, timeout=30)
        if r.returncode == 0:
            ok.append(t)
        else:
            err = r.stderr.strip().split('\n')[-1] if r.stderr.strip() else "unknown"
            fail.append((t, err))
    except Exception as e:
        fail.append((t, str(e)))

print(f"\n=== OK ({len(ok)}) ===")
for t in ok:
    print(f"  OK: {t}")
print(f"\n=== FAIL ({len(fail)}) ===")
for t, err in fail:
    print(f"  FAIL: {t}")
    print(f"        {err[:120]}")