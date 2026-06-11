"""测试 semanticTokens"""
import subprocess, json, time, threading, sys, os

os.chdir(r'e:\Soft\Proje\LXCLUA-NCore\lua')

stderr_lines = []

def read_stderr(proc):
    for line in iter(proc.stderr.readline, b''):
        s = line.decode('utf-8', errors='ignore').rstrip()
        stderr_lines.append(s)
        print('[S]', s)

proc = subprocess.Popen(
    [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
)
t = threading.Thread(target=read_stderr, args=(proc,), daemon=True)
t.start()
time.sleep(0.5)

def send(msg, wait=True):
    data = json.dumps(msg).encode()
    hdr = f'Content-Length: {len(data)}\r\n\r\n'.encode()
    proc.stdin.write(hdr + data)
    proc.stdin.flush()
    if not wait:
        return
    time.sleep(0.3)
    # read response
    header = b''
    while True:
        ch = proc.stdout.read(1)
        if not ch:
            print("EOF reading header")
            return None
        header += ch
        if header.endswith(b'\r\n\r\n') or header.endswith(b'\n\n'):
            break
    hdr_str = header.decode('ascii', 'ignore')
    cl = 0
    for l in hdr_str.split('\r\n'):
        if l.lower().startswith('content-length:'):
            cl = int(l.split(':')[1].strip())
    if cl <= 0:
        return None
    body = proc.stdout.read(cl)
    try:
        return json.loads(body)
    except:
        return body.decode('utf-8', 'ignore')

# Init
resp = send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}})
print("Init:", "OK" if resp else "FAIL")

send({"jsonrpc":"2.0","method":"initialized","params":{}}, wait=False)
time.sleep(0.2)

# Open document
code = "local x=1\nfunction f() end\nlet y:int=2\n"
send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}}, wait=False)
time.sleep(0.3)

# First try a working method
resp = send({"jsonrpc":"2.0","id":10,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":1,"character":9}}})
print("Definition:", resp.get('result') if resp else "NULL")

# Now try semanticTokens
resp = send({"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///t/m.lua"}}})
print("SemanticTokens:", resp.get('result') if resp else "NULL")
if resp and 'error' in resp:
    print("  Error:", resp['error'])

# Shutdown
send({"jsonrpc":"2.0","id":99,"method":"shutdown","params":{}})
time.sleep(0.3)
proc.kill()
proc.wait(timeout=3)