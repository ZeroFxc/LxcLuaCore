#!/usr/bin/env python3
"""LXCLUA 测试运行器 - 带超时自动终止"""

import subprocess
import sys
import os
import glob
import time

LXCLUA = os.path.join(os.path.dirname(__file__), "lxclua.exe")
TEST_DIR = os.path.join(os.path.dirname(__file__), "test")
TIMEOUT = 30  # 每个测试30秒超时

def run_test(test_file):
    """运行单个测试，返回 (passed, output, elapsed)"""
    test_path = os.path.join(TEST_DIR, test_file) if not os.path.isabs(test_file) else test_file
    print(f"\n{'='*60}")
    print(f"运行: {test_file}")
    print(f"{'='*60}")
    
    start = time.time()
    try:
        proc = subprocess.run(
            [LXCLUA, test_path],
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
            cwd=os.path.dirname(__file__),
            env={**os.environ, "PYTHONUTF8": "1"}
        )
        elapsed = time.time() - start
        output = proc.stdout + proc.stderr
        passed = proc.returncode == 0
        
        # 限制输出长度
        if len(output) > 5000:
            output = output[:5000] + "\n... (输出截断)"
        
        print(output)
        
        if proc.returncode == 0:
            print(f"[OK] {test_file} ({elapsed:.1f}s)")
        else:
            print(f"[FAIL] {test_file} (exit={proc.returncode}, {elapsed:.1f}s)")
        
        return passed, output, elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.time() - start
        print(f"[TIMEOUT] {test_file} ({elapsed:.1f}s) - 超时 {TIMEOUT}s，已终止")
        return False, "", elapsed
    except Exception as e:
        elapsed = time.time() - start
        print(f"[ERROR] {test_file}: {e}")
        return False, str(e), elapsed

def main():
    # 获取所有测试文件
    if len(sys.argv) > 1:
        test_files = sys.argv[1:]
    else:
        test_files = sorted(glob.glob(os.path.join(TEST_DIR, "*.lua")))
        # 只取文件名
        test_files = [os.path.basename(f) for f in test_files]
    
    if not test_files:
        print("没有找到测试文件")
        return
    
    # 检查 lxclua.exe
    if not os.path.exists(LXCLUA):
        print(f"错误: 找不到 {LXCLUA}")
        return
    
    print(f"找到 {len(test_files)} 个测试文件")
    
    results = []
    total_start = time.time()
    
    for tf in test_files:
        passed, output, elapsed = run_test(tf)
        results.append((tf, passed, elapsed, output))
        sys.stdout.flush()
    
    total_elapsed = time.time() - total_start
    
    # 汇总
    passed = sum(1 for _, p, _, _ in results if p)
    failed = len(results) - passed
    
    print(f"\n{'='*60}")
    print(f"汇总: {len(results)} 测试, {passed} 通过, {failed} 失败 ({total_elapsed:.1f}s)")
    print(f"{'='*60}")
    
    # 列出失败的
    if failed > 0:
        print("\n失败/超时列表:")
        for tf, p, e, _ in results:
            if not p:
                print(f"  - {tf} ({e:.1f}s)")
    
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())