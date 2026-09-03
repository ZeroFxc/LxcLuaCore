# -*- coding: utf-8 -*-
"""Patch lstrlib.c: pretty-shell output for string.dump envelop (CRLF-preserving)."""
import io, sys

def load(path):
    with io.open(path, 'r', encoding='utf-8-sig', newline='') as f:
        return f.read()

def save(path, text):
    with io.open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)

def patch(path, old, new):
    text = load(path)
    norm = text.replace('\r\n', '\n')
    if norm.count(old) != 1:
        print(f'[FAIL] {path}: anchor found {norm.count(old)} times')
        sys.exit(1)
    norm = norm.replace(old, new, 1)
    if '\r\n' in text:
        norm = norm.replace('\n', '\r\n')
    save(path, norm)
    print(f'[OK] {path}')

path = r'E:\Soft\Proje\LXCLUA-NCore\lua\src\stdlib\lstrlib.c'

# ---- insert pretty-writer after nirithy_encode ----
old_insert = """  out[out_len] = '\\0';
  return out;
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {"""

new_insert = """  out[out_len] = '\\0';
  return out;
}

/* ============================================================
 * Nirithy 壳排版：随机字符表情（框线/颜文字/制表符）+ 分行铺开
 * 装饰字符必须落在 base64 字母表（0-9 a-z A-Z - _）之外。
 * ============================================================ */
static const char* nirithy_emo[] = {
  "(◕‿◕)", "( ͡° ͜ʖ ͡°)", "(╯°□°)╯︵ ┻━┻",
  "☜(˚▽˚)☞", "(๑˃̵ᴗ˂̵)و", "(*≧ω≦*)", "( ˘ ³˘)♥",
  "(๑•̀ㅂ•́)و✧", "ヽ(•̀ω•́ )ゝ", "(￣▽￣)ノ", "ヽ(^◇^*)/",
  "( •̀ᴗ•́ )و", "(ᵔᴥᵔ)", "(づ￣ ³￣)づ", "\\\\(•̀ᴗ•́)/",
  "(°ω°)", "( •̀ω•́ )✧", "(´｡• ᵕ •｡`)", "(˶˃ ᵕ ˂˶)"
};
#define NIRITHY_EMO_N (int)(sizeof(nirithy_emo) / sizeof(nirithy_emo[0]))

/* 随机颜文字行 */
static void nirithy_emo_line(luaL_Buffer *b) {
  luaL_addstring(b, nirithy_emo[rand() % NIRITHY_EMO_N]);
  luaL_addchar(b, '\\n');
}

/* 随机框线分隔条 */
static void nirithy_divider(luaL_Buffer *b) {
  static const char *sets[4] = { "─", "═", "░", "┄" };
  const char *ch = sets[rand() % 4];
  int i, n = 14 + rand() % 22;
  for (i = 0; i < n; i++) luaL_addstring(b, ch);
  luaL_addchar(b, '\\n');
}

/* 头/尾装饰框：随机颜文字 + 框线 */
static void nirithy_banner(luaL_Buffer *b) {
  const char *emo = nirithy_emo[rand() % NIRITHY_EMO_N];
  size_t w = strlen(emo) + 6;
  size_t i;
  luaL_addstring(b, "╔");
  for (i = 0; i < w; i++) luaL_addstring(b, "═");
  luaL_addstring(b, "╗\\n");
  luaL_addstring(b, "║ ");
  luaL_addstring(b, emo);
  for (i = 0; i < w; i++) luaL_addchar(b, ' ');
  luaL_addstring(b, "║\\n");
  luaL_addstring(b, "╚");
  for (i = 0; i < w; i++) luaL_addstring(b, "═");
  luaL_addstring(b, "╝\\n");
}

/* 把纯 base64 流按格式铺开：随机长度分行、随机缩进（含制表符）、
   行间随机插入颜文字/框线分隔条，头尾各一个装饰框。 */
static void nirithy_write_pretty(luaL_Buffer *b, const char *b64, size_t b64len) {
  size_t pos = 0;
  nirithy_banner(b);
  while (pos < b64len) {
    size_t chunk = 8 + (size_t)(rand() % 28);
    size_t i;
    int indent = rand() % 5;
    if (chunk > b64len - pos) chunk = b64len - pos;
    for (i = 0; i < (size_t)indent; i++) luaL_addchar(b, ' ');
    if (rand() % 4 == 0) luaL_addchar(b, '\\t');
    luaL_addlstring(b, b64 + pos, chunk);
    luaL_addchar(b, '\\n');
    pos += chunk;
    if (pos < b64len) {
      int r = rand() % 6;
      if (r == 0) nirithy_emo_line(b);
      else if (r == 1) nirithy_divider(b);
    }
  }
  nirithy_banner(b);
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {"""

patch(path, old_insert, new_insert)

# ---- seed rng in aux_envelop ----
old_seed = """static void aux_envelop(lua_State *L, const char *s, size_t l) {
  uint64_t timestamp = (uint64_t)time(NULL);"""

new_seed = """static void aux_envelop(lua_State *L, const char *s, size_t l) {
  uint64_t timestamp = (uint64_t)time(NULL);
  srand((unsigned)timestamp ^ (unsigned)(size_t)s ^ (unsigned)l);  /* seed pretty-shell decoration */"""

patch(path, old_seed, new_seed)

# ---- pretty output in aux_envelop ----
old_out = """  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addstring(&b, "Nirithy==");
  luaL_addstring(&b, encoded);
  free(encoded);

  luaL_pushresult(&b);"""

new_out = """  luaL_Buffer b;
  luaL_buffinit(L, &b);
  luaL_addstring(&b, "Nirithy==");
  nirithy_write_pretty(&b, encoded, strlen(encoded));
  free(encoded);

  luaL_pushresult(&b);"""

patch(path, old_out, new_out)

print('done')
