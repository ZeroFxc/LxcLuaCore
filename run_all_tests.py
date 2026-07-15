import subprocess, os, glob, sys

test_dir = "test"
files = sorted(glob.glob(os.path.join(test_dir, "*.lua")))
# 排除以 _ 开头的测试碎片文件
files = [f for f in files if not os.path.basename(f).startswith("_")]
results = {"ok": 0, "fail": 0, "crash": 0, "timeout": 0}
fail_list = []
total = len(files)

for i, f in enumerate(files):
    name = os.path.basename(f)
    # test_regex_bench 执行 1000 万次匹配，需要更长超时
    test_timeout = 30 if name == "test_regex_bench.lua" else 10
    try:
        r = subprocess.run(
            ["./lxclua.exe", f],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=test_timeout, cwd="."
        )
        if r.returncode == 0:
            results["ok"] += 1
        elif r.returncode == 3221225477 or r.returncode == -1073741819:
            results["crash"] += 1
            fail_list.append(name + " (CRASH)")
        else:
            results["fail"] += 1
            err = r.stderr.strip()
            if not err and r.stdout:
                err = r.stdout.strip()[-100:]
            fail_list.append(
                name + " (exit=" + str(r.returncode) + ") " + err[-80:]
            )
    except subprocess.TimeoutExpired:
        results["timeout"] += 1
        fail_list.append(name + " (TIMEOUT)")
    except Exception as e:
        results["fail"] += 1
        fail_list.append(name + " (" + str(e) + ")")

print(
    "OK: "
    + str(results["ok"])
    + "/"
    + str(total)
    + "  FAIL: "
    + str(results["fail"])
    + "  CRASH: "
    + str(results["crash"])
    + "  TIMEOUT: "
    + str(results["timeout"])
)
if fail_list:
    print()
    for item in fail_list:
        print("  " + item)