import subprocess
import os
import sys

os.chdir(r"e:\Soft\Proje\LXCLUA-NCore\lua")

print("=== Running make clean ===")
result = subprocess.run(["make", "clean"], capture_output=True, text=True)
print("STDOUT:", result.stdout)
print("STDERR:", result.stderr)
print("Exit code:", result.returncode)

print("\n=== Running make mingw ===")
result = subprocess.run(["make", "mingw"], capture_output=True, text=True)
print("STDOUT:", result.stdout[-5000:] if len(result.stdout) > 5000 else result.stdout)
print("STDERR:", result.stderr[-5000:] if len(result.stderr) > 5000 else result.stderr)
print("Exit code:", result.returncode)
