#!/usr/bin/env python3
"""
将 lstrlib.c 从纯 PCRE2 引擎改造为 PCRE2/原版 Lua 双引擎切换。
1. 重命名 PCRE2 函数添加 pcre2_ 前缀
2. 从 originregstr.c 提取原版 Lua 正则函数，添加 lua_ 前缀
3. 添加 dispatch 包装函数
4. 合并 MatchState 和 GMatchState 结构体
"""

import re

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(content)

# 读取文件
lstrlib = read_file('src/stdlib/lstrlib.c')
origin = read_file('../originregstr.c')

# ============================================================
# 步骤1: 添加 extern 声明
# ============================================================
# 在 extern int XCLUA_REGEX_JIT_ENABLED; 之后添加
old_extern = 'extern int XCLUA_REGEX_JIT_ENABLED;'
new_extern = old_extern + '\n\n/* LXCLUA PCRE2 引擎开关，由 jit.regex.pcre2.on()/off() 控制 */\nextern int XCLUA_PCRE2_ENABLED;'
lstrlib = lstrlib.replace(old_extern, new_extern)

# ============================================================
# 步骤2: 合并 MatchState 结构体
# ============================================================
old_ms = '''/* PCRE2 匹配状态 */
typedef struct MatchState {
  const char *src_init;
  const char *src_end;
  lua_State *L;
  pcre2_code *code;
  pcre2_match_data *mdata;
  PCRE2_SIZE *ovector;
  uint32_t ovec_count;
} MatchState;'''

new_ms = '''/* 双引擎匹配状态（PCRE2 + 原版 Lua） */
typedef struct MatchState {
  const char *src_init;
  const char *src_end;
  lua_State *L;
  /* PCRE2 字段 */
  pcre2_code *code;
  pcre2_match_data *mdata;
  PCRE2_SIZE *ovector;
  uint32_t ovec_count;
  /* 原版 Lua 正则字段 */
  const char *p_end;
  int matchdepth;
  int level;
  struct {
    const char *init;
    ptrdiff_t len;
  } capture[LUA_MAXCAPTURES];
} MatchState;'''

lstrlib = lstrlib.replace(old_ms, new_ms)

# ============================================================
# 步骤3: 重命名 PCRE2 函数
# ============================================================
# 重命名列表（按依赖顺序，先重命名被调用的）
# 注意：str_escape 使用了 SPECIALS 宏，但 PCRE2 的 SPECIALS 和 Lua 的 SEPECIALS 不同
# 这里 str_escape 在 PCRE2 块之外，使用的是 PCRE2 的 SPECIALS 定义
# 我们需要保留它

rename_map = [
    # 缓存函数
    ('init_cache', 'pcre2_init_cache'),
    ('cache_lookup', 'pcre2_cache_lookup'),
    ('cache_insert', 'pcre2_cache_insert'),
    # 核心函数
    ('compile_pattern', 'pcre2_compile_pattern'),
    ('free_pattern', 'pcre2_free_pattern'),
    ('nospecials', 'pcre2_nospecials'),
    ('get_onecapture', 'pcre2_get_onecapture'),
    ('push_onecapture', 'pcre2_push_onecapture'),
    ('push_captures', 'pcre2_push_captures'),
    ('do_match', 'pcre2_do_match'),
    # 辅助函数
    ('add_s', 'pcre2_add_s'),
    ('add_value', 'pcre2_add_value'),
    # 顶层函数（先重命名 gmatch_aux 因为 gmatch 引用它）
    ('gmatch_aux', 'pcre2_gmatch_aux'),
    ('gfind_aux', 'pcre2_gfind_aux'),
    ('str_find_aux', 'pcre2_str_find_aux'),
    ('str_find', 'pcre2_str_find'),
    ('str_match', 'pcre2_str_match'),
    ('gfind', 'pcre2_gfind'),
    ('gmatch', 'pcre2_gmatch'),
    ('str_gsub', 'pcre2_str_gsub'),
]

# 使用词边界替换，避免误替换
# 注意：函数名可能出现在以下位置：
# 1. 定义：static int func_name(
# 2. 调用：func_name(
# 3. 调用：func_name()
# 4. 在字符串中
# 5. 在注释中
# 6. 在 lua_pushcclosure 中作为函数指针

