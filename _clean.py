import os
basedir = r"e:\Soft\Proje\LXCLUA-NCore\lua"
for f in [os.path.join(basedir, "_rebuild.py"), os.path.join(basedir, "_clean_lsp.py")]:
    if os.path.exists(f):
        os.remove(f)
print("清理完成")