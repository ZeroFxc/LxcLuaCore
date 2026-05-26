#!/usr/bin/env python3
"""
LXCLua 头文件合并工具
将多个分散的头文件递归合并为一个独立的 lxclua.h，供 C 扩展模块开发使用。

功能：
- 递归展开 #include "..." 本地头文件
- 保留 #include <...> 系统头文件原样
- 通过 include guard 宏名去重，避免同一头文件重复展开
- 保留每个文件的 #ifndef/#define/#endif guard，用于嵌套防护
- 跟踪 typedef/struct/enum/union 名称，对冲突的类型名用 #ifndef 保护
"""

import os
import re
import sys

# ============================================================
# 配置
# ============================================================

# 用于搜索 #include "..." 的目录列表
INCLUDE_DIRS = [
    "src/core",
    "src/compiler",
    "src/utils",
    "src/stdlib",
    ".",  # 用于解析 "../utils/xxx.h" 这类相对路径
]

# 需要展开的入口头文件列表（按依赖顺序排列）
ENTRY_HEADERS = [
    "src/core/lprefix.h",
    "src/core/luaconf.h",
    "src/core/llimits.h",
    "src/core/lua.h",
    "src/core/lauxlib.h",
    "src/core/lualib.h",
    "src/core/lstate.h",
    "src/core/lobject.h",
    "src/core/ltable.h",
    "src/core/ltm.h",
    "src/core/lstring.h",
    "src/core/lfunc.h",
    "src/core/lgc.h",
    "src/core/lmem.h",
    "src/core/lopcodes.h",
    "src/core/lopnames.h",
    "src/core/lundump.h",
    "src/core/lzio.h",
    "src/core/ldo.h",
    "src/core/ldebug.h",
    "src/core/lapi.h",
    "src/core/lcode.h",
    "src/compiler/lparser.h",
    "src/compiler/llex.h",
    "src/compiler/llexer_compiler.h",
    "src/utils/lthread.h",
    "src/utils/lbigint.h",
    "src/utils/lnamespace.h",
    "src/utils/ltranslator.h",
    "src/utils/lobfuscate.h",
    "src/utils/leventloop.h",
    "src/utils/laio.h",
    "src/utils/lpromise.h",
    "src/utils/sha256.h",
    "src/utils/crc.h",
    "src/utils/aes.h",
    "src/utils/csprng.h",
    "src/utils/json_parser.h",
    "src/utils/lctype.h",
    "src/stdlib/lstruct.h",
    "src/stdlib/lclass.h",
    "src/stdlib/lsuper.h",
    "src/stdlib/ltests.h",
]

# 输出文件路径
OUTPUT_FILE = "lxclua.h"

# ============================================================
# 全局状态
# ============================================================

# 已处理过的 guard 宏名集合，用于去重
_seen_guards = set()

# 已处理过的规范化路径集合，用于去重
_seen_paths = set()

# 已声明的类型名集合（typedef struct/enum/union 和单独的 struct/enum/union）
_declared_types = set()

# 输出行缓冲区
_output_lines = []


# ============================================================
# 核心逻辑
# ============================================================

def resolve_include(include_name, current_file_path):
    """根据 #include "xxx.h" 中的文件名，在 INCLUDE_DIRS 中搜索实际文件。"""
    # 先尝试相对于当前文件所在目录解析
    current_dir = os.path.dirname(os.path.abspath(current_file_path))
    candidate = os.path.normpath(os.path.join(current_dir, include_name))
    if os.path.isfile(candidate):
        return os.path.abspath(candidate)

    # 再在 INCLUDE_DIRS 中搜索
    for inc_dir in INCLUDE_DIRS:
        candidate = os.path.normpath(os.path.join(inc_dir, include_name))
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)

    # 递归搜索
    if '/' not in include_name and '\\' not in include_name:
        for inc_dir in INCLUDE_DIRS:
            for root, dirs, files in os.walk(inc_dir):
                if include_name in files:
                    return os.path.abspath(os.path.join(root, include_name))
    return None


def extract_guard_macro(lines):
    """从文件行列表中提取 include guard 宏名。"""
    for line in lines[:30]:
        m = re.match(r'^\s*#ifndef\s+(\w+)', line)
        if m:
            return m.group(1)
    return None


