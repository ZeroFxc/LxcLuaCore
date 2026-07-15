import subprocess, os, glob, sys

test_dir = "test"
files = sorted(glob.glob(os.path.join(test_dir, "*.lua")))
results = {"ok": [], "fail": []}
total = len(files)

for i, f in enumerate(files):
    name = os.path.basename(f)
    try:
        r = subprocess.run(
            ["./lxclua.exe", f],
            capture_output=True, text=True, timeout=10,
            cwd=os.path.dirname(os.path.abspath(__file__))
        )
        if r.returncode == 0:
            results["ok"].append(name)
            print(f"[{i+1:3d}/{total}] OK   {name}")
        else:
            results["fail"].append((name, r.returncode, r.stderr.strip()[-200:] if r.stderr else ""))
            print(f"[{i+1:3d}/{total}] FAIL {name} (exit={r.returncode})")
    except subprocess.TimeoutExpired:
        results["fail"].append((name, -1, "TIMEOUT"))
        print(f"[{i+1:3d}/{total}] FAIL {name} (TIMEOUT)")
    except Exception as e:
        results["fail"].append((name, -2, str(e)))
        print(f"[{i+1:3d}/{total}] FAIL {name} (ERR: {e})")

print(f"\n===== RESULTS =====")
print(f"OK:   {len(results['ok'])}/{total}")
print(f"FAIL: {len(results['fail'])}/{total}")
if results["fail"]:
    print(f"\nFailed tests:")
    for name, code, err in results["fail"]:
        print(f"  {name} (exit={code})")
        if err:
            print(f"    {err[:200]}")