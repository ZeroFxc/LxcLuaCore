#!/usr/bin/env python3
"""
build_wasm.py — 一键编译 WASM 模块

自动分析 C 源码:
  - 检测所有导出函数(非 static / 非 extern 声明的函数定义)
  - 检测 import 声明
  - 自动生成 emcc 编译命令

用法:
  python build_wasm.py <file.c>           # 显示命令,需确认后执行
  python build_wasm.py <file.c> --run     # 直接编译
  python build_wasm.py <file.c> --dry     # 仅显示命令,不执行
  python build_wasm.py all                # 编译当前目录所有 .c
  python build_wasm.py all --dry          # 显示所有命令,不执行

依赖: emcc 已在 PATH 中
"""

import re
import sys
import subprocess
import os
from pathlib import Path


# ============================================================
# C 源码分析
# ============================================================

def parse_c_file(filepath: str) -> dict:
    """解析 C 源文件,提取元数据.

    返回:
      {
        "exports": ["func1", "func2", ...],      # 导出函数名
        "imports": [("module", "name"), ...],     # import 声明
        "basename": "linker_test_module",         # 文件名(不含扩展名)
      }
    """
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    basename = Path(filepath).stem

    # ---- 1. 检测 import 声明 ----
    # 匹配: __attribute__((import_module("mod"), import_name("name")))
    import_re = re.compile(
        r'import_module\("([^"]+)"\).*?import_name\("([^"]+)"\)'
    )
    imports = import_re.findall(content)

    # ---- 2. 检测导出函数 ----
    # 策略: 找所有 "函数名(参数) {" 模式，从 ( 往前取函数名
    # 然后检查前面是否有 static/extern/typedef 关键字
    exports = []
    cleaned = _strip_comments(content)

    # 匹配: 函数名 ( 参数 ) {   (允许 { 在下一行)
    func_re = re.compile(
        r'\b(\w+)\s*'          # 函数名
        r'\([^)]*\)'           # (参数列表)
        r'\s*\{',               # 函数体开始
        re.MULTILINE
    )

    for m in func_re.finditer(cleaned):
        func_name = m.group(1)

        # 跳过明显不是函数名的情况
        if func_name in ("if", "for", "while", "switch", "sizeof",
                          "return", "goto", "struct", "enum", "union"):
            continue

        # 获取函数名所在行, 从行首到函数名之间的文本
        line_start = cleaned.rfind('\n', 0, m.start()) + 1
        line_prefix = cleaned[line_start:m.start()]

        # 检查同一行内的关键字:
        # - static → 跳过(静态函数不导出)
        # - typedef → 跳过(类型别名不是函数)
        if re.search(r'\bstatic\b', line_prefix):
            continue
        if re.search(r'\btypedef\b', line_prefix):
            continue

        # extern 声明: 函数体以 ; 结尾才是声明, 这里匹配到 { 说明有定义
        # 但 import 声明(有 __attribute__)可能配到同名, 跳过
        if re.search(r'\bextern\b', line_prefix):
            if func_name in [imp[1] for imp in imports]:
                continue

        exports.append(func_name)

    # 去重并保持顺序
    seen = set()
    unique_exports = []
    for e in exports:
        if e not in seen:
            seen.add(e)
            unique_exports.append(e)

    return {
        "exports": unique_exports,
        "imports": imports,
        "basename": basename,
    }


def _strip_comments(content: str) -> str:
    """去除 C 注释(// 和 /* */)和字符串字面量干扰."""
    # 移除行注释
    content = re.sub(r'//[^\n]*', '', content)
    # 移除块注释
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    return content


# ============================================================
# 命令生成
# ============================================================

def build_emcc_command(meta: dict, extra_flags: str = "") -> str:
    """根据解析结果生成 emcc 编译命令."""
    basename = meta["basename"]
    exports = meta["exports"]
    imports = meta["imports"]

    if not exports:
        print(f"  [WARN] 未检测到导出函数, 请检查 {basename}.c")
        return ""

    # 构造 EXPORTED_FUNCTIONS
    funcs_json = ",".join(f'"_export_func"' for _ in exports)
    # 实际使用 Python 格式化
    func_list = '","'.join(f"_{e}" for e in exports)
    export_funcs = f'EXPORTED_FUNCTIONS=\'["{func_list}"]\''

    cmd = (
        f"emcc {basename}.c "
        f"-o {basename}.wasm "
        f"--no-entry "
        f"-s STANDALONE_WASM=1 "
        f"-s {export_funcs} "
        f"-O2"
    )

    if extra_flags:
        cmd += " " + extra_flags

    return cmd


def print_info(meta: dict):
    """打印模块分析信息."""
    basename = meta["basename"]
    exports = meta["exports"]
    imports = meta["imports"]

    print(f"\n  === {basename}.c ===")
    if imports:
        print(f"  Import: {len(imports)} 个")
        for mod, name in imports:
            print(f"    {mod}.{name}()")
    else:
        print(f"  Import: 无 (纯函数模块)")
    print(f"  Export: {len(exports)} 个 -> {', '.join(exports)}")


# ============================================================
# 主入口
# ============================================================

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    target = args[0]
    auto_run = "--run" in args
    dry_run = "--dry" in args
    extra_flags = " ".join(a for a in args[1:] if not a.startswith("--"))

    files = []
    if target == "all":
        cwd = Path.cwd()
        files = sorted(cwd.glob("*.c"))
        if not files:
            print(f"[ERROR] 当前目录 ({cwd}) 没有 .c 文件")
            return 1
    else:
        fpath = Path(target)
        if not fpath.exists():
            print(f"[ERROR] 文件不存在: {target}")
            return 1
        if fpath.suffix != ".c":
            print(f"[ERROR] 不是 .c 文件: {target}")
            return 1
        files = [fpath]

    print(f"\n{'='*60}")
    print(f"  build_wasm — EMCC WASM 一键编译")
    print(f"{'='*60}")

    commands = []
    for fp in files:
        meta = parse_c_file(str(fp))
        cmd = build_emcc_command(meta, extra_flags)
        if not cmd:
            continue
        print_info(meta)
        print(f"\n  {cmd}")
        commands.append((meta, cmd))

    if not commands:
        print("\n  [DONE] 没有可编译的文件")
        return 0

    print(f"\n{'='*60}")
    print(f"  共 {len(commands)} 个模块")

    if dry_run:
        print(f"  [DRY RUN] 仅显示命令,不执行")
        return 0

    if not auto_run:
        print(f"\n  输入 y 确认编译, 其他键跳过: ", end="")
        try:
            confirm = input().strip().lower()
        except (EOFError, KeyboardInterrupt):
            print("\n  已取消")
            return 0
        if confirm != "y":
            print("  已跳过")
            return 0

    # 执行编译
    failed = []
    for meta, cmd in commands:
        basename = meta["basename"]
        print(f"\n  [{basename}] 编译中...")
        result = subprocess.run(
            cmd, shell=True,
            cwd=str(Path.cwd()),
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print(f"  [FAIL] {basename}")
            print(f"  stderr: {result.stderr[:500]}")
            failed.append(basename)
        else:
            print(f"  [OK] {basename}.wasm")
            if result.stdout.strip():
                print(f"  {result.stdout.strip()[:200]}")

    print(f"\n{'='*60}")
    ok = len(commands) - len(failed)
    print(f"  结果: {ok} 成功, {len(failed)} 失败")
    if failed:
        print(f"  失败: {', '.join(failed)}")
    print(f"{'='*60}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())