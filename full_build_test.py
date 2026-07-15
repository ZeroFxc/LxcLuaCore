import subprocess
import os

os.chdir(r"e:\Soft\Proje\LXCLUA-NCore\lua")

print("=== make clean build ===")
result = subprocess.run(["make", "clean"], capture_output=True, text=True)
result = subprocess.run(["make", "mingw"], capture_output=True, text=True)
print("STDOUT (tail 2000):")
print(result.stdout[-2000:])
print("STDERR (tail 5000):")
print(result.stderr[-5000:])
print("Exit code:", result.returncode)

if result.returncode == 0:
    print("\n=== Running test: lxclua.exe -e \"print('hello')\" ===")
    result = subprocess.run(["./lxclua.exe", "-e", "print('hello')"], capture_output=True, text=True)
    print("STDOUT:")
    print(result.stdout)
    print("STDERR:")
    print(result.stderr)
    print("Exit code:", result.returncode)
