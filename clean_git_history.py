"""
从 git 历史中清除大文件和构建产物 (Windows 版)
"""
import subprocess
import sys
import time

CWD = r"e:\Soft\Proje\LXCLUA-NCore\lua"

def run(cmd):
    """运行 PowerShell 命令"""
    r = subprocess.run(["powershell", "-NoProfile", "-Command", cmd],
                       capture_output=True, text=True, cwd=CWD)
    return r.stdout.strip(), r.stderr.strip(), r.returncode

print("=== 从 git 历史中清除大文件 ===")
print(f"工作目录: {CWD}")

# 先确认没有未提交的更改
out, _, _ = run("git status --porcelain")
if out:
    print("WARNING: 工作区有未提交的更改:")
    print(out)
    resp = input("请先提交或 stash，然后按回车继续 (输入 q 取消): ")
    if resp.lower() == 'q':
        print("已取消")
        sys.exit(0)

# 构建 index-filter 脚本 (用于 filter-branch 的 --index-filter)
# Windows 上的 filter-branch 需要使用 cmd 或 bash
# 我们把删除命令写入一个文件然后调用

# 要删除的模式
patterns = [
    "luatest/",
    "*.dmp",
    "*.log",
    "replay_pid*",
    "hs_err_pid*",
    "*.o",
    "*.a",
    "*.so",
    "*.dll",
    "*.dylib",
    "*.exe",
    "*.pdb",
    "*.dSYM",
    "*.zip",
    "*.tar.gz",
    "*.bak",
    "*.tmp",
    "*.temp",
    "*.swp",
    "*.swo",
    "*~",
    "*.luac",
    "*.out",
    "*.wasm",
    "app/src/main/jniLibs/",
    "app/src/main/jni/obj/",
    "app/src/main/jni/vmp/",
    "jar/",
    "emsdk-main/",
    "wasmtime/",
    "koishi/",
    ".qoder/",
    "release/",
    "test_output/",
    "lxclua_standalone.html",
    "index.html",
    "qjsc*",
    "test_lua_*",
    "sta*",
    "stY*",
    "lua",
    "luac",
    "lxclua",
    "lbcdump",
    "*.bat",
]

# 生成删除脚本 (bash 风格，git filter-branch 在 Git Bash 中运行)
rm_lines = []
for p in patterns:
    rm_lines.append(f'git rm --cached --ignore-unmatch -r "{p}" 2>/dev/null')

# 写入 bash 脚本
script_content = "#!/bin/bash\n" + "\n".join(rm_lines) + "\ntrue\n"
script_path = CWD + r"\filter_script.sh"
with open(script_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(script_content)
print(f"\n已生成过滤脚本: {script_path}")

print(f"\n将删除 {len(patterns)} 类文件模式")
print("开始 filter-branch (869 commits)...\n")
start = time.time()

# 运行 filter-branch (需要在 Git Bash 中运行)
# 使用 bash 来执行
cmd = f'bash -c "git filter-branch --force --index-filter \'bash {script_path}\' --prune-empty --tag-name-filter cat -- --all"'
print(f"执行中...")

result = subprocess.run(
    ["bash", "-c", 
     f"git filter-branch --force --index-filter 'bash {script_path}' --prune-empty --tag-name-filter cat -- --all"],
    capture_output=True, text=True, cwd=CWD
)

elapsed = time.time() - start
print(f"\n耗时: {elapsed:.1f}s")
print(f"返回码: {result.returncode}")

if result.stdout:
    tail = result.stdout[-3000:] if len(result.stdout) > 3000 else result.stdout
    print(f"输出:\n{tail}")
if result.stderr:
    tail = result.stderr[-2000:] if len(result.stderr) > 2000 else result.stderr
    print(f"错误:\n{tail}")

if result.returncode != 0:
    print("\n[ERROR] filter-branch 失败!")
    sys.exit(1)

print("\n=== 清理 reference 和 GC ===")
run('git for-each-ref --format="%(refname)" refs/original/ | ForEach-Object { git update-ref -d $_ }')
run("git reflog expire --expire=now --all")
run("git gc --aggressive --prune=now")

print("\n=== 完成! ===")
print("如需推送到远程 (强制推送):")
print("  git push --force --all")
print("  git push --force --tags")