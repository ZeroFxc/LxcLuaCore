# -*- coding: utf-8 -*-
"""Verify Nirithy pretty-shell integrity (pure Python, no crypto libs)."""
import io, struct, time

B64 = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_"

def b64val(c):
    return B64.index(c) if c in B64 else -1

def nirithy_decode(s):
    # compact: keep only valid base64 chars and '='
    buf = [c for c in s if c == '=' or b64val(c) >= 0]
    clen = len(buf)
    assert clen % 4 == 0, f'compacted length {clen} not multiple of 4'
    L = clen // 4 * 3
    if buf and buf[-1] == '=':
        L -= 1
    if len(buf) > 1 and buf[-2] == '=':
        L -= 1
    out = bytearray()
    for i in range(0, clen, 4):
        a = 0 if buf[i] == '=' else b64val(buf[i])
        b = 0 if buf[i+1] == '=' else b64val(buf[i+1])
        c = 0 if buf[i+2] == '=' else b64val(buf[i+2])
        d = 0 if buf[i+3] == '=' else b64val(buf[i+3])
        assert a >= 0 and b >= 0 and c >= 0 and d >= 0
        t = (a << 18) + (b << 12) + (c << 6) + d
        if len(out) < L:
            out.append((t >> 16) & 0xFF)
        if len(out) < L:
            out.append((t >> 8) & 0xFF)
        if len(out) < L:
            out.append(t & 0xFF)
    return bytes(out)

def nirithy_encode(data):
    out = []
    for i in range(0, len(data), 3):
        a = data[i] if i < len(data) else 0
        b = data[i+1] if i+1 < len(data) else 0
        c = data[i+2] if i+2 < len(data) else 0
        t = (a << 16) + (b << 8) + c
        out.append(B64[(t >> 18) & 0x3F])
        out.append(B64[(t >> 12) & 0x3F])
        out.append(B64[(t >> 6) & 0x3F])
        out.append(B64[t & 0x3F])
    res = ''.join(out)
    if len(data) % 3 == 1:
        res = res[:-2] + '=='
    elif len(data) % 3 == 2:
        res = res[:-1] + '='
    return res

base = r'E:\Soft\Proje\LXCLUA-NCore\lua\build'
shell = io.open(base + r'\test_fib.shell', encoding='utf-8').read()
luac = open(base + r'\test_fib.luac', 'rb').read()

assert shell.startswith('Nirithy=='), 'signature missing'
payload_text = shell[9:]  # everything after signature

# compact the decorated payload exactly as the new C decoder does
compacted = ''.join(c for c in payload_text if c == '=' or b64val(c) >= 0)

# decoded binary payload
decoded = nirithy_decode(payload_text)

print('shell size        :', len(shell.encode('utf-8')))
print('raw payload len   :', len(payload_text))
print('compacted b64 len :', len(compacted))
print('decoded payload   :', len(decoded), 'bytes')
print('expect 24+luac    :', 24 + len(luac))
print('payload length OK :', len(decoded) == 24 + len(luac))

# re-encode decoded -> should equal compacted (lossless decoration)
reenc = nirithy_encode(decoded)
print('compact == re-encode :', compacted == reenc)

# timestamp sanity
ts = struct.unpack('<Q', decoded[:8])[0]
print('timestamp          :', ts, '->', time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts)))
print('timestamp sane     :', abs(time.time() - ts) < 7 * 86400)

# show a summary of decoration content (non-base64 chars)
import collections
deco = collections.Counter(c for c in payload_text if not (c == '=' or b64val(c) >= 0))
print('decoration chars   :', dict(sorted(deco.items(), key=lambda x: -x[1])[:8]))

print('\nRESULT:', 'PASS' if (len(decoded) == 24 + len(luac) and compacted == reenc and abs(time.time()-ts) < 7*86400) else 'FAIL')
