/*
** luaccheck.c - LXCLUA 字节码查看器
** 直接加载 .luac 文件并显示字节码结构
** 自动处理 Nirithy== 加密格式
** DifierLine
*/

#define luaccheck_c
#define LUA_CORE

#include "lprefix.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lapi.h"
#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "lstate.h"
#include "lundump.h"
#include "lobfuscate.h"

#define PROGNAME    "luaccheck"

static void fatal(const char* message) {
    fprintf(stderr, "%s: %s\n", PROGNAME, message);
    exit(EXIT_FAILURE);
}

static void usage(void) {
    fprintf(stderr,
        "用法: %s [选项] <luac文件>\n"
        "选项:\n"
        "  -l      列出字节码\n"
        "  -ll     列出字节码 + 常量/局部变量/upvalue\n"
        "  -h      显示此帮助\n"
        "示例:\n"
        "  %s test.luac\n"
        "  %s -ll obfuscated.luac\n",
        PROGNAME, PROGNAME, PROGNAME);
    exit(EXIT_FAILURE);
}

/*
** 从栈上获取 Proto
*/
#define toproto(L,i) getproto(s2v(L->top.p+(i)))

/*
** 打印字节码 (从 luac.c 移植)
*/
#define UPVALNAME(x) ((f->upvalues[x].name) ? getstr(f->upvalues[x].name) : "-")
#define S(i) ((i)!=1?"s":"")
#define SS(i) ((i)!=1?"s":"")
#define COMMENT "\t; "
#define eventname(i) (getstr(tmname[i]))
#define EXTRAARG    GETARG_Ax(code[pc+1])
#define EXTRAARGC   (EXTRAARG*(MAXARG_C+1))

#ifdef VOID
#undef VOID
#endif
#define VOID(p) ((const void*)(p))

static TString **tmname;

static void PrintConstant(const Proto* f, int i) {
    TValue* o = &f->k[i];
    switch (ttypetag(o)) {
    case LUA_VNIL:      printf("nil"); break;
    case LUA_VFALSE:    printf("false"); break;
    case LUA_VTRUE:     printf("true"); break;
    case LUA_VNUMFLT:   printf("%.14g", fltvalue(o)); break;
    case LUA_VNUMINT:   printf("%lld", (long long)ivalue(o)); break;
    case LUA_VSHRSTR:
    case LUA_VLNGSTR:   printf("\"%s\"", getstr(tsvalue(o))); break;
    default:            printf("?"); break;
    }
}

static void PrintType(const Proto* f, int i) {
    TValue* o = &f->k[i];
    switch (ttypetag(o)) {
    case LUA_VNIL:      printf("nil"); break;
    case LUA_VFALSE:    printf("boolean"); break;
    case LUA_VTRUE:     printf("boolean"); break;
    case LUA_VNUMFLT:   printf("number"); break;
    case LUA_VNUMINT:   printf("integer"); break;
    case LUA_VSHRSTR:
    case LUA_VLNGSTR:   printf("string"); break;
    default:            printf("?"); break;
    }
}

