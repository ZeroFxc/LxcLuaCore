#!/bin/bash
# Git filter-branch index-filter script
# 从每个提交的索引中删除不应跟踪的文件

git rm --cached --ignore-unmatch -r \
  "luatest/" \
  "*.dmp" \
  "*.log" \
  "replay_pid*" \
  "hs_err_pid*" \
  "*.o" \
  "*.a" \
  "*.so" \
  "*.dll" \
  "*.dylib" \
  "*.exe" \
  "*.pdb" \
  "*.zip" \
  "*.tar.gz" \
  "*.bak" \
  "*.tmp" \
  "*.temp" \
  "*.swp" \
  "*.swo" \
  "*~" \
  "*.luac" \
  "*.out" \
  "*.wasm" \
  "app/src/main/jniLibs/" \
  "app/src/main/jni/obj/" \
  "app/src/main/jni/vmp/" \
  "jar/" \
  "emsdk-main/" \
  "wasmtime/" \
  "koishi/" \
  ".qoder/" \
  "release/" \
  "test_output/" \
  "lxclua_standalone.html" \
  "lxclua*.html" \
  "index.html" \
  "qjsc*" \
  "test_lua_*" \
  "sta*" \
  "stY*" \
  "lua" \
  "luac" \
  "lxclua" \
  "lbcdump" \
  "*.bat" \
  2>/dev/null

true