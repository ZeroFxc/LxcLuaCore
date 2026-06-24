# Makefile for building Lua
# See ../doc/readme.html for installation and customization instructions.

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Your platform. See PLATS for possible values.
PLAT= guess

# Build directory for object files
BUILDDIR = build/obj

CC= gcc -std=gnu11 -pipe
CFLAGS= -O2 -funroll-loops -fomit-frame-pointer -ffunction-sections -fdata-sections -fstrict-aliasing -g0 -DNDEBUG -fno-exceptions -Wimplicit-function-declaration -D_GNU_SOURCE

AR= ar rcu
RANLIB= ranlib
RM= rm -f
UNAME= uname

# wasmtime: 支持 WASM GC 提案的运行时（v45.0.1 预编译库，用于桌面对 Windows MinGW）
WASMTIME_DIR = wasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api
WASMTIME_INC = -I$(WASMTIME_DIR)/include
WASMTIME_LIB = $(WASMTIME_DIR)/lib/libwasmtime.a -lbcrypt -luserenv -lole32 -lntdll
WASMTIME_DLL = $(WASMTIME_DIR)/lib/wasmtime.dll
# wasmtime Android 预编译库（aarch64）
WASMTIME_ANDROID_DIR = wasmtime/wasmtime-v45.0.1-aarch64-android-c-api

SYSCFLAGS= -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE
override CFLAGS+= $(SYSCFLAGS) $(MYCFLAGS)
SYSLDFLAGS=
SYSLIBS=

MYCFLAGS= -Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm -Ipcre2 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H $(WASMTIME_INC)
MYLDFLAGS=
MYLIBS=
MYOBJS= 

# Combine flags for linker
LDFLAGS= $(SYSLDFLAGS) $(MYLDFLAGS)
LIBS= -lm $(SYSLIBS) $(MYLIBS) $(WASMTIME_LIB)

# 按目标的 WASM 导出名称（wasm 构建时通过命令行覆盖）
WASM_EXPORT_NAME_LUA =
WASM_EXPORT_NAME_LUAC =
WASM_EXPORT_NAME_LUACCHECK =

# Special flags for compiler modules; -Os reduces code size.
VPATH = src/core:src/stdlib:src/vm:src/compiler:src/utils:src/wasm:src/bin:src/lua2wasm:pcre2/src
CMCFLAGS= -Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Isrc/lua2wasm -Ipcre2 $(WASMTIME_INC)


# == END OF USER SETTINGS -- NO NEED TO CHANGE ANYTHING BELOW THIS LINE =======

PLATS= guess aix bsd c89 freebsd generic ios linux macosx mingw posix solaris