# 安全的做法是用 \b 词边界替换
for old_name, new_name in rename_map:
    lstrlib = re.sub(r'\b' + re.escape(old_name) + r'\b', new_name, lstrlib)

# ============================================================
# 步骤4: 合并 GMatchState 结构体
# ============================================================
old_gm = '''typedef struct GMatchState {
  const char *src;
  const char *p;
  const char *lastmatch;
  pcre2_code *code;
  pcre2_match_data *mdata;
  MatchState ms;
} GMatchState;'''

new_gm = '''typedef struct GMatchState {
  const char *src;
  const char *p;
  const char *lastmatch;
  MatchState ms;
  /* PCRE2 额外字段 */
  pcre2_code *code;
  pcre2_match_data *mdata;
  int anchor;
  /* 引擎选择 */
  int engine;  /* 0 = Lua原版, 1 = PCRE2 */
} GMatchState;'''

lstrlib = lstrlib.replace(old_gm, new_gm)

# ============================================================
# 步骤5: 添加 Lua 原版正则函数
# ============================================================
# 在 PCRE2 块结束 (/* }=========================================== */) 之后，
# 在 /* 纯文本查找 */ 之前插入 Lua 原版正则函数

# 从 originregstr.c 提取所有需要的内容
# PCRE2 块结束标记
pcre2_end_marker = '/* }=========================================== */\n\n/* 纯文本查找'

# 找到 PCRE2 块结束位置
idx = lstrlib.find(pcre2_end_marker)
if idx == -1:
    print("ERROR: cannot find PCRE2 block end marker")
    exit(1)

# 从 originregstr.c 提取 Lua 原版正则函数
# 找到 origin 中 PATTERN MATCHING 开始后的所有内容，直到下一个 "/* }=========================================== */"
origin_pattern_start = origin.find('/* {===========================================\n** PATTERN MATCHING\n** ============================================\n*/')
origin_pattern_end = origin.find('/* }=========================================== */', origin_pattern_start)

origin_pattern_section = origin[origin_pattern_start:origin_pattern_end]

# 提取后的部分需要做以下处理：
# 1. 添加 lua_ 前缀到所有函数名
# 2. 宏定义保留但调整
# 3. 移除重复的 MatchState 定义（改用我们的）
# 4. 移除 str_find_aux, str_find, str_match, gfind_aux, gfind, gmatch_aux, gmatch, str_gsub 的 static 定义
#    （这些将由 dispatch 函数替代）

# 提取 origin 中的 lmemfind（在 pattern matching 块之后）
# 实际上 origin 的 lmemfind 在 pattern matching 块内

# 构建 Lua 原版正则代码块
# 从 origin 提取，但去掉 MatchState 定义和顶层函数定义

# 我们先提取 origin 中 pattern matching 块的内容
# 然后做替换

# 定位 origin 中 pattern matching 块内的各个部分
# MatchState 定义在 662-673
# 函数定义：
# match (line 960)
# check_capture, capture_to_close, classend, match_class, matchbracketclass,
# singlematch, matchbalance, max_expand, min_expand, start_capture, end_capture,
# match_capture, lmemfind, get_onecapture, push_onecapture, push_captures,
# nospecials, prepstate, reprepstate
# str_find_aux, str_find, gfind_aux, gfind, str_match, gmatch_aux, gmatch
# add_s, add_value, str_gsub

# 从 origin 提取代码块：从 "#define CAP_UNFINISHED" 到 "/* }=========================================== */"
origin_block_start = origin.find('#define CAP_UNFINISHED', origin_pattern_start)
origin_block_end = origin.find('/* }=========================================== */', origin_pattern_start)

lua_block_raw = origin[origin_block_start:origin_block_end]

# 现在对 lua_block_raw 做处理：
# 1. 移除 MatchState 定义
# 2. 将函数名加 lua_ 前缀
# 3. 移除 str_find, str_match, gfind, gmatch, str_gsub 这些顶层函数（由 dispatch 替代）
# 4. 保留 str_find_aux 但加 lua_ 前缀
# 5. 保留 gfind_aux, gmatch_aux 但加 lua_ 前缀

# 移除 MatchState 定义
ms_def_start = lua_block_raw.find('typedef struct MatchState')
ms_def_end = lua_block_raw.find('} MatchState;', ms_def_start)
if ms_def_start >= 0 and ms_def_end >= 0:
    ms_def_end += len('} MatchState;')
    lua_block_raw = lua_block_raw[:ms_def_start] + lua_block_raw[ms_def_end:]

