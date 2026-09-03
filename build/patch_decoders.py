# -*- coding: utf-8 -*-
"""Apply Nirithy pretty-shell patches (CRLF-preserving)."""
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

# ============ 1) lauxlib.c decoder ============
old_la = '''static unsigned char* nirithy_decode(const char* input, size_t input_len, size_t* out_len) {
  size_t len;
  unsigned char* out;
  size_t i, j;

  if (input_len % 4 != 0) return NULL;
  len = input_len / 4 * 3;
  if (input_len > 0 && input[input_len - 1] == '=') len--;
  if (input_len > 1 && input[input_len - 2] == '=') len--;
  out = (unsigned char*)malloc(len);
  if (!out) return NULL;

  for (i = 0, j = 0; i < input_len; i += 4) {
    int a = input[i] == '=' ? 0 : nirithy_b64_val(input[i]);
    int b = input[i+1] == '=' ? 0 : nirithy_b64_val(input[i+1]);
    int c = input[i+2] == '=' ? 0 : nirithy_b64_val(input[i+2]);
    int d = input[i+3] == '=' ? 0 : nirithy_b64_val(input[i+3]);
    uint32_t triple;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free(out);
      return NULL;
    }
    triple = (uint32_t)((a << 18) + (b << 12) + (c << 6) + d);
    if (j < len) out[j++] = (triple >> 16) & 0xFF;
    if (j < len) out[j++] = (triple >> 8) & 0xFF;
    if (j < len) out[j++] = (triple) & 0xFF;
  }
  *out_len = len;
  return out;
}'''

new_la = '''static unsigned char* nirithy_decode(const char* input, size_t input_len, size_t* out_len) {
  char *buf;
  size_t clen, len, i, j;
  unsigned char *out;

  /* Nirithy 壳允许在载荷中穿插随机制表符/框线/颜文字排版装饰。
     解码前先把有效 base64 字符（含 '=' 填充）压实成纯流，跳过其余装饰字符。 */
  buf = (char*)malloc(input_len + 1);
  if (!buf) return NULL;
  clen = 0;
  for (i = 0; i < input_len; i++) {
    if (input[i] == '=' || nirithy_b64_val(input[i]) >= 0)
      buf[clen++] = input[i];
  }
  buf[clen] = '\\0';

  if (clen % 4 != 0) { free(buf); return NULL; }
  len = clen / 4 * 3;
  if (clen > 0 && buf[clen - 1] == '=') len--;
  if (clen > 1 && buf[clen - 2] == '=') len--;
  out = (unsigned char*)malloc(len);
  if (!out) { free(buf); return NULL; }

  for (i = 0, j = 0; i < clen; i += 4) {
    int a = buf[i] == '=' ? 0 : nirithy_b64_val(buf[i]);
    int b = buf[i+1] == '=' ? 0 : nirithy_b64_val(buf[i+1]);
    int c = buf[i+2] == '=' ? 0 : nirithy_b64_val(buf[i+2]);
    int d = buf[i+3] == '=' ? 0 : nirithy_b64_val(buf[i+3]);
    uint32_t triple;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free(buf);
      free(out);
      return NULL;
    }
    triple = (uint32_t)((a << 18) + (b << 12) + (c << 6) + d);
    if (j < len) out[j++] = (triple >> 16) & 0xFF;
    if (j < len) out[j++] = (triple >> 8) & 0xFF;
    if (j < len) out[j++] = (triple) & 0xFF;
  }
  free(buf);
  *out_len = len;
  return out;
}'''

patch(r'E:\Soft\Proje\LXCLUA-NCore\lua\src\core\lauxlib.c', old_la, new_la)

# ============ 2) llex.c decoder ============
old_ll = '''static unsigned char* nirithy_decode(const char* input, size_t input_len, size_t* out_len) {
  size_t len;
  unsigned char* out;
  size_t i, j;

  if (input_len % 4 != 0) return NULL;
  len = input_len / 4 * 3;
  if (input_len > 0 && input[input_len - 1] == '=') len--;
  if (input_len > 1 && input[input_len - 2] == '=') len--;
  out = (unsigned char*)malloc(len);
  if (!out) return NULL;

  for (i = 0, j = 0; i < input_len; i += 4) {
    int a = input[i] == '=' ? 0 : nirithy_b64_val(input[i]);
    int b = input[i+1] == '=' ? 0 : nirithy_b64_val(input[i+1]);
    int c = input[i+2] == '=' ? 0 : nirithy_b64_val(input[i+2]);
    int d = input[i+3] == '=' ? 0 : nirithy_b64_val(input[i+3]);
    uint32_t triple;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free(out);
      return NULL;
    }
    triple = (uint32_t)((a << 18) + (b << 12) + (c << 6) + d);
    if (j < len) out[j++] = (triple >> 16) & 0xFF;
    if (j < len) out[j++] = (triple >> 8) & 0xFF;
    if (j < len) out[j++] = (triple) & 0xFF;
  }
  *out_len = len;
  return out;
}'''

new_ll = '''static unsigned char* nirithy_decode(const char* input, size_t input_len, size_t* out_len) {
  char *buf;
  size_t clen, len, i, j;
  unsigned char *out;

  /* Nirithy 壳允许在载荷中穿插随机制表符/框线/颜文字排版装饰。
     解码前先把有效 base64 字符（含 '=' 填充）压实成纯流，跳过其余装饰字符。 */
  buf = (char*)malloc(input_len + 1);
  if (!buf) return NULL;
  clen = 0;
  for (i = 0; i < input_len; i++) {
    if (input[i] == '=' || nirithy_b64_val(input[i]) >= 0)
      buf[clen++] = input[i];
  }
  buf[clen] = '\\0';

  if (clen % 4 != 0) { free(buf); return NULL; }
  len = clen / 4 * 3;
  if (clen > 0 && buf[clen - 1] == '=') len--;
  if (clen > 1 && buf[clen - 2] == '=') len--;
  out = (unsigned char*)malloc(len);
  if (!out) { free(buf); return NULL; }

  for (i = 0, j = 0; i < clen; i += 4) {
    int a = buf[i] == '=' ? 0 : nirithy_b64_val(buf[i]);
    int b = buf[i+1] == '=' ? 0 : nirithy_b64_val(buf[i+1]);
    int c = buf[i+2] == '=' ? 0 : nirithy_b64_val(buf[i+2]);
    int d = buf[i+3] == '=' ? 0 : nirithy_b64_val(buf[i+3]);
    uint32_t triple;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free(buf);
      free(out);
      return NULL;
    }
    triple = (uint32_t)((a << 18) + (b << 12) + (c << 6) + d);
    if (j < len) out[j++] = (triple >> 16) & 0xFF;
    if (j < len) out[j++] = (triple >> 8) & 0xFF;
    if (j < len) out[j++] = (triple) & 0xFF;
  }
  free(buf);
  *out_len = len;
  return out;
}'''

patch(r'E:\Soft\Proje\LXCLUA-NCore\lua\src\compiler\llex.c', old_ll, new_ll)

print('done')
