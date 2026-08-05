LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := lua
LOCAL_CFLAGS := -std=c23 -O3 \
                -funroll-loops -fomit-frame-pointer \
                -ffunction-sections -fdata-sections \
                -fstrict-aliasing
LOCAL_CFLAGS += -g0 -DNDEBUG

# 极致性能构建配置
LOCAL_CFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -Wimplicit-function-declaration
LOCAL_CFLAGS += -fasm



LOCAL_SRC_FILES := \
    src/utils/csprng.c\
    src/utils/aes.c\
	src/utils/laio.c\
    src/utils/crc.c\
    src/stdlib/lfs.c\
    src/stdlib/lastlib.c\
	src/core/lapi.c \
	src/vm/lbytecode.c \
	src/core/lauxlib.c \
	src/utils/lbigint.c\
	src/stdlib/lbaselib.c \
	src/stdlib/lboolib.c \
	src/stdlib/lclass.c \
	src/core/lcode.c \
	src/stdlib/lcorolib.c \
	src/utils/lctype.c \
	src/stdlib/ldblib.c \
	src/core/ldebug.c \
	src/core/ldo.c \
	src/core/ldump.c \
	src/utils/leventloop.c \
	src/core/lfunc.c \
	src/core/lgc.c \
	src/core/linit.c \
	src/stdlib/liolib.c \
	src/compiler/llex.c \
	src/stdlib/lmathlib.c \
	src/core/lmem.c \
	src/stdlib/loadlib.c \
	src/core/lobject.c \
	src/core/lopcodes.c \
	src/stdlib/loslib.c \
	src/compiler/lparser.c \
	src/compiler/lasm.c \
	src/compiler/last.c \
	src/compiler/last_parse.c \
	src/compiler/last_visitor.c \
	src/compiler/last_serialize.c \
	src/compiler/last_unparse.c \
	src/compiler/lcodegen.c \
	src/utils/lpromise.c \
	src/core/lstate.c \
	src/core/lstring.c \
	src/stdlib/lstrlib.c \
	src/core/ltable.c \
    src/core/lmap.c \
    src/utils/libhttp.c\
	src/stdlib/ltablib.c \
    src/stdlib/lmaplib.c \
    src/core/ltm.c \
	src/bin/lua.c \
	src/utils/ltranslator.c \
	src/core/lundump.c \
	src/stdlib/ludatalib.c \
	src/stdlib/lutf8lib.c \
	src/stdlib/lbitlib.c \
	src/vm/lvmlib.c \
	src/vm/lvmustom.c \
	src/vm/lnativevm.c \
	src/vm/lnativeparser.c \
	src/vm/lvm.c \
	src/core/lzio.c \
	src/utils/lnamespace.c\
	src/utils/lthread.c \
	src/stdlib/lthreadlib.c \
	src/stdlib/lproclib.c\
	src/stdlib/lptrlib.c \
	src/vm/lvmpro.c\
	src/utils/logtable.c \
	src/utils/json_parser.c \
	src/stdlib/lsuper.c\
	src/stdlib/lstruct.c \
	src/utils/sha256.c \
	src/compiler/lbctc.c\
	src/utils/lpatchlib.c\
	src/compiler/llexerlib.c\
	src/compiler/llexer_compiler.c\
	src/utils/lobfuscate.c \
	src/wasm/lwasm3.c \
	src/wasm/lwasmtime.c \
	src/bin/lquickjs.c \
	src/wasm/m3_api_libc.c \
	src/wasm/m3_api_meta_wasi.c \
	src/wasm/m3_api_tracer.c \
	src/wasm/m3_api_uvwasi.c \
	src/wasm/m3_api_wasi.c \
	src/wasm/m3_bind.c \
	src/wasm/m3_code.c \
	src/wasm/m3_compile.c \
	src/wasm/m3_core.c \
	src/wasm/m3_env.c \
	src/wasm/m3_exec.c \
	src/wasm/m3_function.c \
	src/wasm/m3_info.c \
	src/wasm/m3_module.c \
	src/wasm/m3_parse.c \
	quickjs/quickjs.c \
	quickjs/libregexp.c \
	quickjs/libunicode.c \
	quickjs/cutils.c \
	quickjs/quickjs-libc.c \
	quickjs/dtoa.c \
	src/lua2wasm/ast.c \
	src/lua2wasm/lexer.c \
	src/lua2wasm/parser.c \
	src/lua2wasm/wat_builder.c \
	src/lua2wasm/codegen.c \
	src/lua2wasm/builtins.c \
	src/lua2wasm/wat2wasm.c \
	src/lua2wasm/xalloc.c \
	src/lua2wasm/lua2wasmlib.c \
	src/utils/lcrypto.c \
	src/utils/luuid.c \
	src/utils/lrsa.c \
	src/utils/lecc.c \
    src/stdlib/lpcre2_stubs.c \
	pcre2/src/pcre2_auto_possess.c \
	pcre2/src/pcre2_chartables.c \
	pcre2/src/pcre2_chkdint.c \
	pcre2/src/pcre2_compile.c \
	pcre2/src/pcre2_compile_cgroup.c \
	pcre2/src/pcre2_compile_class.c \
	pcre2/src/pcre2_config.c \
	pcre2/src/pcre2_context.c \
	pcre2/src/pcre2_convert.c \
	pcre2/src/pcre2_dfa_match.c \
	pcre2/src/pcre2_error.c \
	pcre2/src/pcre2_extuni.c \
	pcre2/src/pcre2_find_bracket.c \
	pcre2/src/pcre2_jit_compile.c \
	pcre2/src/pcre2_maketables.c \
	pcre2/src/pcre2_match.c \
	pcre2/src/pcre2_match_data.c \
	pcre2/src/pcre2_match_next.c \
	pcre2/src/pcre2_newline.c \
	pcre2/src/pcre2_ord2utf.c \
	pcre2/src/pcre2_pattern_info.c \
	pcre2/src/pcre2_script_run.c \
	pcre2/src/pcre2_serialize.c \
	pcre2/src/pcre2_string_utils.c \
	pcre2/src/pcre2_study.c \
	pcre2/src/pcre2_substitute.c \
	pcre2/src/pcre2_substring.c \
	pcre2/src/pcre2_tables.c \
	pcre2/src/pcre2_ucd.c \
	pcre2/src/pcre2_valid_utf.c \
	pcre2/src/pcre2_xclass.c

