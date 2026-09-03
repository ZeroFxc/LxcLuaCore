"""Minimal crash reproduction"""
import subprocess, json, time, os, sys, threading, queue

def send_msg(proc, msg):
    data = json.dumps(msg).encode('utf-8')
    header = f"Content-Length: {len(data)}\r\n\r\n".encode('ascii')
    proc.stdin.write(header + data)
    proc.stdin.flush()

def _bg_reader(stdout, out_q):
    try:
        while True:
            header = b''
            while not header.endswith(b'\r\n\r\n') and not header.endswith(b'\n\n'):
                ch = stdout.read(1)
                if not ch:
                    return
                header += ch
            hdr_str = header.decode('ascii', errors='ignore')
            cl = 0
            for line in hdr_str.replace('\r', '').split('\n'):
                if line.lower().startswith('content-length:'):
                    try: cl = int(line.split(':',1)[1].strip())
                    except: cl = 0
            if cl <= 0: continue
            body = stdout.read(cl)
            try:
                out_q.put(json.loads(body))
            except:
                pass
    except Exception:
        return

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'lxclua-lsp.exe')
proc = subprocess.Popen(
    [EXE],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    cwd=os.path.dirname(EXE), bufsize=0,
)
msg_q = queue.Queue()
t = threading.Thread(target=_bg_reader, args=(proc.stdout, msg_q), daemon=True)
t.start()

def read_next(timeout=5.0, expected_id=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            m = msg_q.get(timeout=0.05)
        except queue.Empty:
            continue
        if not isinstance(m, dict): continue
        if expected_id is not None:
            if m.get('id') == expected_id: return m
            continue
        return m
    return None

code = "local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n"

try:
    # 1. initialize
    send_msg(proc, {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","workspaceFolders":[{"uri":"file:///t","name":"t"}],"capabilities":{}}})
    r = read_next(timeout=10, expected_id=1)
    print("init:", "OK" if r else "TIMEOUT", r and r.get('id'))

    # 2. initialized (notif)
    send_msg(proc, {"jsonrpc":"2.0","method":"initialized","params":{}})
    time.sleep(0.1)

    # 3. didOpen
    send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})
    time.sleep(0.3)

    # 4. hover (request id 10) to check server alive
    send_msg(proc, {"jsonrpc":"2.0","id":10,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":4,"character":16}}})
    r10 = read_next(timeout=5, expected_id=10)
    print(f"[id=10 hover] alive? {r10 is not None}", r10 and r10.keys())

    # 5. prepareCallHierarchy id 11
    send_msg(proc, {"jsonrpc":"2.0","id":11,"method":"textDocument/prepareCallHierarchy","params":{"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":4,"character":16}}})
    r11 = read_next(timeout=5, expected_id=11)
    print(f"[id=11 prepareCallHierarchy] got? {r11 is not None} result_type: {type(r11.get('result')).__name__ if isinstance(r11,dict) and 'result' in r11 else 'N/A'}")
    if isinstance(r11, dict): print("  result =", json.dumps(r11.get('result'))[:400])

    time.sleep(0.2)
    # 6. ping id 12 to check alive
    send_msg(proc, {"jsonrpc":"2.0","id":12,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":1,"character":1}}})
    r12 = read_next(timeout=5, expected_id=12)
    print(f"[id=12 ping hover alive after callHierarchy] {r12 is not None}")
    if r12 is None:
        try: proc.kill()
        except: pass
        err = proc.stderr.read().decode('utf-8', errors='ignore')
        print("\n==== STDERR (last 6KB) ====\n")
        print(err[-6000:])
        sys.exit(1)

    # 7. prepareTypeHierarchy id 13
    send_msg(proc, {"jsonrpc":"2.0","id":13,"method":"textDocument/prepareTypeHierarchy","params":{"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":9,"character":7}}})
    r13 = read_next(timeout=5, expected_id=13)
    print(f"[id=13 prepareTypeHierarchy] got? {r13 is not None}")
    if r13 is None:
        try: proc.kill()
        except: pass
        err = proc.stderr.read().decode('utf-8', errors='ignore')
        print("\n==== STDERR (last 6KB) ====\n")
        print(err[-6000:])
        sys.exit(1)

    send_msg(proc, {"jsonrpc":"2.0","id":9999,"method":"shutdown","params":{}})
    sd = read_next(timeout=5, expected_id=9999)
    print("shutdown OK?", sd is not None)
    send_msg(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
    try:
        proc.wait(timeout=3)
    except:
        proc.kill()
    err = proc.stderr.read().decode('utf-8', errors='ignore')
    print(f"\nSTDERR last 3KB\n{err[-3000:]}")
except Exception as e:
    import traceback
    traceback.print_exc()
    try: proc.kill()
    except: pass
    err = proc.stderr.read().decode('utf-8', errors='ignore')
    print(f"\n==== STDERR ON EXCEPTION ({e}) - last 10KB ====\n")
    print(err[-10000:])
    sys.exit(1)