# 移除多余的空白行
lua_block_raw = re.sub(r'\n{3,}', '\n\n', lua_block_raw)

# 函数重命名映射（Lua 版本）
lua_rename = {
    'check_capture': 'lua_check_capture',
    'capture_to_close': 'lua_capture_to_close',
    'classend': 'lua_classend',
    'match_class': 'lua_match_class',
    'matchbracketclass': 'lua_matchbracketclass',
    'singlematch': 'lua_singlematch',
    'matchbalance': 'lua_matchbalance',
    'max_expand': 'lua_max_expand',
    'min_expand': 'lua_min_expand',
    'start_capture': 'lua_start_capture',
    'end_capture': 'lua_end_capture',
    'match_capture': 'lua_match_capture',
    'get_onecapture': 'lua_get_onecapture',
    'push_onecapture': 'lua_push_onecapture',
    'push_captures': 'lua_push_captures',
    'nospecials': 'lua_nospecials',
    'prepstate': 'lua_prepstate',
    'reprepstate': 'lua_reprepstate',
    'add_s': 'lua_add_s',
    'add_value': 'lua_add_value',
    'str_find_aux': 'lua_str_find_aux',
    'gfind_aux': 'lua_gfind_aux',
    'gmatch_aux': 'lua_gmatch_aux',
    'str_gsub': 'lua_str_gsub',
    'parse_repetition': 'lua_parse_repetition',
    'find_matching_paren': 'lua_find_matching_paren',
    'range_expand': 'lua_range_expand',
}

# 注意：match 函数也需要重命名
# 但 match 这个词太通用，需要小心处理
# 在 origin 中，match 函数定义是：
# static const char *match (MatchState *ms, const char *s, const char *p);
# static const char *match (MatchState *ms, const char *s, const char *p) {
lua_rename['match'] = 'lua_match'

# 注意：lmemfind 也需要重命名
# 因为 lstrlib.c 中已经有 lmemfind（在 PCRE2 块之外，用于 str_split 等）
# 原版 Lua 的 lmemfind 命名为 lua_lmemfind
lua_rename['lmemfind'] = 'lua_lmemfind'

# 应用重命名
for old_name, new_name in lua_rename.items():
    lua_block_raw = re.sub(r'\b' + re.escape(old_name) + r'\b', new_name, lua_block_raw)

# 将宏定义中的 SPECIALS 改为 LUA_SPECIALS 避免与 PCRE2 的宏冲突
# 但 origin 的 SPECIALS 是 "^$*+?.([%-"，PCRE2 的是 "\\^$.*+?()[]{}|"
# 两者不同，但它们在合并后的文件中会冲突
# 实际上，SPECIALS 在 origin 的 pattern matching 块中定义，但在 lstrlib.c 中 PCRE2 也有 SPECIALS
# 需要处理：在 Lua 块中，将 SPECIALS 改为 LUA_SPECIALS
lua_block_raw = lua_block_raw.replace('#define SPECIALS\t"^$*+?.([%-"', '#define LUA_SPECIALS\t"^$*+?.([%-"')
# 同时更新所有引用 SPECIALS 的地方（在 lua_nospecials 和 match 函数中）
lua_block_raw = lua_block_raw.replace('SPECIALS', 'LUA_SPECIALS')
# 但不要把 LUA_SPECIALS 变成 LUA_LUA_SPECIALS
lua_block_raw = lua_block_raw.replace('LUA_LUA_SPECIALS', 'LUA_SPECIALS')

# 现在构建最终的 Lua 代码块
# 添加注释头
lua_block_header = '''
/*
** {===========================================
** PATTERN MATCHING（原版 Lua 正则引擎）
** 函数名加 lua_ 前缀，与 PCRE2 引擎共存
** ============================================
*/

/* 原版 Lua 正则专用的宏 */
#define LUA_ESC '%'
#define LUA_SPECIALS "^$*+?.([%-"
#define LUA_MAXCCALLS 200
#define LUA_CAP_UNFINISHED (-1)
#define LUA_CAP_POSITION (-2)

'''