static void PrintCode(const Proto* f) {
    const Instruction* code = f->code;
    int pc, n = f->sizecode;
    for (pc = 0; pc < n; pc++) {
        Instruction i = code[pc];
        OpCode o = GET_OPCODE(i);
        int a = GETARG_A(i);
    int b = GETARG_B(i);
    int c = GETARG_C(i);
    int bx = GETARG_Bx(i);
    int sbx = GETARG_sBx(i);
    int ax = GETARG_Ax(i);
    int line = luaG_getfuncline(f, pc);
        printf("\t%d\t", pc + 1);
        if (line > 0) printf("[%d]\t", line); else printf("[-]\t");
        printf("%-9s\t", opnames[o]);
        switch (o) {
        case OP_MOVE:       printf("%d %d", a, b); break;
        case OP_LOADI:      printf("%d %d", a, sbx); break;
        case OP_LOADF:      printf("%d %d", a, sbx); break;
        case OP_LOADK:      printf("%d %d", a, bx); printf(COMMENT); PrintConstant(f, bx); break;
        case OP_LOADKX:     printf("%d", a); printf(COMMENT); PrintConstant(f, EXTRAARG); break;
        case OP_LOADFALSE:  printf("%d", a); break;
        case OP_LFALSESKIP: printf("%d", a); break;
        case OP_LOADTRUE:   printf("%d", a); break;
        case OP_LOADNIL:    printf("%d %d", a, b); printf(COMMENT "%d out", b + 1); break;
        case OP_GETUPVAL:   printf("%d %d", a, b); printf(COMMENT "%s", UPVALNAME(b)); break;
        case OP_SETUPVAL:   printf("%d %d", a, b); printf(COMMENT "%s", UPVALNAME(b)); break;
        case OP_GETTABUP:   printf("%d %d %d", a, b, c); printf(COMMENT "; %s", UPVALNAME(b)); break;
        case OP_GETTABLE:   printf("%d %d %d", a, b, c); break;
        case OP_GETI:       printf("%d %d %d", a, b, c); break;
        case OP_GETFIELD:   printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_SETTABUP:   printf("%d %d %d", a, b, c); printf(COMMENT "; %s", UPVALNAME(a)); break;
        case OP_SETTABLE:   printf("%d %d %d", a, b, c); break;
        case OP_SETI:       printf("%d %d %d", a, b, c); break;
        case OP_SETFIELD:   printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_NEWTABLE:   printf("%d %d %d", a, b, c); break;
        case OP_SELF:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); if (c < f->sizek) PrintConstant(f, c); break;
        case OP_ADDI:       printf("%d %d %d", a, b, c); break;
        case OP_ADDK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_SUBK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_MULK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_MODK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_POWK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_DIVK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_IDIVK:      printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_BANDK:      printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_BORK:       printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_BXORK:      printf("%d %d %d", a, b, c); printf(COMMENT "; "); PrintConstant(f, c); break;
        case OP_SHRI:       printf("%d %d %d", a, b, c); break;
        case OP_SHLI:       printf("%d %d %d", a, b, c); break;
        case OP_ADD:        printf("%d %d %d", a, b, c); break;
        case OP_SUB:        printf("%d %d %d", a, b, c); break;
        case OP_MUL:        printf("%d %d %d", a, b, c); break;
        case OP_MOD:        printf("%d %d %d", a, b, c); break;
        case OP_POW:        printf("%d %d %d", a, b, c); break;
        case OP_DIV:        printf("%d %d %d", a, b, c); break;
        case OP_IDIV:       printf("%d %d %d", a, b, c); break;
        case OP_BAND:       printf("%d %d %d", a, b, c); break;
        case OP_BOR:        printf("%d %d %d", a, b, c); break;
        case OP_BXOR:       printf("%d %d %d", a, b, c); break;
        case OP_SHL:        printf("%d %d %d", a, b, c); break;
        case OP_SHR:        printf("%d %d %d", a, b, c); break;
        case OP_SPACESHIP:  printf("%d %d %d", a, b, c); break;
        case OP_MMBIN:      printf("%d %d %d", a, b, c); printf(COMMENT "%s", eventname(c)); break;
        case OP_MMBINI:     printf("%d %d %d", a, b, c); printf(COMMENT "%s", eventname(c)); break;
        case OP_MMBINK:     printf("%d %d %d", a, b, c); printf(COMMENT "%s ; ", eventname(c)); PrintConstant(f, b); break;
        case OP_UNM:        printf("%d %d", a, b); break;
        case OP_BNOT:       printf("%d %d", a, b); break;
        case OP_NOT:        printf("%d %d", a, b); break;
        case OP_LEN:        printf("%d %d", a, b); break;
        case OP_CONCAT:     printf("%d %d", a, b); break;
        case OP_CLOSE:      printf("%d", a); break;
        case OP_TBC:        printf("%d %d", a, b); break;
        case OP_JMP:        printf("%d", sbx); printf(COMMENT "to %d", sbx + pc + 2); break;
        case OP_EQ:         printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_LT:         printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_LE:         printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_EQK:        printf("%d %d %d", a, b, c); printf(COMMENT "%s ; ", (a ? "true->skip" : "false->skip")); PrintConstant(f, c); break;
        case OP_EQI:        printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_LTI:        printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_LEI:        printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_GTI:        printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_GEI:        printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_TEST:       printf("%d %d", a, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_TESTSET:    printf("%d %d %d", a, b, c); printf(COMMENT "%s", (a ? "true->skip" : "false->skip")); break;
        case OP_CALL:       printf("%d %d %d", a, b, c); break;
        case OP_TAILCALL:   printf("%d %d %d", a, b, c); break;
        case OP_RETURN:     printf("%d %d %d", a, b, c); break;
        case OP_RETURN0:    break;
        case OP_RETURN1:    printf("%d", a); break;
        case OP_FORLOOP:    printf("%d %d", a, sbx); printf(COMMENT "to %d", sbx + pc + 2); break;
        case OP_FORPREP:    printf("%d %d", a, sbx); printf(COMMENT "to %d", sbx + pc + 2); break;
        case OP_TFORPREP:   printf("%d %d", a, sbx); printf(COMMENT "to %d", sbx + pc + 2); break;
        case OP_TFORCALL:   printf("%d %d", a, c); break;
        case OP_TFORLOOP:   printf("%d %d", a, sbx); printf(COMMENT "to %d", sbx + pc + 2); break;
        case OP_SETLIST:    printf("%d %d %d", a, b, c); break;
        case OP_CLOSURE:    printf("%d %d", a, bx); printf(COMMENT "%p", VOID(f->p[bx])); break;
        case OP_VARARG:     printf("%d %d", a, b); break;
        case OP_GETVARG:    printf("%d %d", a, b); break;
        case OP_ERRNNIL:    printf("%d", a); break;
        case OP_VARARGPREP: printf("%d", a); break;
        case OP_IS:         printf("%d %d %d", a, b, c); break;
        case OP_TESTNIL:    printf("%d %d", a, b); break;
        case OP_NEWCLASS:   printf("%d %d %d", a, b, c); break;
        case OP_INHERIT:    printf("%d %d", a, b); break;
        case OP_GETSUPER:   printf("%d %d", a, b); break;
        case OP_SETMETHOD:  printf("%d %d %d", a, b, c); break;
        case OP_SETSTATIC:  printf("%d %d %d", a, b, c); break;
        case OP_NEWOBJ:     printf("%d %d", a, b); break;
        case OP_GETPROP:    printf("%d %d %d", a, b, c); break;
        case OP_SETPROP:    printf("%d %d %d", a, b, c); break;
        case OP_INSTANCEOF: printf("%d %d", a, b); break;
        case OP_IMPLEMENT:  printf("%d %d", a, b); break;
        case OP_SETIFACEFLAG: printf("%d %d", a, b); break;
        case OP_ADDMETHOD:  printf("%d %d %d", a, b, c); break;
        case OP_SLICE:      printf("%d %d %d", a, b, c); break;
        case OP_NOP:        break;
        case OP_CASE:       printf("%d %d %d", a, b, c); break;
        case OP_NEWCONCEPT: printf("%d %d", a, b); break;
        case OP_NEWNAMESPACE: printf("%d %d", a, b); break;
        case OP_LINKNAMESPACE: printf("%d %d %d", a, b, c); break;
        case OP_EXTRAARG:   printf("%d", ax); break;
        default:            printf("%d %d %d %d", a, b, c, bx); break;
        }
        printf("\n");
    }
}

