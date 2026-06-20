"""最简单的 LSP 服务器连通性测试"""
import subprocess, json, time, threading, os

proc = subprocess.Popen(
    [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=r'e:\Soft\Proje\LXCLUA-NCore\lua'
)

# 读取 stderr 的线程
def drain_stderr():
    while True:
        line = proc.stderr.readline()
        if not line:
            break
        print(f"  [SERVER] {line.decode('utf-8', errors='ignore').rstrip()}")

t = threading.Thread(target=drain_stderr, daemon=True)
t.start()
time.sleep(0.5)

# 发送 initialize
init_msg = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}}'
header = f"Content-Length: {len(init_msg)}\r\n\r\n"
print(f"-> Sending: {header}{init_msg}")
proc.stdin.write(header.encode('ascii'))
proc.stdin.write(init_msg.encode('utf-8'))
proc.stdin.flush()

# 读取响应 - 逐字节读取看看
print("<- Reading response...")
header_bytes = b''
while True:
    ch = proc.stdout.read(1)
    if not ch:
        print("  EOF on stdout!")
        break
    header_bytes += ch
    print(f"  Got byte: {ch!r}")
    if header_bytes.endswith(b'\r\n\r\n'):
        print(f"  Header done: {header_bytes!r}")
        break
    if len(header_bytes) > 200:
        print(f"  Header too long: {header_bytes[:200]!r}")
        break

# 解析 Content-Length
hdr_str = header_bytes.decode('ascii', errors='ignore')
cl = 0
for line in hdr_str.split('\r\n'):
    if line.lower().startswith('content-length:'):
        cl = int(line.split(':')[1].strip())

if cl > 0:
    body = proc.stdout.read(cl)
    print(f"<- Body ({cl} bytes): {body.decode('utf-8', errors='ignore')}")
else:
    print(f"  Invalid Content-Length: {cl}")

proc.kill()
proc.wait()
print("Done.")