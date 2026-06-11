"""Debug documentSymbol in isolation"""
import subprocess, json, sys, threading, time

def read_stderr(proc):
    while True:
        line = proc.stderr.readline()
        if not line:
            break
        print(f"[STDERR] {line.decode('utf-8', errors='ignore').rstrip()}")

def read_msg(proc):
    header = b''
    while True:
        ch = proc.stdout.read(1)
        if not ch:
            return None
        header += ch
        if header.endswith(b'\r\n\r\n') or header.endswith(b'\n\n'):
            break
    hdr_str = header.decode('ascii', errors='ignore')
    cl = 0
    for line in hdr_str.split('\r\n'):
        if line.lower().startswith('content-length:'):
            cl = int(line.split(':')[1].strip())
    if cl <= 0:
        return None
    body = proc.stdout.read(cl)
    try:
        return json.loads(body)
    except Exception as e:
        print(f"[PARSE ERROR] {e}")
        print(f"[RAW] {body[:500]}")
        return None

proc = subprocess.Popen(
    [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=r'e:\Soft\Proje\LXCLUA-NCore\lua'
)

# Start stderr thread
stderr_thread = threading.Thread(target=read_stderr, args=(proc,), daemon=True)
stderr_thread.start()

def send(msg):
    data = json.dumps(msg).encode('utf-8')
    header = f"Content-Length: {len(data)}\r\n\r\n".encode('ascii')
    proc.stdin.write(header + data)
    proc.stdin.flush()
    time.sleep(0.05)

# Init
send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}})
resp = read_msg(proc)
print(f"Init: {'OK' if resp else 'FAIL'}")

# Initialized
send({"jsonrpc":"2.0","method":"initialized","params":{}})

# didOpen with simple code
code = "local x=1\nlocal y=2"
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})
time.sleep(0.1)

# Test documentSymbol
print("\n--- Testing documentSymbol ---")
send({"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///t/m.lua"}}})
try:
    resp = read_msg(proc)
except Exception as e:
    print(f"read_msg exception: {e}")
    resp = None

print(f"documentSymbol response: {resp}")

if resp and "result" in resp:
    r = resp["result"]
    if isinstance(r, list):
        print(f"Result: {len(r)} symbols")
        for s in r:
            print(f"  - {s.get('name')} (kind={s.get('kind')})")
    else:
        print(f"Result type: {type(r).__name__}, value: {r}")
elif resp and "error" in resp:
    print(f"Error: {resp['error']}")

# Shutdown
time.sleep(0.2)
try:
    send({"jsonrpc":"2.0","id":99,"method":"shutdown","params":{}})
    read_msg(proc)
except:
    pass
try:
    send({"jsonrpc":"2.0","method":"exit","params":{}})
except:
    pass

try:
    proc.wait(timeout=5)
except:
    proc.kill()

print("\nDone.")