/*
** VM 指令名称表（对应 lobfuscate.h 中的 VMOpCode 枚举）
*/
static const char *vm_opnames[] = {
    "NOP",          /* VM_OP_NOP = 0 */
    "LOAD",         /* VM_OP_LOAD = 1 */
    "MOVE",         /* VM_OP_MOVE = 2 */
    "STORE",        /* VM_OP_STORE = 3 */
    "ADD",          /* VM_OP_ADD = 4 */
    "SUB",          /* VM_OP_SUB = 5 */
    "MUL",          /* VM_OP_MUL = 6 */
    "DIV",          /* VM_OP_DIV = 7 */
    "MOD",          /* VM_OP_MOD = 8 */
    "POW",          /* VM_OP_POW = 9 */
    "UNM",          /* VM_OP_UNM = 10 */
    "IDIV",         /* VM_OP_IDIV = 11 */
    "BAND",         /* VM_OP_BAND = 12 */
    "BOR",          /* VM_OP_BOR = 13 */
    "BXOR",         /* VM_OP_BXOR = 14 */
    "BNOT",         /* VM_OP_BNOT = 15 */
    "SHL",          /* VM_OP_SHL = 16 */
    "SHR",          /* VM_OP_SHR = 17 */
    "JMP",          /* VM_OP_JMP = 18 */
    "JEQ",          /* VM_OP_JEQ = 19 */
    "JNE",          /* VM_OP_JNE = 20 */
    "JLT",          /* VM_OP_JLT = 21 */
    "JLE",          /* VM_OP_JLE = 22 */
    "JGT",          /* VM_OP_JGT = 23 */
    "JGE",          /* VM_OP_JGE = 24 */
    "CALL",         /* VM_OP_CALL = 25 */
    "RET",          /* VM_OP_RET = 26 */
    "TAILCALL",     /* VM_OP_TAILCALL = 27 */
    "NEWTABLE",     /* VM_OP_NEWTABLE = 28 */
    "GETTABLE",     /* VM_OP_GETTABLE = 29 */
    "SETTABLE",     /* VM_OP_SETTABLE = 30 */
    "GETFIELD",     /* VM_OP_GETFIELD = 31 */
    "SETFIELD",     /* VM_OP_SETFIELD = 32 */
    "GETI",         /* VM_OP_GETI = 33 */
    "SETI",         /* VM_OP_SETI = 34 */
    "GETTABUP",     /* VM_OP_GETTABUP = 35 */
    "SETTABUP",     /* VM_OP_SETTABUP = 36 */
    "CLOSURE",      /* VM_OP_CLOSURE = 37 */
    "GETUPVAL",     /* VM_OP_GETUPVAL = 38 */
    "SETUPVAL",     /* VM_OP_SETUPVAL = 39 */
    "CONCAT",       /* VM_OP_CONCAT = 40 */
    "LEN",          /* VM_OP_LEN = 41 */
    "NOT",          /* VM_OP_NOT = 42 */
    "TEST",         /* VM_OP_TEST = 43 */
    "TESTSET",      /* VM_OP_TESTSET = 44 */
    "FORLOOP",      /* VM_OP_FORLOOP = 45 */
    "FORPREP",      /* VM_OP_FORPREP = 46 */
    "TFORPREP",     /* VM_OP_TFORPREP = 47 */
    "TFORCALL",     /* VM_OP_TFORCALL = 48 */
    "TFORLOOP",     /* VM_OP_TFORLOOP = 49 */
    "VARARG",       /* VM_OP_VARARG = 50 */
    "VARARGPREP",   /* VM_OP_VARARGPREP = 51 */
    "SELF",         /* VM_OP_SELF = 52 */
    "SETLIST",      /* VM_OP_SETLIST = 53 */
    "LOADKX",       /* VM_OP_LOADKX = 54 */
    "LOADFALSE",    /* VM_OP_LOADFALSE = 55 */
    "LOADTRUE",     /* VM_OP_LOADTRUE = 56 */
    "LOADNIL",      /* VM_OP_LOADNIL = 57 */
    "MMBIN",        /* VM_OP_MMBIN = 58 */
    "MMBINI",       /* VM_OP_MMBINI = 59 */
    "MMBINK",       /* VM_OP_MMBINK = 60 */
    "EXT1",         /* VM_OP_EXT1 = 61 */
    "EXT2",         /* VM_OP_EXT2 = 62 */
    "EXT3",         /* VM_OP_EXT3 = 63 */
    "EXT4",         /* VM_OP_EXT4 = 64 */
    "EXT5",         /* VM_OP_EXT5 = 65 */
    "EXT6",         /* VM_OP_EXT6 = 66 */
    "EXT7",         /* VM_OP_EXT7 = 67 */
    "HALT",         /* VM_OP_HALT = 68 */
};

