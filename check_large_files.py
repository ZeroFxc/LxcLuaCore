"""分析 git 历史中的大文件 - 批量版本"""
import subprocess

cwd = r"e:\Soft\Proje\LXCLUA-NCore\lua"

# 使用 git rev-list + 管道批量查询
rev_list = subprocess.Popen(
    ["git", "rev-list", "--objects", "--all"],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd
)

cat_file = subprocess.Popen(
    ["git", "cat-file", "--batch-check=%(objecttype) %(objectname) %(objectsize) %(rest)"],
    stdin=rev_list.stdout,
    stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd
)
rev_list.stdout.close()

output, _ = cat_file.communicate()
lines = output.decode('utf-8', errors='replace').strip().split('\n')

files = []
for line in lines:
    if not line.strip():
        continue
    parts = line.split(' ', 3)
    if len(parts) < 3 or parts[0] != 'blob':
        continue
    oid = parts[1]
    try:
        size = int(parts[2])
    except ValueError:
        continue
    filepath = parts[3] if len(parts) > 3 else "<unknown>"
    files.append((size, filepath, oid))

files.sort(key=lambda x: x[0], reverse=True)

# 排除源码文件（.c .h .lua 等）—— 这些是正常代码
source_exts = ('.c', '.h', '.lua', '.lxclua', '.mk', '.txt', '.md', '.java', '.xml', '.gradle', '.py')
# 排除已经在仓库但应保留的小文件类型
normal_exts = ('.c', '.h', '.lua', '.lxclua', '.mk', '.txt', '.md', '.java', '.xml', '.gradle', '.py',
               '.cpp', '.hpp', '.cs', '.html', '.css', '.json', '.yml', '.yaml', '.gitignore',
               '.gitattributes', '.gitmodules')

print("=== TOP 30 大文件（排除源码) ===")
shown = 0
suspicious = []
for size, filepath, oid in files:
    # 跳过正常的源码文件
    if any(filepath.endswith(ext) for ext in source_exts):
        continue
    
    if shown < 30:
        if size >= 1024 * 1024:
            size_str = f"{size / 1024 / 1024:.2f} MB"
        elif size >= 1024:
            size_str = f"{size / 1024:.2f} KB"
        else:
            size_str = f"{size} B"
        print(f"{shown+1:3d}. {size_str:>10s}  {filepath}")
        shown += 1

    # 收集可疑文件（匹配 .gitignore 模式）
    if size < 50 * 1024:
        continue
    if filepath.startswith("luatest/") or filepath.startswith("emsdk-main/") or \
       filepath.startswith("wasmtime/") or filepath.startswith("koishi/") or \
       filepath.startswith("release/") or filepath.startswith("test_output/") or \
       filepath.startswith(".qoder/") or \
       any(filepath.endswith(ext) for ext in ['.o', '.a', '.dll', '.so', '.dylib', '.exe',
            '.wasm', '.zip', '.tar.gz', '.luac', '.out', '.tmp', '.temp', '.log', '.bak',
            '.pdb', '.bat', '.swp', '.swo', '.DS_Store']):
        suspicious.append((size, filepath))

if suspicious:
    print(f"\n=== 应被 .gitignore 忽略但已提交的大文件（共{len(suspicious)}个）===")
    for size, filepath in suspicious[:50]:
        if size >= 1024 * 1024:
            size_str = f"{size / 1024 / 1024:.2f} MB"
        else:
            size_str = f"{size / 1024:.2f} KB"
        print(f"  {size_str:>10s}  {filepath}")

# 汇总
total_sus = sum(s for s, _ in suspicious)
print(f"\n=== 可疑文件总大小: {total_sus / 1024 / 1024:.2f} MB ===")