LOCAL_CFLAGS += -I$(LOCAL_PATH)/src/core -I$(LOCAL_PATH)/src/stdlib -I$(LOCAL_PATH)/src/vm -I$(LOCAL_PATH)/src/compiler -I$(LOCAL_PATH)/src/utils -I$(LOCAL_PATH)/src/wasm -I$(LOCAL_PATH)/src/bin -I$(LOCAL_PATH)/src/lua2wasm -I$(LOCAL_PATH)/wasmtime/wasmtime-v45.0.1-aarch64-android-c-api/include -I$(LOCAL_PATH)/pcre2 -I$(LOCAL_PATH)/pcre2/src
LOCAL_CFLAGS += -DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H

# QuickJS 配置
LOCAL_CFLAGS += -I$(LOCAL_PATH)/quickjs -D_GNU_SOURCE -DCONFIG_VERSION=\"2024-01-13\"

# 针对不同 ABI 设置架构优化
ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    LOCAL_CFLAGS += -march=armv8-a
endif
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -march=armv7-a
endif
ifeq ($(TARGET_ARCH_ABI), x86_64)
    LOCAL_CFLAGS += -march=x86-64
endif
ifeq ($(TARGET_ARCH_ABI), x86)
    LOCAL_CFLAGS += -march=i686
endif


# 添加缺失的库依赖
LOCAL_LDLIBS += -llog -lz
# 静态库的 LDLIBS 会被忽略，需要用 EXPORT 传递给链接它的共享库
LOCAL_EXPORT_LDLIBS := -llog

include $(BUILD_STATIC_LIBRARY) 