#define VM_OP_COUNT (sizeof(vm_opnames) / sizeof(vm_opnames[0]))

/*
** 显示VM保护指令（解密后的VM指令）
** VM操作码是随机打乱的，通过reverse_map[vm_op]获取对应的Lua操作码
*/
static void PrintVMCode(const Proto* f) {
    VMCodeTable *vt = f->vm_code_table;
    if (vt == NULL) return;

    printf("\n========== VM保护指令 (已解密) ==========\n");
    printf("  指令数: %d\n", vt->size);
    printf("  加密密钥: 0x%016llx\n", (unsigned long long)vt->encrypt_key);
    printf("  种子: 0x%08x\n", vt->seed);
    printf("------------------------------------------\n");

    int pc;
    for (pc = 0; pc < vt->size; pc++) {
        VMInstruction encrypted = vt->code[pc];
        VMInstruction decrypted = luaO_decryptVMInst(encrypted, vt->encrypt_key, pc);
        int vm_op = VM_GET_OP(decrypted);
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        int c = VM_GET_C(decrypted);
        int64_t bx = VM_GET_Bx(decrypted);
        int flags = VM_GET_FLAGS(decrypted);

        /* 查找对应的Lua操作码 */
        int lua_op = (vm_op >= 0 && vm_op < VM_MAP_SIZE) ? vt->reverse_map[vm_op] : -1;

        printf("\t%d\t", pc);

        if (vm_op == VM_OP_HALT) {
            printf("VM_HALT         \t(halt)\t; 结束");
        } else {
            /* 显示Lua操作码名称（因为VM操作码是随机打乱的） */
            const char *lua_name = (lua_op >= 0 && lua_op < NUM_OPCODES) ? opnames[lua_op] : "???";
            printf("VM_%-9s\t", lua_name);
            printf("a=%d b=%d c=%d", a, b, c);
            if (flags != 0) printf(" flags=%d", flags);
            if (bx != 0) printf(" bx=%lld", (long long)bx);
            printf("\t; vm_op=%d", vm_op);
        }

        printf("\n");
    }
    printf("==========================================\n");
}

