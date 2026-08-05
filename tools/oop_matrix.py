#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OOP 特性矩阵测试：运行一批 lua 文件并汇总结果。
用法: python tools/oop_matrix.py <timeout_sec> <exe> <file1> [file2 ...]
每个文件: 退出码 0 且无 'error' 即 OK。失败时保存输出到 build/mat_<name>.txt
"""
import subprocess, sys, os, glob

def run_one(exe, f, timeout):
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    p = subprocess.Popen([exe, f], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, creationflags=flags)
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
    timeout = float(sys.argv[1]); exe = sys.argv[2]
    files = []
    for pat in sys.argv[3:]:
        matched = glob.glob(pat)
        files.extend(sorted(matched) if matched else [pat])
    ok = 0; fails = []
    for f in files:
        rc, out, err, to = run_one(exe, f, timeout)
        o = out.decode("utf-8", "replace"); e = err.decode("utf-8", "replace")
        # 过滤编译期 warning 行，仅保留真正 error
        err_lines = [l for l in e.splitlines()
                     if "warning:" not in l and l.strip() != ""]
        good = (not to) and rc == 0 and not any(
            ("error" in l.lower() or "traceback" in l.lower()) for l in err_lines)
        tag = os.path.basename(f)
        if good:
            ok += 1
            print("OK   %s" % tag)
        else:
            reason = "TIMEOUT" if to else ("rc=%d" % rc)
            if err_lines:
                reason += " | " + err_lines[0][:120]
            print("FAIL %s  (%s)" % (tag, reason))
            fails.append((tag, reason))
            fn = "build/mat_%s.txt" % tag
            with open(fn, "wb") as fp:
                fp.write(b"--- STDOUT ---\n"); fp.write(out)
                fp.write(b"--- STDERR ---\n"); fp.write(err)
    print("== SUMMARY: %d/%d OK ==" % (ok, len(files)))
    for t, r in fails:
        print("  FAIL: %s  (%s)" % (t, r))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
