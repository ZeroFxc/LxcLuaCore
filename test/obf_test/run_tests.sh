#!/bin/bash
# LXCLUA Obfuscator Comprehensive Test Suite v2
# Fix: baseline = compile+run bytecode (no compiler warnings)
# Fix: per-test timeout to handle infinite loops
# Fix: handle segfault (rc=139) gracefully

cd "$(dirname "$0")/../.." || cd "E:/Soft/Proje/LXCLUA-NCore/lua"

CASES_DIR="test/obf_test/cases"
OUT_DIR="test/obf_test/run"
REPORT_DIR="test/obf_test/report"
mkdir -p "$OUT_DIR" "$REPORT_DIR"

LUAC="./luac.exe"
LXCLUA="./lxclua.exe"
TIMEOUT_CMD="timeout"  # git-bash has timeout
PER_TEST_TIMEOUT=10

# Obfuscation options
OPTIONS=(
    "-f|CFF"
    "-B|BlockShuffle"
    "-g|BogusBlocks"
    "-P|OpaquePredicates"
    "-r|RandomNOP"
    "-e|StateEncode"
    "-b|BinaryDispatcher"
    "-i|FuncInterleave"
    "-n|NestedDispatcher"
    "-m|VMProtect"
    "-E|StringEncryption"
)

PRESETS=(
    "-O1|Preset-O1-Light"
    "-O2|Preset-O2-Medium"
    "-O3|Preset-O3-Heavy"
    "-Oa|Preset-Oa-All"
)

RESULTS_FILE="$REPORT_DIR/results.csv"
DETAIL_FILE="$REPORT_DIR/details.txt"
SUMMARY_FILE="$REPORT_DIR/summary.txt"
echo "test_case,option,flag,compile_rc,run_rc,output_match,size_base,size_obf,size_changed,status" > "$RESULTS_FILE"
> "$DETAIL_FILE"

TOTAL=0
PASS=0
COMPILE_FAIL=0
RUN_FAIL=0
RUN_TIMEOUT=0
OUTPUT_DIFF=0