static void PrintHeader(const Proto* f) {
    const char* s = f->source ? getstr(f->source) : "=?";
    if (*s == '@' || *s == '=')
        s++;
    else if (*s == LUA_SIGNATURE[0])
        s = "(bstring)";
    else
        s = "(string)";
    printf("\n%s <%s:%d,%d> (%d instruction%s at %p)\n",
        (f->linedefined == 0) ? "main" : "function", s,
        f->linedefined, f->lastlinedefined,
        f->sizecode, (f->sizecode != 1) ? "s" : "", VOID(f));
    printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
        (int)(f->numparams), (f->is_vararg) ? "+" : "",
        (f->numparams != 1) ? "s" : "",
        f->maxstacksize, (f->maxstacksize != 1) ? "s" : "",
        f->sizeupvalues, (f->sizeupvalues != 1) ? "s" : "");
    printf("%d local%s, %d constant%s, %d function%s\n",
        f->sizelocvars, (f->sizelocvars != 1) ? "s" : "",
        f->sizek, (f->sizek != 1) ? "s" : "",
        f->sizep, (f->sizep != 1) ? "s" : "");
    /* 显示混淆标志 */
    if (f->difierline_mode != 0) {
        printf("混淆标志: 0x%08x", f->difierline_mode);
        if (f->difierline_mode & OBFUSCATE_VM_PROTECT) printf(" [VM_PROTECT]");
        if (f->difierline_mode & OBFUSCATE_STR_ENCRYPT) printf(" [STR_ENCRYPT]");
        if (f->difierline_mode & OBFUSCATE_CFF) printf(" [CFF]");
        printf("\n");
        if (f->difierline_mode & OBFUSCATE_VM_PROTECT && f->vm_code_table != NULL) {
            printf("VM指令数: %d, 加密密钥: 0x%016llx\n",
                f->vm_code_table->size, (unsigned long long)f->vm_code_table->encrypt_key);
        }
    }
}

