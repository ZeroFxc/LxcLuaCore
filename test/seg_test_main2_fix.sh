#!/bin/bash
# 分段测试 main(2).lua - preamble + runner 方式 + diff 对比
cd "$(dirname "$0")/.."

LUA=./lxclua.exe
OUTDIR=build/segtest
mkdir -p "$OUTDIR"
RUNNER="test/c/lbctc_runner2.c"

SYSLIBS="-lwininet -lws2_32 -lpsapi -lpthread -lsecur32 -lcrypt32"
MYCFLAGS="-Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Ipcre2 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H -Iwasmtime/wasmtime-v48.0.1-x86_64-mingw-c-api/include"

# 分段定义: 起始行 结束行 名称
# 注意：从 719 行开始的段落需要 printf/error，从 1690 开始的还需要 ok/assert_eq/assert_true
SEGMENTS=(
    "1 121 seg0_for_step"
    "123 247 seg1_vararg"
    "249 401 seg2_goto"
    "405 499 seg3_upvalues"
    "501 597 seg4_bool_varargs"
    "601 713 seg5_meta_huge"
    "719 875 seg6_loadk_table"
    "877 1033 seg7_arith_for"
    "1037 1156 seg8_eq_closure"
    "1158 1303 seg9_nested_closure"
    "1305 1450 seg10_ast_a"
    "1451 1689 seg10_ast_b"
    "1690 1830 seg11_native_a"
    "1831 2038 seg11_native_b"
    "2039 2300 seg12_final_a"
    "2301 2583 seg12_final_b"
)

TOTAL=${#SEGMENTS[@]}
PASS=0
FAIL=0
RESULTS=""

for seg in "${SEGMENTS[@]}"; do
    read start end name <<< "$seg"

    # 提取段落
    awk "NR>=$((start)) && NR<=$((end))" "main(2).lua" > "$OUTDIR/${name}_body.lua"

    # 根据 start 行决定是否需要 preamble
    NEED_PREAMBLE=0
    if [ "$start" -ge 719 ]; then
        NEED_PREAMBLE=1
    fi

    # 组装完整测试文件
    if [ "$NEED_PREAMBLE" -eq 1 ]; then
        cat "$OUTDIR/preamble.lua" "$OUTDIR/${name}_body.lua" > "$OUTDIR/${name}.lua"
    else
        cp "$OUTDIR/${name}_body.lua" "$OUTDIR/${name}.lua"
    fi

    # 先用解释器跑，看是否本身就有语法/运行错误
    $LUA "$OUTDIR/${name}.lua" > "$OUTDIR/${name}_lua.txt" 2>&1
    lua_rc=$?
    if [ $lua_rc -ne 0 ]; then
        echo "[$name] LUA_INTERP_ERR"
        head -5 "$OUTDIR/${name}_lua.txt"
        # 如果解释器都跑不过，说明分段截断了，跳过
        FAIL=$((FAIL+1))
        RESULTS="$RESULTS\n  $name: LUA_INTERP_ERR"
        continue
    fi

    # tcc.compile 生成 C 代码
    $LUA -e "
        local t=require'tcc'
        local f=io.open('$OUTDIR/${name}.lua','rb')
        local src=f:read('*a'); f:close()
        local c,err=t.compile(src,0)
        if not c then print('GEN_ERR: '..tostring(err)); os.exit(2) end
        local fp=io.open('$OUTDIR/${name}.c','wb')
        fp:write(c); fp:close()
        print('GEN_OK')
    " > "$OUTDIR/${name}_gen.log" 2>&1

    if ! grep -q "GEN_OK" "$OUTDIR/${name}_gen.log"; then
        echo "[$name] GEN_ERR"
        cat "$OUTDIR/${name}_gen.log"
        FAIL=$((FAIL+1))
        RESULTS="$RESULTS\n  $name: GEN_ERR"
        continue
    fi

    # 编译为 exe
    gcc -std=gnu11 -O2 -pipe $MYCFLAGS -I. -o "$OUTDIR/${name}.exe" "$RUNNER" \
        -include src/stdlib/lclass.h \
        -include "$OUTDIR/${name}.c" liblxclua.a $SYSLIBS -lm \
        > "$OUTDIR/${name}_gcc.log" 2>&1

    if [ $? -ne 0 ]; then
        echo "[$name] GCC_ERR"
        grep -i 'error:' "$OUTDIR/${name}_gcc.log" | head -10
        FAIL=$((FAIL+1))
        RESULTS="$RESULTS\n  $name: GCC_ERR"
        continue
    fi

    # 运行编译后的 exe
    "$OUTDIR/${name}.exe" > "$OUTDIR/${name}_run.txt" 2>&1
    rc=$?

    # 对比输出 (剥离 filename:line: 前缀, 排序相同首token的连续行)
    # 先规范化两个输出文件
    sed 's/^[^:]*:[0-9]*: //' "$OUTDIR/${name}_lua.txt" | grep -v "^warning:" | grep -v "\[unused\]" | sort -t' ' -k1,1 > "$OUTDIR/${name}_lua_norm.txt" 2>/dev/null
    sed 's/^[^:]*:[0-9]*: //' "$OUTDIR/${name}_run.txt" | grep -v "^warning:" | grep -v "\[unused\]" | sort -t' ' -k1,1 > "$OUTDIR/${name}_run_norm.txt" 2>/dev/null

    if [ $rc -ne 0 ]; then
        echo "[$name] RUN_ERR(rc=$rc)"
        echo "--- 期望输出(解释器) ---"
        head -5 "$OUTDIR/${name}_lua.txt"
        echo "--- 实际输出(编译) ---"
        head -10 "$OUTDIR/${name}_run.txt"
        FAIL=$((FAIL+1))
        RESULTS="$RESULTS\n  $name: RUN_ERR"
    elif diff -q "$OUTDIR/${name}_lua_norm.txt" "$OUTDIR/${name}_run_norm.txt" > /dev/null 2>&1; then
        echo "[$name] PASS"
        PASS=$((PASS+1))
        RESULTS="$RESULTS\n  $name: PASS"
    else
        echo "[$name] DIFF"
        echo "--- diff (lua vs compiled) ---"
        diff "$OUTDIR/${name}_lua_norm.txt" "$OUTDIR/${name}_run_norm.txt" | head -15
        FAIL=$((FAIL+1))
        RESULTS="$RESULTS\n  $name: DIFF"
    fi
done

echo ""
echo "========================================"
echo "分段测试结果: PASS=$PASS FAIL=$FAIL Total=$TOTAL"
echo "========================================"
echo -e "$RESULTS"