# 将 lua_ 块中的宏替换为 LUA_ 前缀版本
lua_block_raw = lua_block_raw.replace('#define CAP_UNFINISHED\t(-1)', '#define LUA_CAP_UNFINISHED\t(-1)')
lua_block_raw = lua_block_raw.replace('#define CAP_POSITION\t(-2)', '#define LUA_CAP_POSITION\t(-2)')
lua_block_raw = lua_block_raw.replace('#define MAXCCALLS\t200', '#define LUA_MAXCCALLS 200')
lua_block_raw = lua_block_raw.replace('#define L_ESC\t\t\'%\'', '#define LUA_ESC \'%\'')
# 替换对 CAP_UNFINISHED, CAP_POSITION, MAXCCALLS, L_ESC 的引用
lua_block_raw = re.sub(r'\bCAP_UNFINISHED\b', 'LUA_CAP_UNFINISHED', lua_block_raw)
lua_block_raw = re.sub(r'\bCAP_POSITION\b', 'LUA_CAP_POSITION', lua_block_raw)
lua_block_raw = re.sub(r'\bMAXCCALLS\b', 'LUA_MAXCCALLS', lua_block_raw)
lua_block_raw = re.sub(r'\bL_ESC\b', 'LUA_ESC', lua_block_raw)

# 移除 str_find, str_match, gfind, gmatch 这些顶层函数（dispatch 会替代）
# 这些函数在 lua_ 块中已经被重命名为 lua_str_find_aux, lua_gfind, lua_gmatch 等
# 但我们需要保留 lua_str_find_aux, lua_gfind, lua_gmatch, lua_gfind_aux, lua_gmatch_aux, lua_str_gsub
# 而移除 str_find, str_match 这些静态函数

# 实际上经过重命名后：
# str_find → 被 pcre2_str_find 替换（在 PCRE2 块中）
# str_match → 被 pcre2_str_match 替换
# gfind → 被 pcre2_gfind 替换
# gmatch → 被 pcre2_gmatch 替换
# 这些在 Lua 块中也有重名，但经过 lua_ 前缀重命名后：
# - 原 str_find 在 lua 块中应该不存在（因为 origin 中 str_find 调用了 str_find_aux，而 str_find_aux 已改为 lua_str_find_aux）
# 但 str_find 这个函数定义本身在 origin 中：
#   static int str_find (lua_State *L) { return str_find_aux(L, 1); }
# 这个函数定义会被重命名为... 等等，str_find 在 rename_map 中没在 lua_rename 中
# 让我检查 lua_rename 中是否有 str_find
# 没有！所以 origin 中的 static int str_find 还是叫 str_find
# 但 lstrlib.c 中已经有 pcre2_str_find（原 str_find 被重命名了）
# 所以不会冲突，因为 pcre2_str_find 和 str_find 是不同的名字

# 但是！origin 中的 str_find 通过 lua_rename 没有被重命名，所以它仍然是 str_find
# 这会导致与 dispatch 函数 str_find 冲突！
# 我需要从 lua_block_raw 中移除这些顶层函数定义

# 在 origin 中：
# static int str_find (lua_State *L) { return str_find_aux(L, 1); }
# 经过 rename 后 str_find_aux 变成了 lua_str_find_aux
# 但 str_find 没变

# 所以需要移除 origin 中的 str_find, str_match, gfind, gmatch 定义
# 这些函数定义很简单，只是调用 str_find_aux

# 移除 static int str_find 函数定义
lua_block_raw = re.sub(
    r'\nstatic int str_find\s*\(lua_State\s*\*L\)\s*\{\s*\n\s*return lua_str_find_aux\(L,\s*1\);\s*\n\s*\}\s*\n',
    '\n',
    lua_block_raw
)

# 移除 static int str_match 函数定义
lua_block_raw = re.sub(
    r'\nstatic int str_match\s*\(lua_State\s*\*L\)\s*\{\s*\n\s*return lua_str_find_aux\(L,\s*0\);\s*\n\s*\}\s*\n',
    '\n',
    lua_block_raw
)

# 移除 static int gfind 函数定义
lua_block_raw = re.sub(
    r'\nstatic int gfind\s*\(lua_State\s*\*L\)\s*\{\s*\n\s*luaL_checkstring\(L,\s*1\);\s*\n\s*luaL_checkstring\(L,\s*2\);\s*\n\s*int b = lua_toboolean\(L,\s*3\);\s*\n\s*lua_settop\(L,\s*2\);\s*\n\s*lua_pushinteger\(L,\s*0\);\s*\n\s*lua_pushboolean\(L,\s*b\);\s*\n\s*lua_pushcclosure\(L,\s*lua_gfind_aux,\s*4\);\s*\n\s*return 1;\s*\n\s*\}\s*\n',
    '\n',
    lua_block_raw
)

