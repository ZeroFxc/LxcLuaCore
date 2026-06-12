import os, glob
basedir = r"e:\Soft\Proje\LXCLUA-NCore\lua"
for f in glob.glob(os.path.join(basedir, "build", "obj", "lspsrv_*.o")):
    os.remove(f)
    print(f"删除: {os.path.basename(f)}")
exe = os.path.join(basedir, "lxclua-lsp.exe")
if os.path.exists(exe):
    os.remove(exe)
    print(f"删除: lxclua-lsp.exe")
print("清理完成，开始编译...")