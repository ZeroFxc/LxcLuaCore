"""批量编译测试 - 所有测试文件"""
import subprocess
import os

test_dir = "test"
tests = []
for f in os.listdir(test_dir):
    if f.endswith(".lua") and f.startswith("test_"):
        tests.append(f)

ok = []
fail = []
skip = 0
for t in sorted(tests):
    path = os.path.join(test_dir, t)
    out = path.replace(".lua", ".luac")
    try:
        r = subprocess.run(["luac.exe", "-o", out, path], capture_output=True, text=True, timeout=30)
        if r.returncode == 0:
            ok.append(t)
        else:
            err = r.stderr.strip().split('\n')[-1] if r.stderr.strip() else "unknown"
            # 只取最后一行错误
            fail.append((t, err[:150]))
    except Exception as e:
        fail.append((t, str(e)[:150]))

print(f"\n=== OK ({len(ok)}) ===")
for t in ok:
    print(f"  OK: {t}")
print(f"\n=== FAIL ({len(fail)}) ===")
for t, err in fail:
    print(f"  FAIL: {t}")
    print(f"        {err}")