def find_guard_endif(lines, start_line=0):
    """找到与 #ifndef guard 配对的 #endif 行号。"""
    depth = 0
    for i in range(start_line, len(lines)):
        stripped = lines[i].strip()
        if (stripped.startswith('#ifndef') or stripped.startswith('#ifdef') or
            re.match(r'^#if\b', stripped)):
            depth += 1
        elif stripped.startswith('#endif'):
            depth -= 1
            if depth == 0:
                return i
    return -1


def extract_typedef_names(line):
    """从行中提取真正的类型定义名称。只匹配带 { 的定义行。"""
    names = []

    # 匹配: typedef struct/union/enum Name { （带 brace 才是真正的定义）
    m = re.match(r'^\s*typedef\s+(?:struct|union|enum)\s+([a-zA-Z_]\w*)\s*\{', line)
    if m:
        tag = m.group(1)
        if tag not in ('struct', 'union', 'enum'):
            names.append(tag)

    # 匹配: struct/union/enum Name { （非 typedef，必须有 { 才是定义）
    # 注意：不包括 typedef 的，避免重复匹配
    m = re.match(r'^\s*(?:struct|union|enum)\s+([a-zA-Z_]\w*)\s*\{', line)
    if m:
        tag = m.group(1)
        if tag not in ('struct', 'union', 'enum'):
            names.append(tag)

    return names