LUA_A=	liblxclua.a
CORE_O= $(addprefix $(BUILDDIR)/,sljitLir.o ljit.o ljit_ir.o ljit_ir_list.o ljit_ir_label.o ljit_ir_bb.o ljit_sljit.o ljit_codegen.o ljit_cg_arith.o ljit_cg_ctrl.o ljit_cg_table.o ljit_cg_call.o ljit_cg_conv.o ljit_cg_closure.o ljit_cg_oop.o ljit_regalloc.o ljit_reg_live.o ljit_reg_graph.o ljit_reg_color.o ljit_reg_spill.o ljit_reg_alloc.o ljit_opt.o ljit_opt_const.o ljit_opt_dce.o ljit_opt_peep.o ljit_opt_cse.o ljit_opt_inline.o ljit_translate.o ljit_analyze.o lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmap.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o ltm.o lundump.o lvm.o lzio.o lobfuscate.o lthread.o lstruct.o lnamespace.o lbigint.o lsuper.o)
CORE_O_NOJIT= $(addprefix $(BUILDDIR)/,lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o lmap.o lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o ltm.o lundump.o lvm.o lzio.o lobfuscate.o lthread.o lstruct.o lnamespace.o lbigint.o lsuper.o lvmustom.o)
WASM3_O= $(addprefix $(BUILDDIR)/,m3_api_libc.o m3_api_meta_wasi.o m3_api_tracer.o m3_api_uvwasi.o m3_api_wasi.o m3_bind.o m3_code.o m3_compile.o m3_core.o m3_env.o m3_exec.o m3_function.o m3_info.o m3_module.o m3_parse.o)
# lua2wasm: Lua-to-WASM 编译器模块（编译进 liblxclua.a）
# 核心编译管线：词法分析→语法分析→代码生成→WAT输出
LUA2WASM_CORE_O= $(addprefix $(BUILDDIR)/,ast.o lexer_l2w.o parser_l2w.o wat_builder.o codegen_l2w.o builtins_l2w.o xalloc_l2w.o)
# WAT→WASM 汇编器
WAT2WASM_CORE_O= $(BUILDDIR)/wat2wasm_core.o
# Lua 模块入口：luaopen_lua2wasm
LUA2WASM_LIB_O= $(BUILDDIR)/lua2wasmlib.o
# CLI 主程序（可选独立编译）
LUA2WASM_CLI_O= $(BUILDDIR)/lua2wasm_main.o
WAT2WASM_CLI_O= $(BUILDDIR)/wat2wasm_cli.o
LIB_O=	$(addprefix $(BUILDDIR)/,lauxlib.o lpatchlib.o lbaselib.o lcorolib.o ldblib.o liolib.o lmathlib.o loadlib.o loslib.o lstrlib.o ltablib.o lutf8lib.o lmaplib.o linit.o json_parser.o lboolib.o lbitlib.o lptrlib.o ludatalib.o lvmlib.o lvmustom.o lnativevm.o lnativeparser.o lclass.o ltranslator.o llexerlib.o llexer_compiler.o logtable.o sha256.o aes.o crc.o csprng.o lthreadlib.o libhttp.o lfs.o lproclib.o lvmpro.o lbctc.o lbytecode.o lquickjs.o leventloop.o lpromise.o laio.o lcrypto.o luuid.o lrsa.o lecc.o)
# PCRE2 正则引擎库
PCRE2_CFLAGS = -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H
PCRE2_O= $(addprefix $(BUILDDIR)/,pcre2_auto_possess.o pcre2_chartables.o pcre2_chkdint.o pcre2_compile.o pcre2_compile_cgroup.o pcre2_compile_class.o pcre2_config.o pcre2_context.o pcre2_convert.o pcre2_dfa_match.o pcre2_error.o pcre2_extuni.o pcre2_find_bracket.o pcre2_jit_compile.o pcre2_maketables.o pcre2_match.o pcre2_match_data.o pcre2_match_next.o pcre2_newline.o pcre2_ord2utf.o pcre2_pattern_info.o pcre2_script_run.o pcre2_serialize.o pcre2_string_utils.o pcre2_study.o pcre2_substitute.o pcre2_substring.o pcre2_tables.o pcre2_ucd.o pcre2_valid_utf.o pcre2_xclass.o)
# PCRE2 不含 JIT 编译（用于 wasm 等不支持 JIT 的平台）
PCRE2_O_NOJIT= $(addprefix $(BUILDDIR)/,pcre2_auto_possess.o pcre2_chartables.o pcre2_chkdint.o pcre2_compile.o pcre2_compile_cgroup.o pcre2_compile_class.o pcre2_config.o pcre2_context.o pcre2_convert.o pcre2_dfa_match.o pcre2_error.o pcre2_extuni.o pcre2_find_bracket.o pcre2_jit_stubs.o pcre2_maketables.o pcre2_match.o pcre2_match_data.o pcre2_match_next.o pcre2_newline.o pcre2_ord2utf.o pcre2_pattern_info.o pcre2_script_run.o pcre2_serialize.o pcre2_string_utils.o pcre2_study.o pcre2_substitute.o pcre2_substring.o pcre2_tables.o pcre2_ucd.o pcre2_valid_utf.o pcre2_xclass.o)
QJS_O= quickjs/quickjs.o quickjs/libregexp.o quickjs/libunicode.o quickjs/cutils.o quickjs/quickjs-libc.o quickjs/dtoa.o
LIB_O_WASM= $(BUILDDIR)/lwasm3.o $(BUILDDIR)/lwasmtime.o $(WASM3_O)
BASE_O= $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(QJS_O) $(MYOBJS) $(LUA2WASM_CORE_O) $(WAT2WASM_CORE_O) $(LUA2WASM_LIB_O) $(PCRE2_O)
BASE_O_WASM= $(CORE_O) $(LIB_O) $(LIB_O_WASM) $(MYOBJS) $(PCRE2_O)

LUA_T=	lxclua
LUA_O=	$(BUILDDIR)/lua.o

LUAC_T=	luac
LUAC_O=	$(BUILDDIR)/luac.o

LUACCHECK_T=	luaccheck
LUACCHECK_O=	$(BUILDDIR)/luaccheck.o

# LSP (Language Server Protocol)
LSP_SRV_T=	lxclua-lsp
LSP_SRV_O=	$(addprefix $(BUILDDIR)/,lspsrv_main.o lspsrv_json.o lspsrv_proto.o lspsrv_doc.o lspsrv_lexer.o lspsrv_kwdb.o lspsrv_complete.o lspsrv_hover.o lspsrv_features.o lspsrv_util.o)

ALL_O= $(BASE_O) $(LUA_O) $(LUAC_O) $(LUACCHECK_O)
QJS_T= qjs
QJSC_T= qjsc
QJSC_O= quickjs/qjsc.o
QJS_EXE_O= quickjs/qjs.o

ALL_T= $(LUA_A) $(LUA_T) $(LUAC_T) $(LUACCHECK_T)

ALL_A= $(LUA_A)

# Targets start here.
default: $(PLAT)

all:	$(ALL_T)

o:	$(ALL_O)

a:	$(ALL_A)

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Generic compile rule for .o files in build directory
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(MYCFLAGS) -c $< -o $@

$(LUA_A): $(BASE_O)
	$(AR) $@ $(BASE_O) $(if $(findstring .dll,$(LUA_A)),$(LDFLAGS) $(LIBS))
	$(RANLIB) $@

