#!/bin/sh
# 用 AddressSanitizer 构建核心模块，链接出 lxclua_asan.exe 用于定位内存错误
GCC="${GCC:-/e/Env/llvm-mingw-20260616-ucrt-x86_64/bin/gcc.exe}"
mkdir -p build/asan
INC="-Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm -Ipcre2"
DEF="-DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE -DGUI_PLATFORM_WINDOWS -D_UNICODE -DUNICODE -D_GNU_SOURCE"
FLAGS="-std=gnu11 -O1 -g -fsanitize=address -fno-omit-frame-pointer"
FILES="lapi.c lauxlib.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmap.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lclass.c lsuper.c lstruct.c lnamespace.c lbigint.c lthread.c lua.c"
DIRS="src/core src/compiler src/utils src/stdlib src/vm src/bin"
OK=1
for f in $FILES; do
  p=""
  for d in $DIRS; do
    if [ -f "$d/$f" ]; then p="$d/$f"; break; fi
  done
  if [ -z "$p" ]; then echo "NOT_FOUND: $f"; OK=0; break; fi
  o="build/asan/$(basename "$f" .c)_as.o"
  "$GCC" $FLAGS $INC $DEF -c "$p" -o "$o" || { echo "COMPILE_FAIL: $p"; OK=0; break; }
done
[ "$OK" = "1" ] || exit 1
"$GCC" $FLAGS -o build/lxclua_asan.exe build/asan/*_as.o \
  -Wl,--allow-multiple-definition \
  liblxclua.a \
  -lm -lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 \
  -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32 \
  wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api/lib/libwasmtime.a \
  -lbcrypt -luserenv -lntdll \
  -Wl,--stack,16777216 \
  -Wl,--image-base,0x10000000 || { echo "LINK_FAIL"; exit 2; }
echo "ASAN_BUILD_OK"
