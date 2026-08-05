#!/bin/sh
# 链接带符号的调试版 lxclua（不 strip），显式使用 llvm-mingw 工具链
GCC="${GCC:-/e/Env/llvm-mingw-20260616-ucrt-x86_64/bin/gcc.exe}"
"$GCC" -std=gnu11 -O2 -pipe -o build/lxclua_dbg.exe build/obj/lua.o liblxclua.a \
  -lm -lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 \
  -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32 \
  wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api/lib/libwasmtime.a \
  -lbcrypt -luserenv -lntdll \
  -Wl,--stack,16777216
echo "LINK_EXIT=$?"