# Generate baseline: compile to bytecode (no warnings) then run bytecode
echo "Generating baselines (compile + run bytecode)..."
for f in "$CASES_DIR"/*.lua; do
    name=$(basename "$f" .lua)
    $LUAC -o "$OUT_DIR/${name}_base.out" "$f" 2>/dev/null
    $LXCLUA "$OUT_DIR/${name}_base.out" > "test/obf_test/${name}_base.txt" 2>&1
    echo "  baseline: $name ($(wc -l < "test/obf_test/${name}_base.txt") lines)"
done

run_test() {
    local case_name="$1"
    local flag="$2"
    local desc="$3"
    local case_file="$CASES_DIR/${case_name}.lua"
    local base_file="test/obf_test/${case_name}_base.txt"
    local tag="${flag//[\/ -]/_}"
    local obf_out="$OUT_DIR/${case_name}_${tag}.out"
    local obf_run="$OUT_DIR/${case_name}_${tag}.run"
    local obf_err="$OUT_DIR/${case_name}_${tag}.err"
    local size_base=$(wc -c < "$OUT_DIR/${case_name}_base.out" 2>/dev/null || echo 0)

    TOTAL=$((TOTAL + 1))

    # Step 1: Compile
    $LUAC $flag -o "$obf_out" "$case_file" 2>"$obf_err"
    local compile_rc=$?

    if [ $compile_rc -ne 0 ]; then
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        echo "${case_name},${flag},${desc},${compile_rc},,FAIL,${size_base},,,COMPILE_FAIL" >> "$RESULTS_FILE"
        echo "  [COMPILE_FAIL] ${case_name} | ${desc} | $(head -1 "$obf_err")" >> "$DETAIL_FILE"
        return
    fi

    # Step 2: Run with timeout
    $TIMEOUT_CMD $PER_TEST_TIMEOUT $LXCLUA "$obf_out" > "$obf_run" 2>&1
    local run_rc=$?

    local size_obf=$(wc -c < "$obf_out" 2>/dev/null || echo 0)
    local size_changed="NO"
    [ "$size_base" != "$size_obf" ] && size_changed="YES"

    # Check timeout (rc=124 from timeout command)
    if [ $run_rc -eq 124 ]; then
        RUN_TIMEOUT=$((RUN_TIMEOUT + 1))
        echo "${case_name},${flag},${desc},${compile_rc},${run_rc},TIMEOUT,${size_base},${size_obf},${size_changed},RUN_TIMEOUT" >> "$RESULTS_FILE"
        echo "  [TIMEOUT] ${case_name} | ${desc} | exceeded ${PER_TEST_TIMEOUT}s" >> "$DETAIL_FILE"
        return
    fi

    # Check crash (rc=139 = segfault, rc=134 = abort, rc != 0)
    if [ $run_rc -ne 0 ]; then
        RUN_FAIL=$((RUN_FAIL + 1))
        local sig=""
        [ $run_rc -eq 139 ] && sig=" (SIGSEGV)"
        [ $run_rc -eq 134 ] && sig=" (SIGABRT)"
        echo "${case_name},${flag},${desc},${compile_rc},${run_rc},FAIL,${size_base},${size_obf},${size_changed},RUN_FAIL${sig}" >> "$RESULTS_FILE"
        echo "  [CRASH] ${case_name} | ${desc} | rc=${run_rc}${sig} | $(head -1 "$obf_run")" >> "$DETAIL_FILE"
        return
    fi

    # Step 3: Compare output (exact match - both are bytecode runs, no warnings)
    if diff -q "$base_file" "$obf_run" >/dev/null 2>&1; then
        PASS=$((PASS + 1))
        echo "${case_name},${flag},${desc},${compile_rc},${run_rc},PASS,${size_base},${size_obf},${size_changed},PASS" >> "$RESULTS_FILE"
        echo "  [PASS] ${case_name} | ${desc} | size: ${size_base}->${size_obf} (changed=${size_changed})" >> "$DETAIL_FILE"
    else
        OUTPUT_DIFF=$((OUTPUT_DIFF + 1))
        echo "${case_name},${flag},${desc},${compile_rc},${run_rc},DIFF,${size_base},${size_obf},${size_changed},OUTPUT_DIFF" >> "$RESULTS_FILE"
        echo "  [DIFF] ${case_name} | ${desc} | output differs:" >> "$DETAIL_FILE"
        # Show first diff line
        local first_diff=$(diff "$base_file" "$obf_run" 2>/dev/null | grep '^[<>]' | head -6)
        echo "$first_diff" | sed 's/^/    /' >> "$DETAIL_FILE"
    fi
}

echo ""
echo "========================================"
echo "  Testing Individual Options"
echo "========================================"

for opt_entry in "${OPTIONS[@]}"; do
    flag="${opt_entry%%|*}"
    desc="${opt_entry##*|}"
    echo ""
    echo "--- $flag ($desc) ---"
    echo "" >> "$DETAIL_FILE"
    echo "=== $flag ($desc) ===" >> "$DETAIL_FILE"
    for f in "$CASES_DIR"/*.lua; do
        name=$(basename "$f" .lua)
        run_test "$name" "$flag" "$desc"
    done
done

echo ""
echo "========================================"
echo "  Testing Presets"
echo "========================================"

for preset_entry in "${PRESETS[@]}"; do
    flag="${preset_entry%%|*}"
    desc="${preset_entry##*|}"
    echo ""
    echo "--- $flag ($desc) ---"
    echo "" >> "$DETAIL_FILE"
    echo "=== $flag ($desc) ===" >> "$DETAIL_FILE"
    for f in "$CASES_DIR"/*.lua; do
        name=$(basename "$f" .lua)
        run_test "$name" "$flag" "$desc"
    done
done

# Generate summary
echo ""
echo "========================================"
echo "  Test Summary"
echo "========================================"
echo "  Total:         $TOTAL"
echo "  PASS:          $PASS"
echo "  COMPILE_FAIL:  $COMPILE_FAIL"
echo "  RUN_FAIL:      $RUN_FAIL"
echo "  RUN_TIMEOUT:   $RUN_TIMEOUT"
echo "  OUTPUT_DIFF:   $OUTPUT_DIFF"
if [ $TOTAL -gt 0 ]; then
    echo "  Pass rate:     $(echo "scale=1; $PASS * 100 / $TOTAL" | bc 2>/dev/null || echo "?")%"
fi
echo "========================================"

# Per-option breakdown
echo ""
echo "Per-option results:"
echo "option,pass,fail,timeout,total" >> "$RESULTS_FILE"
for opt_entry in "${OPTIONS[@]}" "${PRESETS[@]}"; do
    flag="${opt_entry%%|*}"
    desc="${opt_entry##*|}"
    opt_pass=$(grep ",${flag}," "$RESULTS_FILE" | grep -c ",PASS$")
    opt_fail=$(grep ",${flag}," "$RESULTS_FILE" | grep -cE ",(COMPILE_FAIL|RUN_FAIL|RUN_TIMEOUT|OUTPUT_DIFF)$")
    echo "  $flag ($desc): $opt_pass pass / $opt_fail fail"
done

echo ""
echo "Results: $RESULTS_FILE"
echo "Details: $DETAIL_FILE"

# Save summary
{
    echo "LXCLUA Obfuscator Test Report"
    echo "Generated: $(date)"
    echo ""
    echo "Total: $TOTAL  Pass: $PASS  CompileFail: $COMPILE_FAIL  RunFail: $RUN_FAIL  Timeout: $RUN_TIMEOUT  OutputDiff: $OUTPUT_DIFF"
} > "$SUMMARY_FILE"