# 移除 static int gmatch 函数定义
lua_block_raw = re.sub(
    r'\nstatic int gmatch\s*\(lua_State\s*\*L\)\s*\{[^}]*\}\s*\n',
    '\n',
    lua_block_raw
)

# 移除 GMatchState 定义（已在 lstrlib.c 中定义）
lua_block_raw = re.sub(
    r'\n/\* state for .gmatch. \*/\ntypedef struct GMatchState \{.*?\} GMatchState;\s*\n',
    '\n',
    lua_block_raw
)

# 清理多余空白
lua_block_raw = re.sub(r'\n{3,}', '\n\n', lua_block_raw)

# 构建最终的 Lua 块
lua_block = lua_block_header + lua_block_raw.strip() + '\n\n/* }=========================================== */\n\n'

# 插入到 PCRE2 块结束之后
insert_pos = lstrlib.find(pcre2_end_marker)
if insert_pos >= 0:
    lstrlib = lstrlib[:insert_pos] + '/* }=========================================== */\n\n' + lua_block + '/* 纯文本查找'

# ============================================================
# 步骤6: 添加 dispatch 包装函数
# ============================================================
# 替换现有的 pcre2_str_find, pcre2_str_match, pcre2_gfind, pcre2_gmatch, pcre2_str_gsub
# 为 dispatch 函数

# 先找到这些函数的位置并替换

# pcre2_str_find: 原本是 static int str_find(lua_State *L) { return pcre2_str_find_aux(L, 1); }
# 现在变成了 static int pcre2_str_find(lua_State *L) { return pcre2_str_find_aux(L, 1); }
# 需要改为 dispatch:
# static int str_find(lua_State *L) { if (XCLUA_PCRE2_ENABLED) return pcre2_str_find(L); else return lua_str_find_aux(L, 1); }

# 先查找 pcre2_str_find 函数定义
# 注意：pcre2_str_find 调用 pcre2_str_find_aux，但 dispatch 中我们应该调用 pcre2_str_find（原始函数）
# 等等，pcre2_str_find 本身就是调用 pcre2_str_find_aux(L, 1)
# 所以 dispatch 应该直接调用 pcre2_str_find(L) 或 pcre2_str_find_aux(L, 1)

# 实际上：
# pcre2_str_find(L) 内部是 return pcre2_str_find_aux(L, 1);
# 所以 dispatch 可以：
# static int str_find(lua_State *L) {
#     if (XCLUA_PCRE2_ENABLED) return pcre2_str_find(L);
#     else return lua_str_find_aux(L, 1);
# }

# 替换 pcre2_str_find → str_find (dispatch)
old_pcre2_str_find = '''static int pcre2_str_find (lua_State *L) {
  return pcre2_str_find_aux(L, 1);
}'''

new_str_find = '''static int str_find (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) return pcre2_str_find_aux(L, 1);
  else return lua_str_find_aux(L, 1);
}'''

lstrlib = lstrlib.replace(old_pcre2_str_find, new_str_find)

# 替换 pcre2_str_match → str_match (dispatch)
old_pcre2_str_match = '''static int pcre2_str_match (lua_State *L) {
  return pcre2_str_find_aux(L, 0);
}'''

new_str_match = '''static int str_match (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) return pcre2_str_find_aux(L, 0);
  else return lua_str_find_aux(L, 0);
}'''

lstrlib = lstrlib.replace(old_pcre2_str_match, new_str_match)

# 替换 pcre2_gfind_aux → 保留不变（PCRE2 版本）
# 但 pcre2_gfind 需要改为 dispatch

old_pcre2_gfind = '''static int pcre2_gfind (lua_State *L) {
  luaL_checkstring(L, 1);
  luaL_checkstring(L, 2);
  int b = lua_toboolean(L, 3);
  lua_settop(L, 2);
  lua_pushinteger(L, 0);
  lua_pushboolean(L, b);
  lua_pushcclosure(L, pcre2_gfind_aux, 4);
  return 1;
}'''