def process_file(file_path, indent=""):
    """递归处理一个头文件：读取内容，展开 #include "..."，输出到全局缓冲区。"""
    global _declared_types

    abs_path = os.path.abspath(file_path)

    # 路径级别去重
    norm_path = os.path.normpath(abs_path)
    if norm_path in _seen_paths:
        print(f"{indent}[跳过] {file_path} (已处理过相同路径)")
        return
    _seen_paths.add(norm_path)

    if not os.path.isfile(abs_path):
        print(f"{indent}[警告] 文件不存在: {abs_path}")
        return

    with open(abs_path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    if not lines:
        return

    # 提取 guard 宏名并检查是否已处理
    guard_macro = extract_guard_macro(lines)
    if guard_macro:
        if guard_macro in _seen_guards:
            print(f"{indent}[跳过] {file_path} (guard: {guard_macro} 已存在)")
            return
        _seen_guards.add(guard_macro)
        print(f"{indent}[展开] {file_path} (guard: {guard_macro})")
    else:
        print(f"{indent}[展开] {file_path} (无 guard)")

    # 来源注释
    rel_path = os.path.relpath(abs_path).replace('\\', '/')
    _output_lines.append(f"\n/* ====== BEGIN: {rel_path} ====== */\n")

    # 找到 guard 位置
    ifndef_line = -1
    endif_line = -1
    define_line = -1
    if guard_macro:
        for i, line in enumerate(lines[:40]):
            if re.match(rf'^\s*#ifndef\s+{re.escape(guard_macro)}\b', line):
                ifndef_line = i
                break
        if ifndef_line >= 0:
            endif_line = find_guard_endif(lines, ifndef_line)
            # 找到对应的 #define guard_macro 行
            for i in range(ifndef_line + 1, min(ifndef_line + 10, len(lines))):
                if re.match(rf'^\s*#define\s+{re.escape(guard_macro)}\b', lines[i]):
                    define_line = i
                    break

    # 逐行处理，跟踪是否正在跳过冲突的 struct 定义体
    i = 0
    skip_block_depth = 0  # >0 表示正在跳过一个冲突 struct 的 { ... } 体
    while i < len(lines):
        line = lines[i]

        # 处理 guard 的 #ifndef 行
        if i == ifndef_line:
            # 输出并缩进
            _output_lines.append(f"#ifndef {guard_macro}\n")
            i += 1
            # 跳到 #define guard 行
            if define_line >= 0:
                _output_lines.append(f"#define {guard_macro}\n")
                i = define_line + 1
            continue

        # 处理 guard 的 #endif 行（保留下面的缩进，只是输出得好看点）
        if i == endif_line:
            _output_lines.append(f"#endif /* {guard_macro} */\n")
            i += 1
            continue

        # 如果正在跳过冲突 struct 的 body，只跟踪 {} 深度
        if skip_block_depth > 0:
            # 统计该行的 { 和 }
            skip_block_depth += line.count('{') - line.count('}')
            if skip_block_depth <= 0:
                skip_block_depth = 0
                _output_lines.append(f"/* [merge] conflicting struct body skipped */\n")
            i += 1
            continue

        # 处理 #include 指令
        stripped = line.strip()
        if stripped.startswith('#include'):
            # 系统头文件 - 保留原样
            if re.match(r'^\s*#include\s*<', line):
                _output_lines.append(line)
                i += 1
                continue

            # 本地头文件 - 递归展开
            local_match = re.match(r'^\s*#include\s*"([^"]+)"', line)
            if local_match:
                include_name = local_match.group(1)
                resolved = resolve_include(include_name, abs_path)
                if resolved:
                    # 输出注释标记被展开的 include（对已展开的被跳过文件不显示）
                    rel_resolved = os.path.relpath(resolved).replace('\\', '/')
                    _output_lines.append(f"/* #include \"{include_name}\" -> {rel_resolved} */\n")
                    process_file(resolved, indent + "  ")
                else:
                    print(f"{indent}[警告] 无法解析 include: {include_name} (from {abs_path})")
                    _output_lines.append(f"/* [merge] unresolved: {line.rstrip()} */\n")
                i += 1
                continue

            # 其他 #include，保留原样
            _output_lines.append(line)
            i += 1
            continue

        # 检测类型定义冲突
        type_names = extract_typedef_names(line)
        conflicting = [n for n in type_names if n in _declared_types]

        if conflicting:
            # 该行定义了已存在的类型名，跳过整个 struct 体
            guard_name = f"LXCLUA_NOCONFLICT_{conflicting[0]}"
            _output_lines.append(f"/* [merge] {conflicting[0]} already defined; skipping entire definition */\n")
            # 统计当前行的 { 数量作为起始深度
            skip_block_depth = line.count('{') - line.count('}')
            if skip_block_depth <= 0:
                skip_block_depth = 1  # 至少 skip 一行
            i += 1
            continue
        else:
            _output_lines.append(line)
            # 记录新声明的类型名
            for n in type_names:
                _declared_types.add(n)

        i += 1

    # 来源结束注释
    _output_lines.append(f"/* ====== END: {rel_path} ====== */\n")


# ============================================================
# 主入口
# ============================================================

def main():
    """主函数：处理所有入口头文件并生成合并输出。"""
    global _seen_guards, _seen_paths, _declared_types, _output_lines

    _seen_guards = set()
    _seen_paths = set()
    _declared_types = set()
    _output_lines = []

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    os.chdir(project_dir)

    print(f"工作目录: {os.getcwd()}")
    print(f"输出文件: {OUTPUT_FILE}")
    print("=" * 50)

    from datetime import datetime
    _output_lines.append("/*\n")
    _output_lines.append(" * LXCLua 合并头文件 - 供 C 扩展模块开发使用\n")
    _output_lines.append(" * 自动生成，请勿手动编辑\n")
    _output_lines.append(f" * 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    _output_lines.append(" *\n")
    _output_lines.append(" * 使用方法:\n")
    _output_lines.append(' *   #include "lxclua.h"\n')
    _output_lines.append(" *\n")
    _output_lines.append(" * 此文件自包含，不依赖任何外部路径。\n")
    _output_lines.append(" */\n\n")

    # 顶层唯一 guard
    _output_lines.append("#ifndef LXCLUA_H\n")
    _output_lines.append("#define LXCLUA_H\n\n")

    # 处理所有入口头文件
    for header in ENTRY_HEADERS:
        abs_header = os.path.abspath(header)
        if os.path.isfile(abs_header):
            process_file(abs_header)
        else:
            print(f"[警告] 入口文件不存在: {header}")

    # 结束顶层 guard
    _output_lines.append("\n#endif /* LXCLUA_H */\n")

    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.writelines(_output_lines)

    line_count = len(_output_lines)
    print("=" * 50)
    print(f"合并完成: {OUTPUT_FILE} ({line_count} 行)")
    print(f"包含 {len(_seen_guards)} 个唯一头文件")
    if _declared_types:
        print(f"跟踪了 {len(_declared_types)} 个类型名（用于冲突检测）")


if __name__ == '__main__':
    main()