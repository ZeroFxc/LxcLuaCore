#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
带超时保护的 lxclua 测试运行器
用法:
    python tools/run_timeout.py <timeout_sec> <exe> [args...]
示例:
    python tools/run_timeout.py 20 lxclua.exe test_2lv.lua
行为:
    - 正常退出: 打印 stdout/stderr, 退出码 = 进程退出码
    - 超时: 杀掉进程树, 打印 "TIMEOUT", 退出码 = 124
"""
import subprocess
import sys
import os
import time

def main():
    if len(sys.argv) < 3:
        print("usage: run_timeout.py <timeout_sec> <exe> [args...]", file=sys.stderr)
        return 2
    try:
        timeout = float(sys.argv[1])
    except ValueError:
        print("invalid timeout: " + sys.argv[1], file=sys.stderr)
        return 2
    cmd = sys.argv[2:]
    t0 = time.time()
    # CREATE_NEW_PROCESS_GROUP 便于整组杀掉
    flags = 0
    if os.name == "nt":
        flags = subprocess.CREATE_NEW_PROCESS_GROUP
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            creationflags=flags,
        )
    except Exception as e:
        print("LAUNCH_ERR: %s" % e, file=sys.stderr)
        return 3
    try:
        out, err = proc.communicate(timeout=timeout)
        elapsed = time.time() - t0
        sys.stdout.write(out.decode("utf-8", errors="replace"))
        sys.stderr.write(err.decode("utf-8", errors="replace"))
        sys.stderr.write("[runner] exit=%d elapsed=%.2fs\n" % (proc.returncode, elapsed))
        return proc.returncode
    except subprocess.TimeoutExpired:
        # 超时：杀进程树
        try:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                proc.kill()
            out, err = proc.communicate(timeout=5)
        except Exception:
            out, err = b"", b""
        elapsed = time.time() - t0
        sys.stdout.write(out.decode("utf-8", errors="replace") if out else "")
        sys.stderr.write(err.decode("utf-8", errors="replace") if err else "")
        sys.stderr.write("[runner] TIMEOUT after %.2fs (limit %.1fs), process killed\n"
                         % (elapsed, timeout))
        return 124

if __name__ == "__main__":
    sys.exit(main())