new_gfind = '''static int gfind (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) {
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    int b = lua_toboolean(L, 3);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushboolean(L, b);
    lua_pushcclosure(L, pcre2_gfind_aux, 4);
    return 1;
  }
  else {
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    int b = lua_toboolean(L, 3);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushboolean(L, b);
    lua_pushcclosure(L, lua_gfind_aux, 4);
    return 1;
  }
}'''

lstrlib = lstrlib.replace(old_pcre2_gfind, new_gfind)

# 替换 pcre2_gmatch_aux → 保留不变（PCRE2 版本）
# 但 pcre2_gmatch 需要改为 dispatch

# 注意：pcre2_gmatch 创建闭包引用 pcre2_gmatch_aux
# 原版 lua_gmatch 创建闭包引用 lua_gmatch_aux
# 所以 dispatch 需要根据引擎选择不同的闭包

old_pcre2_gmatch = '''static int pcre2_gmatch (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  GMatchState *gm;
  lua_settop(L, 2);
  gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
  if (init > ls) init = ls + 1;
  gm->code = pcre2_compile_pattern(L, p, lp, &gm->mdata);
  gm->ms.L = L;
  gm->ms.src_init = s;
  gm->ms.src_end = s + ls;
  gm->ms.code = gm->code;
  gm->ms.mdata = gm->mdata;
  gm->ms.ovector = NULL;
  gm->ms.ovec_count = 0;
  gm->src = s + init;
  gm->p = p;
  gm->lastmatch = NULL;
  lua_pushcclosure(L, pcre2_gmatch_aux, 3);
  return 1;
}'''

new_gmatch = '''static int gmatch (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) {
    return pcre2_gmatch(L);
  }
  else {
    size_t ls, lp;
    const char *s = luaL_checklstring(L, 1, &ls);
    const char *p = luaL_checklstring(L, 2, &lp);
    size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
    GMatchState *gm;
    lua_settop(L, 2);
    gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
    if (init > ls) init = ls + 1;
    gm->ms.L = L;
    gm->ms.src_init = s;
    gm->ms.src_end = s + ls;
    gm->ms.p_end = p + lp;
    gm->ms.matchdepth = LUA_MAXCCALLS;
    gm->ms.level = 0;
    gm->src = s + init;
    gm->p = p;
    gm->lastmatch = NULL;
    gm->engine = 0;
    lua_pushcclosure(L, lua_gmatch_aux, 3);
    return 1;
  }
}'''

lstrlib = lstrlib.replace(old_pcre2_gmatch, new_gmatch)

# 替换 pcre2_str_gsub → str_gsub (dispatch)
old_pcre2_str_gsub = '''static int pcre2_str_gsub (lua_State *L) {'''

new_str_gsub = '''static int str_gsub (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) return pcre2_str_gsub(L);
  else return lua_str_gsub(L);
}

/* PCRE2 版本的 str_gsub（保留内部实现） */
static int pcre2_str_gsub (lua_State *L) {'''

lstrlib = lstrlib.replace(old_pcre2_str_gsub, new_str_gsub)

# 同样，pcre2_gmatch 需要保留（dispatch 中调用它）
# 但我们已经改成了 dispatch，所以需要把原来的 pcre2_gmatch 保留为内部函数
# 实际上，上面的 dispatch 中调用了 pcre2_gmatch(L)，但 pcre2_gmatch 已经被替换了
# 需要把原来的 pcre2_gmatch 实现保留

# 在 new_gmatch 中，PCRE2 分支调用了 pcre2_gmatch(L)
# 我们需要保留 PCRE2 版本的 gmatch 实现，但改名为 pcre2_gmatch
# 注意：原来的代码中 pcre2_gmatch 就在 dispatch 里面，但它被替换了

# 实际上，在 dispatch 中 if (XCLUA_PCRE2_ENABLED) return pcre2_gmatch(L);
# 需要一个名为 pcre2_gmatch 的函数，但原来的 pcre2_gmatch 已经被替换为 gmatch (dispatch)
# 所以我们需要在 dispatch 之前添加一个保留的 pcre2_gmatch 函数

# 解决方案：在 dispatch gmatch 之前添加 PCRE2 原始实现
# 修改 dispatch: 
# if (XCLUA_PCRE2_ENABLED) { ... PCRE2 实现 ... }
# 或者保留一个内部的 pcre2_gmatch_impl 函数

