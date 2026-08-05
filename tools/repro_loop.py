#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批量重现非确定性崩溃：反复运行 lxclua 测试，失败时保存完整输出。
用法: python tools/repro_loop.py <count> <timeout_sec> <exe> <script> [expected_substr]
"""
import subprocess, sys, os, time

def run_once(cmd, timeout):
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         creationflags=flags)
    try:
        out, err = p.communicate(timeout=timeout)
        return p.returncode, out, err, False
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            p.kill()
        out, err = p.communicate()
        return -999, out, err, True

def main():
    count = int(sys.argv[1]); timeout = float(sys.argv[2])
    cmd = sys.argv[3:5]; expected = sys.argv[5] if len(sys.argv) > 5 else "PASSED"
    fails = 0
    for i in range(count):
        rc, out, err, timed_out = run_once(cmd, timeout)
        o = out.decode("utf-8", "replace"); e = err.decode("utf-8", "replace")
        ok = (not timed_out) and rc == 0 and expected in o
        if not ok:
            fails += 1
            fn = "build/fail_%d.txt" % i
            with open(fn, "wb") as f:
                f.write(("=== RC=%s timeout=%s ===\n" % (rc, timed_out)).encode())
                f.write(b"--- STDOUT ---\n"); f.write(out)
                f.write(b"--- STDERR ---\n"); f.write(err)
            print("[run %d] FAIL rc=%d timeout=%s saved=%s" % (i, rc, timed_out, fn))
        else:
            print("[run %d] OK" % i)
        time.sleep(0.05)
    print("TOTAL: %d/%d failed" % (fails, count))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
