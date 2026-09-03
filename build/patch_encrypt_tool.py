# -*- coding: utf-8 -*-
"""Patch encrypt_bytecode.c: pretty-shell output (CRLF-preserving)."""
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

path = r'E:\Soft\Proje\LXCLUA-NCore\lua\src\utils\encrypt_bytecode.c'

# ---- insert pretty-writer after nirithy_encode ----
old_insert = """    out[out_len] = '\\0';
    return out;
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {"""

new_insert = """    out[out_len] = '\\0';
    return out;
}

/* ============================================================
 * Nirithy 壳排版：随机字符表情（框线/颜文字/制表符）+ 分行铺开
 * 注意：base64 字母表含 0-9 a-z A-Z - _，装饰字符必须全部落在
 * 字母数字之外，否则会被解码端当成数据而破坏载荷。
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
static void nirithy_emo_line(FILE *f) {
  fputs(nirithy_emo[rand() % NIRITHY_EMO_N], f);
  fputc('\\n', f);
}

/* 随机框线分隔条 */
static void nirithy_divider(FILE *f) {
  static const char *sets[4] = { "─", "═", "░", "┄" };
  const char *ch = sets[rand() % 4];
  int i, n = 14 + rand() % 22;
  for (i = 0; i < n; i++) fputs(ch, f);
  fputc('\\n', f);
}

/* 头/尾装饰框：随机颜文字 + 框线 */
static void nirithy_banner(FILE *f) {
  const char *emo = nirithy_emo[rand() % NIRITHY_EMO_N];
  size_t w = strlen(emo) + 6;
  size_t i;
  fputs("╔", f);
  for (i = 0; i < w; i++) fputs("═", f);
  fputs("╗\\n", f);
  fputs("║ ", f);
  fputs(emo, f);
  for (i = 0; i < w; i++) fputc(' ', f);
  fputs("║\\n", f);
  fputs("╚", f);
  for (i = 0; i < w; i++) fputs("═", f);
  fputs("╝\\n", f);
}

/* 把纯 base64 流按格式铺开：随机长度分行、随机缩进（含制表符）、
   行间随机插入颜文字/框线分隔条，头尾各一个装饰框。 */
static void nirithy_write_pretty(FILE *f, const char *b64, size_t b64len) {
  size_t pos = 0;
  nirithy_banner(f);
  while (pos < b64len) {
    size_t chunk = 8 + (size_t)(rand() % 28);
    size_t i;
    int indent = rand() % 5;
    if (chunk > b64len - pos) chunk = b64len - pos;
    for (i = 0; i < (size_t)indent; i++) fputc(' ', f);
    if (rand() % 4 == 0) fputc('\\t', f);
    fwrite(b64 + pos, 1, chunk, f);
    fputc('\\n', f);
    pos += chunk;
    if (pos < b64len) {
      int r = rand() % 6;
      if (r == 0) nirithy_emo_line(f);
      else if (r == 1) nirithy_divider(f);
    }
  }
  nirithy_banner(f);
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {"""

patch(path, old_insert, new_insert)

# ---- seed rng before timestamp ----
old_seed = """    // Prepare encryption
    uint64_t timestamp = (uint64_t)time(NULL);"""

new_seed = """    // Prepare encryption
    srand((unsigned)time(NULL));  /* seed pretty-shell random decoration */
    uint64_t timestamp = (uint64_t)time(NULL);"""

patch(path, old_seed, new_seed)

# ---- pretty output ----
old_out = """    fwrite("Nirithy==", 1, 9, f);
    fwrite(b64, 1, strlen(b64), f);
    fclose(f);"""

new_out = """    fwrite("Nirithy==", 1, 9, f);
    nirithy_write_pretty(f, b64, strlen(b64));
    fclose(f);"""

patch(path, old_out, new_out)

print('done')