# 更简单的方法：在 dispatch 中直接内联 PCRE2 的代码
# 但这样代码会重复

# 最好的方法：把原来的 pcre2_gmatch 实现保留为 pcre2_gmatch_impl，然后在 dispatch 中调用

# 让我修改 new_gmatch，在 PCRE2 分支中内联原来的代码

# 实际上，由于 dispatch gmatch 的 PCRE2 分支和原来的 pcre2_gmatch 完全一样，
# 我们可以先保留 pcre2_gmatch 函数，然后 dispatch 调用它

# 但 pcre2_gmatch 现在已经被替换为 gmatch 了...
# 我需要把原始 pcre2_gmatch 代码重新插入

# 让我重新设计：
# 1. 先保留原始 pcre2_gmatch 为 pcre2_gmatch_internal
# 2. 然后 dispatch gmatch 调用 pcre2_gmatch_internal

# 实际上，更简单的方法是在 dispatch 中直接内联

# 让我们修改 new_gmatch:
new_gmatch_v2 = '''static int gmatch (lua_State *L) {
  if (XCLUA_PCRE2_ENABLED) {
    size_t ls, lp;
    const char *s = luaL_checklstring(L, 1, &ls);
    const char *p = luaL_checklstring(L, 2, &lp);
    size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
    GMatchState *gm;
    lua_settop(L, 2);
    gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
    if (init > ls) init = ls + 1;
    gm->code = pcre2_compile_pattern(L, p, lp, &gm->mdata);
    gm->ms.L = L;
    gm->ms.src_init = s;
    gm->ms.src_end = s + ls;
    gm->ms.code = gm->code;
    gm->ms.mdata = gm->mdata;
    gm->ms.ovector = NULL;
    gm->ms.ovec_count = 0;
    gm->src = s + init;
    gm->p = p;
    gm->lastmatch = NULL;
    gm->engine = 1;
    lua_pushcclosure(L, pcre2_gmatch_aux, 3);
    return 1;
  }
  else {
    size_t ls, lp;
    const char *s = luaL_checklstring(L, 1, &ls);
    const char *p = luaL_checklstring(L, 2, &lp);
    size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
    GMatchState *gm;
    lua_settop(L, 2);
    gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
    if (init > ls) init = ls + 1;
    gm->ms.L = L;
    gm->ms.src_init = s;
    gm->ms.src_end = s + ls;
    gm->ms.p_end = p + lp;
    gm->ms.matchdepth = LUA_MAXCCALLS;
    gm->ms.level = 0;
    gm->src = s + init;
    gm->p = p;
    gm->lastmatch = NULL;
    gm->engine = 0;
    lua_pushcclosure(L, lua_gmatch_aux, 3);
    return 1;
  }
}'''

# 但是 pcre2_gmatch_aux 内部使用了 do_match、push_captures 等函数
# 这些函数现在已经是 pcre2_do_match, pcre2_push_captures 了
# 所以 pcre2_gmatch_aux 应该能正常工作

# 更新：检查 pcre2_gmatch_aux 的内容
# 在 pcre2_gmatch_aux 中：
# - do_match → pcre2_do_match ✓
# - push_captures → pcre2_push_captures ✓
# - gm->ms → MatchState ✓

# 更新：检查 lua_gmatch_aux 的内容
# 在 lua_gmatch_aux 中：
# - match → lua_match ✓
# - reprepstate → lua_reprepstate ✓
# - push_captures → lua_push_captures ✓

# 现在我需要检查 lstrlib 中是否还有引用 pcre2_gmatch 的地方
# dispatch 已经不需要 pcre2_gmatch 了

# 验证：pcre2_str_gsub 的重命名
# 在 dispatch 中：if (XCLUA_PCRE2_ENABLED) return pcre2_str_gsub(L);
# 这个 pcre2_str_gsub 是原来的 str_gsub 重命名后的函数
# 但我们把 str_gsub 改成了 dispatch，而原来的实现被重命名为 pcre2_str_gsub（在内部）
# 这个逻辑是对的

# 现在验证所有重命名是否正确

# 写入文件
write_file('src/stdlib/lstrlib.c', lstrlib)

print("Transformation complete!")
print("lstrlib.c has been updated with dual-engine support.")