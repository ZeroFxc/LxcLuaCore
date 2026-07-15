import subprocess
import os
import sys

os.chdir(r"e:\Soft\Proje\LXCLUA-NCore\lua")

cflags = "-std=gnu11 -pipe -O2 -Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm -Ipcre2 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H"
files = [
    "src/compiler/last.c",
    "src/compiler/last_parse.c",
    "src/compiler/lcodegen.c",
]
for f in files:
    obj = "build/obj/" + os.path.basename(f).replace(".c", ".o")
    print(f"=== Recompiling {f} ===")
    result = subprocess.run(f"gcc {cflags} -c {f} -o {obj}", capture_output=True, text=True)
    print("STDERR:", result.stderr)
    print("Exit:", result.returncode)
    if result.returncode != 0:
        sys.exit(1)

print("\n=== Rebuilding static library ===")
import glob
objs = glob.glob("build/obj/*.o") + ["quickjs/quickjs.o", "quickjs/libregexp.o", "quickjs/libunicode.o", "quickjs/cutils.o", "quickjs/quickjs-libc.o", "quickjs/dtoa.o"]
subprocess.run(["rm", "-f", "liblxclua.a"], capture_output=True)
ar_cmd = ["ar", "rcu", "liblxclua.a"] + objs
result = subprocess.run(ar_cmd, capture_output=True, text=True)
print("AR STDOUT:", result.stdout)
print("AR STDERR:", result.stderr)
print("AR Exit:", result.returncode)

result = subprocess.run(["ranlib", "liblxclua.a"], capture_output=True, text=True)
print("RANLIB Exit:", result.returncode)

print("\n=== Linking lxclua.exe ===")
syslibs = "-lm -lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32 -lbcrypt -luserenv -lntdll"
link_cmd = f"gcc -std=gnu11 -pipe -o lxclua.exe -s -Wl,--stack,16777216 build/obj/lua.o liblxclua.a wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api/lib/libwasmtime.a {syslibs}".split()
result = subprocess.run(link_cmd, capture_output=True, text=True)
print("LINK STDOUT:", result.stdout)
print("LINK STDERR:", result.stderr)
print("LINK Exit:", result.returncode)
if result.returncode != 0:
    sys.exit(1)

print("\n=== Running test ===")
result = subprocess.run(["./lxclua.exe", "-e", "print('hello')"], capture_output=True, text=True)
print("STDOUT:")
print(result.stdout)
print("STDERR:")
print(result.stderr)
print("Exit code:", result.returncode)
