# -*- coding: utf-8 -*-
"""Remove lua2wasm references from Makefile."""
import io

path = r"E:\Soft\Proje\LXCLUA-NCore\lua\Makefile"
with io.open(path, "r", encoding="utf-8") as f:
    text = f.read()

failures = []

def rep(text, old, new, label, optional=False):
    if old in text:
        text = text.replace(old, new, 1)
        print("OK  :", label)
    elif optional:
        print("OK  :", label, "(already absent)")
    else:
        print("FAIL:", label)
        failures.append(label)
    return text

# 1. MYCFLAGS
text = rep(text,
    " -Isrc/lua2wasm ",
    " ", "MYCFLAGS remove -Isrc/lua2wasm", optional=True)
# 2. VPATH
text = rep(text,
    "src/wasm:src/bin:src/lua2wasm:pcre2/src",
    "src/wasm:src/bin:pcre2/src", "VPATH remove src/lua2wasm", optional=True)
# 3. CMCFLAGS
text = rep(text,
    "src/utils -Isrc/wasm -Isrc/bin -Isrc/lua2wasm -Ipcre2 $(WASMTIME_INC)",
    "src/utils -Isrc/wasm -Isrc/bin -Ipcre2 $(WASMTIME_INC)", "CMCFLAGS remove -Isrc/lua2wasm", optional=True)

# 4. remove LUA2WASM_* / WAT2WASM_* variable definitions block
defs_old = """# lua2wasm: Lua-to-WASM 编译器模块（编译进 liblxclua.a）
# 核心编译管线：词法分析→语法分析→代码生成→WAT输出
LUA2WASM_CORE_O= $(addprefix $(BUILDDIR)/,ast.o lexer_l2w.o parser_l2w.o wat_builder.o codegen_l2w.o builtins_l2w.o xalloc_l2w.o)
# WAT→WASM 汇编器
WAT2WASM_CORE_O= $(BUILDDIR)/wat2wasm_core.o
# Lua 模块入口：luaopen_lua2wasm
LUA2WASM_LIB_O= $(BUILDDIR)/lua2wasmlib.o
# CLI 主程序（可选独立编译）
LUA2WASM_CLI_O= $(BUILDDIR)/lua2wasm_main.o
WAT2WASM_CLI_O= $(BUILDDIR)/wat2wasm_cli.o
"""
text = rep(text, defs_old, "", "remove LUA2WASM_* variable definitions")

# 5. BASE_O
text = rep(text,
    "$(MYOBJS) $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O) $(LUA2WASM_LIB_O) $(PCRE2_O)",
    "$(MYOBJS) $(PCRE2_O)", "BASE_O remove lua2wasm objects")

# 6. remove lua2wasm CLI targets + compile rules block (lines ~192-239)
cli_old = """# --- lua2wasm: Lua-to-WASM 编译器 ---
# 核心模块已编译进 $(LUA_A)，可在 Lua 中通过 require("lua2wasm") 使用
# 以下为可选独立 CLI 工具

# lua2wasm CLI：将 .lua 编译为 .wat / .wasm（独立命令行工具）
lua2wasm: $(LUA2WASM_CLI_O) $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O)
	$(CC) -o $@ $(LDFLAGS) $(LUA2WASM_CLI_O) $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O) $(LIBS)

# wat2wasm CLI：WAT 文本转 WASM 二进制（独立命令行工具）
wat2wasm: $(WAT2WASM_CLI_O) $(WAT2WASM_CORE_O)
	$(CC) -o $@ $(LDFLAGS) $(WAT2WASM_CLI_O) $(WAT2WASM_CORE_O) $(LIBS)

# --- lua2wasm 编译规则（显式路径，避免与 lxclua 同名文件冲突） ---

$(BUILDDIR)/ast.o: src/lua2wasm/ast.c src/lua2wasm/ast.h src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/lexer_l2w.o: src/lua2wasm/lexer.c src/lua2wasm/lexer.h src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/parser_l2w.o: src/lua2wasm/parser.c src/lua2wasm/parser.h src/lua2wasm/builtins.h src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/wat_builder.o: src/lua2wasm/wat_builder.c src/lua2wasm/wat_builder.h src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/codegen_l2w.o: src/lua2wasm/codegen.c src/lua2wasm/codegen.h src/lua2wasm/parser.h src/lua2wasm/wat_builder.h src/lua2wasm/builtins.h src/lua2wasm/xalloc.h src/lua2wasm/prelude_wat.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/builtins_l2w.o: src/lua2wasm/builtins.c src/lua2wasm/builtins.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/xalloc_l2w.o: src/lua2wasm/xalloc.c src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/wat2wasm_core.o: src/lua2wasm/wat2wasm.c src/lua2wasm/wat2wasm.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

# lua2wasm Lua 模块入口（编译进 liblxclua.a）
$(BUILDDIR)/lua2wasmlib.o: src/lua2wasm/lua2wasmlib.c src/lua2wasm/lexer.h src/lua2wasm/parser.h src/lua2wasm/codegen.h src/lua2wasm/wat2wasm.h src/lua2wasm/wat_builder.h src/lua2wasm/xalloc.h src/core/lua.h src/core/lauxlib.h src/core/lualib.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

# 独立 CLI 编译规则
$(BUILDDIR)/lua2wasm_main.o: src/lua2wasm/main.c src/lua2wasm/codegen.h src/lua2wasm/lexer.h src/lua2wasm/parser.h src/lua2wasm/wat2wasm.h src/lua2wasm/wat_builder.h src/lua2wasm/xalloc.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/wat2wasm_cli.o: src/lua2wasm/wat2wasm_cli.c src/lua2wasm/wat2wasm.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

"""
text = rep(text, cli_old, "", "remove lua2wasm CLI targets & compile rules")

# 7. clean target
text = rep(text,
    "\t$(RM) lua2wasm.exe wat2wasm.exe liblua2wasm.a\n\t$(RM) lua2wasm_wasm.js lua2wasm_wasm.wasm\n",
    "", "clean target remove lua2wasm artifacts")

# 8. wasmlsp CMCFLAGS
text = rep(text,
    "src/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm\" \\",
    "src/utils -Isrc/wasm -Isrc/bin -Iquickjs\" \\", "wasmlsp CMCFLAGS remove -Isrc/lua2wasm")

if failures:
    print("FAILURES:", failures)
    print("NOT written")
else:
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("Written OK")