static void PrintDebug(const Proto* f) {
    int i, n;
    n = f->sizek;
    printf("constants (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t", i);
        PrintType(f, i);
        printf("\t");
        PrintConstant(f, i);
        printf("\n");
    }
    n = f->sizelocvars;
    printf("locals (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\t%d\t%d\n",
            i, getstr(f->locvars[i].varname), f->locvars[i].startpc + 1, f->locvars[i].endpc + 1);
    }
    n = f->sizeupvalues;
    printf("upvalues (%d) for %p:\n", n, VOID(f));
    for (i = 0; i < n; i++) {
        printf("\t%d\t%s\t%d\t%d\n",
            i, UPVALNAME(i), f->upvalues[i].instack, f->upvalues[i].idx);
    }
}

static void PrintFunction(const Proto* f, int full) {
    int i, n = f->sizep;
    PrintHeader(f);
    /* VM保护模式下，原始字节码已被混淆，直接显示VM指令 */
    if (f->difierline_mode & OBFUSCATE_VM_PROTECT && f->vm_code_table != NULL) {
        PrintVMCode(f);
    } else {
        PrintCode(f);
    }
    if (full) PrintDebug(f);
    for (i = 0; i < n; i++) PrintFunction(f->p[i], full);
}

static int pmain(lua_State* L) {
    int argc = (int)lua_tointeger(L, 1);
    char** argv = (char**)lua_touserdata(L, 2);
    int listing = 0;
    const char* filename = NULL;

    tmname = G(L)->tmname;

    /* 解析参数 */
    int i;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "-l") == 0) listing = 1;
            else if (strcmp(argv[i], "-ll") == 0) listing = 2;
            else if (strcmp(argv[i], "-h") == 0) { usage(); return 0; }
            else fatal("未知选项");
        } else {
            if (filename == NULL) filename = argv[i];
            else fatal("只能指定一个文件");
        }
    }

    if (filename == NULL) usage();

    /* 加载文件（自动处理 Nirithy== 加密和标准 .luac 格式） */
    if (luaL_loadfile(L, filename) != LUA_OK) {
        fprintf(stderr, "%s: %s\n", PROGNAME, lua_tostring(L, -1));
        return 1;
    }

    /* 获取 Proto */
    const Proto* f = toproto(L, -1);

    /* 显示文件信息 */
    printf("========================================\n");
    printf("  LXCLUA 字节码: %s\n", filename);
    printf("========================================\n");

    /* 显示字节码 */
    if (listing > 0) {
        PrintFunction(f, listing > 1);
    } else {
        /* 默认显示摘要信息 */
        PrintHeader(f);
        printf("  使用 -l 显示字节码, -ll 显示完整信息\n");
    }

    return 0;
}

int main(int argc, char* argv[]) {
    lua_State* L = luaL_newstate();
    if (L == NULL) fatal("无法创建 Lua 状态");
    luaL_openlibs(L);

    lua_pushcfunction(L, &pmain);
    lua_pushinteger(L, argc - 1);
    lua_pushlightuserdata(L, argv + 1);

    int status = lua_pcall(L, 2, 0, 0);
    if (status != LUA_OK) {
        fprintf(stderr, "%s: %s\n", PROGNAME, lua_tostring(L, -1));
        lua_close(L);
        return EXIT_FAILURE;
    }

    lua_close(L);
    return EXIT_SUCCESS;
}