$(LUA_T): $(LUA_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(WASM_EXPORT_NAME_LUA) $(LUA_O) $(LUA_A) $(LIBS)

$(LUAC_T): $(LUAC_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(WASM_EXPORT_NAME_LUAC) $(LUAC_O) $(LUA_A) $(LIBS)
$(QJS_T): $(QJS_EXE_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(QJS_EXE_O) $(LUA_A) $(LIBS)

$(QJSC_T): $(QJSC_O) $(LUA_A)
	$(CC) -o $@ $(LDFLAGS) $(QJSC_O) $(LUA_A) $(LIBS)

$(LUACCHECK_T): $(LUACCHECK_O) $(LUA_A)
	$(CC) -mconsole -o $@ $(LDFLAGS) $(WASM_EXPORT_NAME_LUACCHECK) $(LUACCHECK_O) $(LUA_A) $(LIBS)

# ---- LSP Server (lxclua-lsp) ----
# LSP 服务器不需要 wasmtime 运行时，仅链接基础数学库
# 禁用 FORTIFY_SOURCE 避免 GCC 15 的 _chk 符号链接失败
LSP_CFLAGS = $(CFLAGS) -U_FORTIFY_SOURCE
LSP_LIBS = -lm
$(LSP_SRV_T): $(LSP_SRV_O)
	$(CC) -o $@ $(LDFLAGS) $(LSP_SRV_O) $(LSP_LIBS)

# LSP object compilation rules (src/lspsrv/*.c)
$(BUILDDIR)/lspsrv_main.o: src/lspsrv/lspsrv_main.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_json.o: src/lspsrv/lspsrv_json.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_proto.o: src/lspsrv/lspsrv_proto.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_doc.o: src/lspsrv/lspsrv_doc.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_lexer.o: src/lspsrv/lspsrv_lexer.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_kwdb.o: src/lspsrv/lspsrv_kwdb.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_complete.o: src/lspsrv/lspsrv_complete.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_hover.o: src/lspsrv/lspsrv_hover.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_features.o: src/lspsrv/lspsrv_features.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

$(BUILDDIR)/lspsrv_util.o: src/lspsrv/lspsrv_util.c src/lspsrv/lspsrv.h | $(BUILDDIR)
	$(CC) $(LSP_CFLAGS) $(CMCFLAGS) -Isrc/lspsrv -c $< -o $@

# --- PCRE2 正则引擎库 ---
$(BUILDDIR)/pcre2_%.o: pcre2/src/pcre2_%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(PCRE2_CFLAGS) -Ipcre2 -Ipcre2/src -c $< -o $@

# --- lua2wasm: Lua-to-WASM 编译器 ---
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

$(WEBSERVER_A): $(WEBSERVER_O) $(LUA_A)
	$(CC) -shared -o $@ $(LDFLAGS) $(WEBSERVER_O) $(LUA_A) $(LIBS) -lws2_32

test:
	./$(LUA_T) -v
clean:
	$(RM) -r $(BUILDDIR)
	$(RM) $(ALL_T) $(ALL_A) $(ALL_O) $(QJSC_O) $(QJS_EXE_O) quickjs/repl.c
	$(RM) lxclua.exe luac.exe luaccheck.exe lxclua.dll qjs.exe qjsc.exe
	$(RM) lxclua-lsp.exe
	$(RM) lua2wasm.exe wat2wasm.exe liblua2wasm.a
	$(RM) lua2wasm_wasm.js lua2wasm_wasm.wasm
	$(RM) *.o *.a *.dll *.js *.wasm lxclua_standalone.html
	$(RM) *.lua *.luac *.out *.outa *.log


depend:
	@$(CC) $(CFLAGS) -MM l*.c

echo:
	@echo "PLAT= $(PLAT)"
	@echo "CC= $(CC)"
	@echo "CFLAGS= $(CFLAGS)"
	@echo "LDFLAGS= $(LDFLAGS)"
	@echo "LIBS= $(LIBS)"
	@echo "AR= $(AR)"
	@echo "RANLIB= $(RANLIB)"
	@echo "RM= $(RM)"
	@echo "UNAME= $(UNAME)"

# Convenience targets for popular platforms.
ALL= all

help:
	@echo "Do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"
	@echo "See doc/readme.html for complete instructions."

guess:
	@echo Guessing `$(UNAME)`
	@$(MAKE) `$(UNAME)`

AIX aix:
	$(MAKE) $(ALL) CC="xlc" CFLAGS="-O2 -DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-ldl" SYSLDFLAGS="-brtl -bexpall"

bsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN" SYSLIBS="-Wl,-E"

c89:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_C89" CC="gcc -std=c89"
	@echo ''
	@echo '*** C89 does not guarantee 64-bit integers for Lua.'
	@echo '*** Make sure to compile all external Lua libraries'
	@echo '*** with LUA_USE_C89 to ensure consistency'
	@echo ''

FreeBSD NetBSD OpenBSD freebsd:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_READLINE -I/usr/include/edit" SYSLIBS="-Wl,-E -ledit" CC="cc"

generic: $(ALL)

ios:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_IOS"

Linux linux:
	$(MAKE) $(ALL) CC="gcc -std=gnu11" CFLAGS="-O2 -fPIC -DNDEBUG -D_DEFAULT_SOURCE" SYSCFLAGS="-DLUA_USE_LINUX" SYSLIBS="-Wl,-E -ldl -lm -lpthread -lssl -lcrypto" SYSLDFLAGS="-s" \
	"WASMTIME_DIR=wasmtime/wasmtime-v45.0.1-x86_64-linux-c-api" \
	"WASMTIME_LIB=wasmtime/wasmtime-v45.0.1-x86_64-linux-c-api/lib/libwasmtime.a" \
	"WASMTIME_DLL=wasmtime/wasmtime-v45.0.1-x86_64-linux-c-api/lib/libwasmtime.so"
	strip --strip-unneeded $(LUA_T) $(LUAC_T) || true

termux:
	$(MAKE) $(ALL) CC="clang -std=c23" CFLAGS="-O2 -fPIC -DNDEBUG" SYSCFLAGS="-DLUA_USE_LINUX -DLUA_USE_DLOPEN" SYSLIBS="-ldl -lm -lssl -lcrypto" SYSLDFLAGS="-Wl,--build-id -fuse-ld=lld" \
	"WASMTIME_DIR=wasmtime/wasmtime-v45.0.1-aarch64-android-c-api" \
	"WASMTIME_LIB=wasmtime/wasmtime-v45.0.1-aarch64-android-c-api/lib/libwasmtime.a" \
	"WASMTIME_DLL=wasmtime/wasmtime-v45.0.1-aarch64-android-c-api/lib/libwasmtime.so"
	strip --strip-unneeded $(LUA_T) $(LUAC_T) || true

Darwin macos macosx:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_MACOSX -DLUA_USE_READLINE" SYSLIBS="-lreadline"

mingw:
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblxclua.a" "LUA_T=lxclua.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE -DGUI_PLATFORM_WINDOWS -D_UNICODE -DUNICODE" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32" "SYSLDFLAGS=-s -Wl,--stack,16777216" \
	"MYOBJS=$(MYOBJS)" lxclua.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblxclua.a" "LUAC_T=luac.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lsecur32 -lcrypt32" "SYSLDFLAGS=-s" \
	luac.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUACCHECK_T=luaccheck.exe" "SYSLDFLAGS=-s -mconsole" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lsecur32 -lcrypt32" luaccheck.exe
	$(CC) -shared -o lxclua.dll -Wl,--export-all-symbols -Wl,--allow-multiple-definition -Wl,--whole-archive liblxclua.a -Wl,--no-whole-archive $(WASMTIME_LIB) -lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32 -lm
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LSP_SRV_T=lxclua-lsp.exe" "SYSLDFLAGS=-s" "SYSLIBS=" lxclua-lsp.exe

lsp:
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LSP_SRV_T=lxclua-lsp.exe" "SYSLDFLAGS=-s" "SYSLIBS=" lxclua-lsp.exe

lsp-linux:
	$(MAKE) "LSP_SRV_T=lxclua-lsp" "SYSLDFLAGS=-s" "SYSLIBS=" \
	"CC=gcc -std=gnu11" "CFLAGS=-O2 -fPIC -DNDEBUG -D_DEFAULT_SOURCE" lxclua-lsp
	strip --strip-unneeded lxclua-lsp || true

mingw-static:
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblxclua.a" "LUA_T=lxclua.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE -DGUI_PLATFORM_WINDOWS -D_UNICODE -DUNICODE" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lcomctl32 -lshell32 -lcomdlg32 -lole32 -luuid -lgdi32 -lsecur32 -lcrypt32" "SYSLDFLAGS=-s" \
	"MYOBJS=$(MYOBJS)" lxclua.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUA_A=liblxclua.a" "LUAC_T=luac.exe" \
	"AR=$(AR)" "RANLIB=$(RANLIB)" \
	"SYSCFLAGS=-DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lsecur32 -lcrypt32" "SYSLDFLAGS=-s" \
	luac.exe
	TMPDIR=. TMP=. TEMP=. $(MAKE) "LUACCHECK_T=luaccheck.exe" "SYSLDFLAGS=-s -mconsole" "SYSLIBS=-lwininet -lws2_32 -lpsapi -lpthread -lsecur32 -lcrypt32" luaccheck.exe


posix:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX"

SunOS solaris:
	$(MAKE) $(ALL) SYSCFLAGS="-DLUA_USE_POSIX -DLUA_USE_DLOPEN -D_REENTRANT" SYSLIBS="-ldl"

# WebAssembly (Emscripten)
# 需要先安装 Emscripten SDK: https://emscripten.org/docs/getting_started/downloads.html
# 使用方法: make wasm
# Emscripten 3.0.0+ 支持 C23 (底层 Clang 18+)
# Emscripten SDK 路径配置（Windows需要.bat扩展名）
EMSDK_PATH= E:/Soft/Proje/LXCLUA-NCore/emsdk/upstream/emscripten
EMCC= PYTHONUTF8=1 $(EMSDK_PATH)/emcc.bat
EMAR= $(EMSDK_PATH)/emar.bat
EMRANLIB= $(EMSDK_PATH)/emranlib.bat
wasm:
	$(MAKE) clean
	PYTHONUTF8=1 $(MAKE) $(ALL) CC="$(EMCC) -std=c23" \
	"CFLAGS=-O3 -DNDEBUG -fno-exceptions -DLUA_32BITS=0" \
	"SYSCFLAGS=-DLUA_USE_LONGJMP -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_NOJIT" \
	"PCRE2_O=$(PCRE2_O_NOJIT)" \
	"SYSLIBS=" \
	"WASMTIME_INC=" \
	"WASMTIME_LIB=" \
	"AR=$(EMAR) rcu" \
	"RANLIB=$(EMRANLIB)" \
	"LUA_T=lxclua.js" \
	"LUAC_T=luac.js" \
	"LUACCHECK_T=luaccheck.js" \
	"WASM_EXPORT_NAME_LUA=-sEXPORT_NAME=LuaModule" \
	"WASM_EXPORT_NAME_LUAC=-sEXPORT_NAME=LuacModule" \
	"WASM_EXPORT_NAME_LUACCHECK=-sEXPORT_NAME=LuaccheckModule" \
	"CORE_O=$(CORE_O_NOJIT)" \
	"LIB_O_WASM=$(BUILDDIR)/lwasm3.o $(WASM3_O)" \
	"LIB_O=$(BUILDDIR)/lauxlib.o $(BUILDDIR)/lpatchlib.o $(BUILDDIR)/lbaselib.o $(BUILDDIR)/lcorolib.o $(BUILDDIR)/ldblib.o $(BUILDDIR)/liolib.o $(BUILDDIR)/lmathlib.o $(BUILDDIR)/loadlib.o $(BUILDDIR)/loslib.o $(BUILDDIR)/lstrlib.o $(BUILDDIR)/ltablib.o $(BUILDDIR)/lutf8lib.o $(BUILDDIR)/lmaplib.o $(BUILDDIR)/linit.o $(BUILDDIR)/json_parser.o $(BUILDDIR)/lboolib.o $(BUILDDIR)/lbitlib.o $(BUILDDIR)/lptrlib.o $(BUILDDIR)/ludatalib.o $(BUILDDIR)/lvmlib.o $(BUILDDIR)/lnativevm.o $(BUILDDIR)/lnativeparser.o $(BUILDDIR)/lclass.o $(BUILDDIR)/ltranslator.o $(BUILDDIR)/llexerlib.o $(BUILDDIR)/llexer_compiler.o  $(BUILDDIR)/logtable.o $(BUILDDIR)/sha256.o $(BUILDDIR)/aes.o $(BUILDDIR)/crc.o $(BUILDDIR)/csprng.o $(BUILDDIR)/lthreadlib.o $(BUILDDIR)/libhttp.o $(BUILDDIR)/lfs.o $(BUILDDIR)/lproclib.o $(BUILDDIR)/lvmpro.o $(BUILDDIR)/lbctc.o $(BUILDDIR)/lbytecode.o $(BUILDDIR)/lquickjs.o $(BUILDDIR)/leventloop.o $(BUILDDIR)/lpromise.o $(BUILDDIR)/laio.o $(BUILDDIR)/lcrypto.o $(BUILDDIR)/luuid.o $(BUILDDIR)/lrsa.o $(BUILDDIR)/lecc.o $(BUILDDIR)/ljit_stubs.o" \
	"GUI_OBJS=" \
	"LDFLAGS=-sWASM=1 -sSINGLE_FILE=1 -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS -sMODULARIZE=1 -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1 -sINVOKE_RUN=0 -sSTACK_SIZE=5MB -sINITIAL_MEMORY=32MB"

wasmlsp:
	PYTHONUTF8=1 $(MAKE) $(LSP_SRV_O) CC="$(EMCC) -std=c23" \
	"CFLAGS=-O3 -DNDEBUG -fno-exceptions" \
	"CMCFLAGS=-Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm" \
	"SYSCFLAGS=" \
	"SYSLIBS="
	$(EMCC) -std=c23 -o lxclua-lsp.js $(LSP_SRV_O) -lm \
		-sWASM=1 -sSINGLE_FILE=1 \
		-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,callMain,FS \
		-sMODULARIZE=1 -sEXPORT_NAME=LuaLSPModule \
		-sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=1 \
		-sINVOKE_RUN=0 -sSTACK_SIZE=5MB -sINITIAL_MEMORY=32MB

# 将 C 文件编译为 WASM 模块（供 wasm3 使用）
# 用法: 
#   make wasm-c SRC=xxx.c [OUT=xxx.wasm] [EXPORTS="_func1,_func2"]
#   make wasm-c-all SRC=xxx.c [OUT=xxx.wasm]   (导出所有函数)
# 示例: 
#   make wasm-c SRC=test.c
#   make wasm-c SRC=test.c OUT=mylib.wasm EXPORTS="_add,_mul"
#   make wasm-c-all SRC=test.c
WASM_CFLAGS= -O3 -DNDEBUG
WASM_LDFLAGS= -sWASM=1 -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=1 --no-entry
WASM_EXPORTS= 


# Lua WASM 导出的 API 函数列表
LUA_WASM_EXPORTS=\
	["_lua_wasm_newstate",\
	 "_lua_wasm_close",\
	 "_lua_wasm_openlibs",\
	 "_lua_wasm_dostring",\
	 "_lua_wasm_loadstring",\
	 "_lua_wasm_dofile",\
	 "_lua_wasm_loadfile",\
	 "_lua_wasm_gettop",\
	 "_lua_wasm_settop",\
	 "_lua_wasm_pop",\
	 "_lua_wasm_pushvalue",\
	 "_lua_wasm_remove",\
	 "_lua_wasm_insert",\
	 "_lua_wasm_replace",\
	 "_lua_wasm_checkstack",\
	 "_lua_wasm_type",\
	 "_lua_wasm_typename",\
	 "_lua_wasm_isnil",\
	 "_lua_wasm_isboolean",\
	 "_lua_wasm_isnumber",\
	 "_lua_wasm_isstring",\
	 "_lua_wasm_istable",\
	 "_lua_wasm_isfunction",\
	 "_lua_wasm_isuserdata",\
	 "_lua_wasm_isthread",\
	 "_lua_wasm_islightuserdata",\
	 "_lua_wasm_tonumber",\
	 "_lua_wasm_tointeger",\
	 "_lua_wasm_toboolean",\
	 "_lua_wasm_tostring",\
	 "_lua_wasm_tolstring",\
	 "_lua_wasm_rawlen",\
	 "_lua_wasm_touserdata",\
	 "_lua_wasm_tothread",\
	 "_lua_wasm_topointer",\
	 "_lua_wasm_pushnil",\
	 "_lua_wasm_pushnumber",\
	 "_lua_wasm_pushinteger",\
	 "_lua_wasm_pushboolean",\
	 "_lua_wasm_pushstring",\
	 "_lua_wasm_pushlstring",\
	 "_lua_wasm_pushlightuserdata",\
	 "_lua_wasm_createtable",\
	 "_lua_wasm_newtable",\
	 "_lua_wasm_getglobal",\
	 "_lua_wasm_setglobal",\
	 "_lua_wasm_getfield",\
	 "_lua_wasm_setfield",\
	 "_lua_wasm_gettable",\
	 "_lua_wasm_settable",\
	 "_lua_wasm_rawget",\
	 "_lua_wasm_rawgeti",\
	 "_lua_wasm_rawset",\
	 "_lua_wasm_rawseti",\
	 "_lua_wasm_setmetatable",\
	 "_lua_wasm_getmetatable",\
	 "_lua_wasm_next",\
	 "_lua_wasm_len",\
	 "_lua_wasm_pcall",\
	 "_lua_wasm_call",\
	 "_lua_wasm_error",\
	 "_lua_wasm_errorstring",\
	 "_lua_wasm_gc",\
	 "_lua_wasm_collectgarbage",\
	 "_lua_wasm_memusage",\
	 "_lua_wasm_ref",\
	 "_lua_wasm_unref",\
	 "_lua_wasm_getregistry",\
	 "_lua_wasm_version",\
	 "_lua_wasm_compare",\
	 "_lua_wasm_equal",\
	 "_lua_wasm_lessthan",\
	 "_lua_wasm_rawequal",\
	 "_lua_wasm_eval",\
	 "_lua_wasm_eval_number",\
	 "_lua_wasm_eval_integer",\
	 "_lua_wasm_call_global_number",\
	 "_lua_wasm_call_global_string",\
	 "_lua_wasm_setglobal_number",\
	 "_lua_wasm_setglobal_integer",\
	 "_lua_wasm_setglobal_string",\
	 "_lua_wasm_getglobal_number",\
	 "_lua_wasm_getglobal_integer",\
	 "_lua_wasm_getglobal_string",\
	 "_lua_wasm_malloc",\
	 "_lua_wasm_free",\
	 "_lua_wasm_realloc",\
	 "_malloc",\
	 "_free"]

# Targets that do not create files (not all makes understand .PHONY).
.PHONY: all $(PLATS) help test clean default o a depend echo wasm wasmlsp release mingw-release linux-release macos-release wasm-release termux-release lsp lsp-linux

# 发行版打包配置
RELEASE_NAME= lxclua
RELEASE_VERSION= $(shell date +%Y%m%d_%H%M%S)
RELEASE_DIR= release
SIGNER= DifierLine

mingw-release: mingw
	@echo "Creating Windows release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Windows x64 (MinGW)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp luac.exe luaccheck.exe lxclua-lsp.exe lxclua.dll lxclua.exe $(RELEASE_DIR)/ 2>/dev/null || true
	@cp LICENSE $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-windows-x64-$(RELEASE_VERSION).zip -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-windows-x64-$(RELEASE_VERSION).zip"



# Linux 发行版
linux-release: linux
	@echo "Creating Linux release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Linux x64" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luaccheck luac liblxclua.a $(RELEASE_DIR)/ 2>/dev/null || true
	@cp LICENSE $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-linux-x64-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-linux-x64-$(RELEASE_VERSION).tar.gz"


# macOS 发行版
macos-release: macosx
	@echo "Creating macOS release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: macOS (Darwin)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luac $(RELEASE_DIR)/
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-macos-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-macos-$(RELEASE_VERSION).tar.gz"

# Termux/Android 发行版

termux-release: termux
	@echo "Creating Termux release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: Android (Termux)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua luac luaccheck liblxclua.a $(RELEASE_DIR)/ 2>/dev/null || true
	@cp LICENSE $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-termux-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-termux-$(RELEASE_VERSION).tar.gz"

# WebAssembly 发行版
wasm-release: wasm
	@echo "Creating WASM release..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Platform: WebAssembly" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp lxclua.js luac.js luaccheck.js $(RELEASE_DIR)/
	@cp LICENSE $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-wasm-$(RELEASE_VERSION).zip -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-wasm-$(RELEASE_VERSION).zip"

# 通用发行版打包
release:
	@echo "Creating release package..."
	@mkdir -p $(RELEASE_DIR)
	@echo "LXCLua Release" > $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Build Time: $$(date '+%Y-%m-%d %H:%M:%S')" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@echo "Signed by: $(SIGNER)" >> $(RELEASE_DIR)/BUILD_INFO.txt
	@cp $(LUA_T) $(LUAC_T) $(RELEASE_DIR)/ 2>/dev/null || true
	@cp $(LUA_A) $(RELEASE_DIR)/ 2>/dev/null || true
	@cp LICENSE README.md README_EN.md $(RELEASE_DIR)/ 2>/dev/null || true
	@tar -caf $(RELEASE_NAME)-$(RELEASE_VERSION).tar.gz -C $(RELEASE_DIR) .
	@rm -rf $(RELEASE_DIR)
	@echo "Created: $(RELEASE_NAME)-$(RELEASE_VERSION).tar.gz"

# Compiler modules may use special flags.

# QuickJS targets
quickjs/%.o: quickjs/%.c
	$(CC) $(CFLAGS) $(CMCFLAGS) -Iquickjs -D_GNU_SOURCE -DCONFIG_VERSION=\"2024-01-13\" -c $< -o $@

$(BUILDDIR)/lquickjs.o: src/bin/lquickjs.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -Iquickjs -c $< -o $@
quickjs/qjsc.o: quickjs/qjsc.c
	$(CC) $(CFLAGS) $(CMCFLAGS) -Iquickjs -D_GNU_SOURCE -DCONFIG_PREFIX=\"/usr/local\" -DCONFIG_VERSION=\"2024-01-13\" -c $< -o $@

quickjs/qjs.o: quickjs/qjs.c
	$(CC) $(CFLAGS) $(CMCFLAGS) -Iquickjs -D_GNU_SOURCE -DCONFIG_VERSION=\"2024-01-13\" -c $< -o $@

$(BUILDDIR)/llex.o: llex.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/lparser.o: lparser.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

$(BUILDDIR)/sljitLir.o: src/jit/sljitLir.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/jit/sljitLir.c -o $@

$(BUILDDIR)/ljit.o: src/vm/jit/core/ljit.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/core/ljit.c -o $@

$(BUILDDIR)/lcode.o: lcode.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -c $< -o $@

# DO NOT DELETE

lapi.o: lapi.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lstring.h \
 ltable.h lundump.h lvm.h
lauxlib.o: lauxlib.c lprefix.h lua.h luaconf.h lauxlib.h llimits.h
lbaselib.o: lbaselib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lcode.o: lcode.c lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lgc.h lstring.h ltable.h lvm.h lopnames.h
lcorolib.o: lcorolib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lctype.o: lctype.c lprefix.h lctype.h lua.h luaconf.h llimits.h
ldblib.o: ldblib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
ldebug.o: ldebug.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lcode.h llex.h lopcodes.h lparser.h \
 ldebug.h ldo.h lfunc.h lstring.h lgc.h ltable.h lvm.h
ldo.o: ldo.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lparser.h lstring.h ltable.h lundump.h lvm.h
ldump.o: ldump.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lgc.h ltable.h lundump.h
lfunc.o: lfunc.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h
lgc.o: lgc.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h lstring.h ltable.h
linit.o: linit.c lprefix.h lua.h luaconf.h lualib.h lauxlib.h llimits.h
lfs.o: lfs.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h
liolib.o: liolib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
llex.o: llex.c lprefix.h lua.h luaconf.h lctype.h llimits.h ldebug.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lgc.h llex.h lparser.h \
 lstring.h ltable.h
lmathlib.o: lmathlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lmem.o: lmem.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h
loadlib.o: loadlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lobject.o: lobject.c lprefix.h lua.h luaconf.h lctype.h llimits.h \
 ldebug.h lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h \
 lvm.h
lopcodes.o: lopcodes.c lprefix.h lopcodes.h llimits.h lua.h luaconf.h \
 lobject.h
loslib.o: loslib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
lparser.o: lparser.c lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lfunc.h lstring.h lgc.h ltable.h
lstate.o: lstate.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h llex.h \
 lstring.h ltable.h
lstring.o: lstring.c lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h
lstrlib.o: lstrlib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltable.o: ltable.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
ltablib.o: ltablib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltm.o: ltm.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
lua.o: lua.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
luac.o: luac.c lprefix.h lua.h luaconf.h lauxlib.h lapi.h llimits.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldebug.h lopcodes.h lopnames.h \
 lundump.h
luaccheck.o: luaccheck.c lprefix.h lua.h luaconf.h lauxlib.h lapi.h llimits.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldebug.h lopcodes.h lopnames.h \
 lundump.h lobfuscate.h
lundump.o: lundump.c lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lstring.h lgc.h \
 ltable.h lundump.h
lutf8lib.o: lutf8lib.c lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lvm.o: lvm.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lstring.h ltable.h lvm.h ljumptab.h
lzio.o: lzio.c lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h

# (end of Makefile)
$(BUILDDIR)/ljit_ir.o: src/vm/jit/ir/ljit_ir.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/ir/ljit_ir.c -o $@

$(BUILDDIR)/ljit_ir_list.o: src/vm/jit/ir/ljit_ir_list.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/ir/ljit_ir_list.c -o $@

$(BUILDDIR)/ljit_ir_label.o: src/vm/jit/ir/ljit_ir_label.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/ir/ljit_ir_label.c -o $@

$(BUILDDIR)/ljit_sljit.o: src/vm/jit/sljit/ljit_sljit.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/sljit/ljit_sljit.c -o $@

$(BUILDDIR)/ljit_ir_bb.o: src/vm/jit/ir/ljit_ir_bb.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/ir/ljit_ir_bb.c -o $@

$(BUILDDIR)/ljit_analyze.o: src/vm/jit/frontend/ljit_analyze.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/frontend/ljit_analyze.c -o $@
$(BUILDDIR)/ljit_translate.o: src/vm/jit/frontend/ljit_translate.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/frontend/ljit_translate.c -o $@
$(BUILDDIR)/ljit_opt.o: src/vm/jit/optimize/ljit_opt.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt.c -o $@
$(BUILDDIR)/ljit_opt_const.o: src/vm/jit/optimize/ljit_opt_const.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt_const.c -o $@
$(BUILDDIR)/ljit_opt_dce.o: src/vm/jit/optimize/ljit_opt_dce.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt_dce.c -o $@
$(BUILDDIR)/ljit_opt_peep.o: src/vm/jit/optimize/ljit_opt_peep.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt_peep.c -o $@
$(BUILDDIR)/ljit_opt_cse.o: src/vm/jit/optimize/ljit_opt_cse.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt_cse.c -o $@
$(BUILDDIR)/ljit_opt_inline.o: src/vm/jit/optimize/ljit_opt_inline.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/optimize/ljit_opt_inline.c -o $@
$(BUILDDIR)/ljit_regalloc.o: src/vm/jit/regalloc/ljit_regalloc.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_regalloc.c -o $@
$(BUILDDIR)/ljit_codegen.o: src/vm/jit/codegen/ljit_codegen.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_codegen.c -o $@
$(BUILDDIR)/ljit_cg_arith.o: src/vm/jit/codegen/ljit_cg_arith.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_arith.c -o $@
$(BUILDDIR)/ljit_cg_ctrl.o: src/vm/jit/codegen/ljit_cg_ctrl.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_ctrl.c -o $@
$(BUILDDIR)/ljit_cg_table.o: src/vm/jit/codegen/ljit_cg_table.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_table.c -o $@
$(BUILDDIR)/ljit_cg_call.o: src/vm/jit/codegen/ljit_cg_call.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_call.c -o $@
$(BUILDDIR)/ljit_cg_conv.o: src/vm/jit/codegen/ljit_cg_conv.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_conv.c -o $@
$(BUILDDIR)/ljit_reg_live.o: src/vm/jit/regalloc/ljit_reg_live.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_reg_live.c -o $@

$(BUILDDIR)/ljit_reg_graph.o: src/vm/jit/regalloc/ljit_reg_graph.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_reg_graph.c -o $@

$(BUILDDIR)/ljit_reg_color.o: src/vm/jit/regalloc/ljit_reg_color.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_reg_color.c -o $@

$(BUILDDIR)/ljit_reg_spill.o: src/vm/jit/regalloc/ljit_reg_spill.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_reg_spill.c -o $@

$(BUILDDIR)/ljit_reg_alloc.o: src/vm/jit/regalloc/ljit_reg_alloc.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/regalloc/ljit_reg_alloc.c -o $@

$(BUILDDIR)/ljit_cg_closure.o: src/vm/jit/codegen/ljit_cg_closure.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_closure.c -o $@

$(BUILDDIR)/ljit_cg_oop.o: src/vm/jit/codegen/ljit_cg_oop.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CMCFLAGS) -I. -c src/vm/jit/codegen/ljit_cg_oop.c -o $@

# Map容器类型（新增）
lmap.o: lmap.c lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h lmap.h lvm.h

lmaplib.o: lmaplib.c lprefix.h lua.h luaconf.h llimits.h lmem.h lobject.h \
 lstate.h lmap.h lstring.h ltm.h lapi.h lvm.h lualib.h lauxlib.h

# ============================================================
# 生成合并头文件 lxclua.h（单头文件，供 C 扩展模块开发使用）
# 用法: make head
# 需要 Python 3
# ============================================================
PYTHON = python

head:
	@echo "正在生成合并头文件 lxclua.h ..."
	@$(PYTHON) tools/merge_headers.py
	@echo "完成: lxclua.h"
