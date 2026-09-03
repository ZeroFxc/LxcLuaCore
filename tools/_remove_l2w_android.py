# -*- coding: utf-8 -*-
import io
path = r"E:\Soft\Proje\LXCLUA-NCore\lua\Android.mk"
with io.open(path, "r", encoding="utf-8") as f:
    lines = f.readlines()
out = []
removed = 0
for ln in lines:
    s = ln.strip()
    if s.startswith("src/lua2wasm/"):
        removed += 1
        continue
    out.append(ln)
# fix LOCAL_CFLAGS line: remove the -I$(LOCAL_PATH)/src/lua2wasm include
out = [ln.replace(" -I$(LOCAL_PATH)/src/lua2wasm", "") for ln in out]
with io.open(path, "w", encoding="utf-8", newline="") as f:
    f.writelines(out)
print("removed source lines:", removed)
