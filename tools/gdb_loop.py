#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 gdb 反复运行调试版 exe，直到抓到崩溃/挂起，保存回溯。
用法: python tools/gdb_loop.py <max_attempts> <timeout_sec> <exe> <script>
"""
import subprocess, sys, os

def main():
    attempts = int(sys.argv[1]); timeout = float(sys.argv[2])
    exe, script = sys.argv[3], sys.argv[4]
    for i in range(attempts):
        bt_file = "build/gdb_bt_%d.txt" % i
        cmd = ["gdb", "-batch", "-nx",
               "-ex", "set confirm off",
               "-ex", "set pagination off",
               "-ex", "run",
               "-ex", "echo \\n===GDB-CRASH===\\n",
               "-ex", "bt 40",
               "-ex", "info registers rip",
               "-ex", "quit",
               "--args", exe, script]
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            print("[attempt %d] GDB TIMEOUT (process hang captured partially)" % i)
            continue
        out = r.stdout.decode("utf-8", "replace")
        err = r.stderr.decode("utf-8", "replace")
        crashed = ("===GDB-CRASH===" in out) or r.returncode != 0
        hung_note = ""
        if "SIGSEGV" in out or "SIGABRT" in out or "access violation" in out.lower():
            crashed = True
        if crashed:
            with open(bt_file, "w", encoding="utf-8") as f:
                f.write("--- STDOUT ---\n"); f.write(out)
                f.write("--- STDERR ---\n"); f.write(err)
            print("[attempt %d] CRASH captured -> %s" % (i, bt_file))
            print(out[-4000:])
            return 0
        else:
            print("[attempt %d] OK run" % i)
    print("no crash in %d attempts" % attempts)
    return 1

if __name__ == "__main__":
    sys.exit(main())
