"""LSP 诊断测试 - 修复 stderr 死锁"""
import subprocess, json, sys, threading, time, os

# 用线程读取 stderr 避免死锁
def read_stderr(proc):
    lines = []
    for line in iter(proc.stderr.readline, b''):
        lines.append(line.decode('utf-8', errors='ignore'))
    return lines

def read_msg(proc):
    """读取 LSP 消息"""
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
    except:
        return body.decode('utf-8', errors='ignore')

def send_msg(proc, msg):
    data = json.dumps(msg).encode('utf-8')
    header = f"Content-Length: {len(data)}\r\n\r\n".encode('ascii')
    proc.stdin.write(header + data)
    proc.stdin.flush()

# 启动 LSP 服务器
proc = subprocess.Popen(
    [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=r'e:\Soft\Proje\LXCLUA-NCore\lua'
)

# 启动 stderr 读取线程
stderr_lines = []
stderr_thread = threading.Thread(target=lambda: stderr_lines.extend(read_stderr(proc)), daemon=True)
stderr_thread.start()

# 等服务器启动
time.sleep(0.3)

code = "local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n"

print("--- Step 1: Initialize ---")
send_msg(proc, {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}})
resp = read_msg(proc)
if resp:
    print(f"  Init OK: id={resp.get('id')}, has_result={'result' in resp}")
    caps = resp.get('result', {}).get('capabilities', {})
    print(f"  Capabilities: {list(caps.keys())}")
else:
    print("  Init FAIL: NULL response")

print("--- Step 2: Initialized ---")
send_msg(proc, {"jsonrpc":"2.0","method":"initialized","params":{}})

print("--- Step 3: didOpen ---")
send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})
time.sleep(0.2)

print("--- Step 4: Test Methods ---")
tests = [
    ("completion", "textDocument/completion", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":2}}),
    ("documentSymbol", "textDocument/documentSymbol", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("foldingRange", "textDocument/foldingRange", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("semanticTokens", "textDocument/semanticTokens/full", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("codeAction", "textDocument/codeAction", {"textDocument":{"uri":"file:///t/m.lua"},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},"context":{"diagnostics":[]}}),
    ("formatting", "textDocument/formatting", {"textDocument":{"uri":"file:///t/m.lua"},"options":{"tabSize":4,"insertSpaces":True}}),
    ("diagnostic", "textDocument/diagnostic", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("hover", "textDocument/hover", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":2,"character":4}}),
    ("definition", "textDocument/definition", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":2,"character":4}}),
    ("references", "textDocument/references", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6},"context":{"includeDeclaration":True}}),
    ("rename", "textDocument/rename", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6},"newName":"renamed_x"}),
    ("signatureHelp", "textDocument/signatureHelp", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":10}}),
    ("documentHighlight", "textDocument/documentHighlight", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6}}),
    ("prepareRename", "textDocument/prepareRename", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6}}),
    ("typeDefinition", "textDocument/typeDefinition", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":1,"character":7}}),
    ("implementation", "textDocument/implementation", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":4,"character":15}}),
    ("workspaceSymbol", "workspace/symbol", {"query":"Point"}),
    ("selectionRange", "textDocument/selectionRange", {"textDocument":{"uri":"file:///t/m.lua"},"positions":[{"line":5,"character":2}]}),
    ("completionResolve", "completionItem/resolve", {"label":"print","kind":3}),
    ("linkedEditing", "textDocument/linkedEditingRange", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6}}),
]

passed = 0
failed = 0
for i, (name, method, params) in enumerate(tests):
    tid = i + 2
    send_msg(proc, {"jsonrpc":"2.0","id":tid,"method":method,"params":params})
    resp = read_msg(proc)
    status = "FAIL"
    if resp:
        if "result" in resp:
            result = resp["result"]
            if isinstance(result, dict) and "items" in result:
                status = f"OK ({len(result['items'])} items)"
            elif isinstance(result, list):
                status = f"OK ({len(result)} entries)"
            elif isinstance(result, dict) and "data" in result:
                data = result["data"]
                status = f"OK (data: {len(str(data))} chars)"
            elif result is None:
                status = "OK (null)"
            else:
                status = f"OK ({type(result).__name__})"
            passed += 1
        elif "error" in resp:
            status = f"ERR: {resp['error']}"
            failed += 1
    else:
        failed += 1
    print(f"  [{tid:2d}] {name:25s}: {status}")

print(f"\n--- Results: {passed} passed, {failed} failed ---")

# 通知测试
print("--- Step 5: Notifications ---")
send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"file:///t/m.lua"}}})
print("  didSave: sent (notification)")
send_msg(proc, {"jsonrpc":"2.0","method":"workspace/didChangeConfiguration","params":{"settings":{}}})
print("  workspace/didChangeConfiguration: sent (notification)")

# 关闭
print("--- Step 6: Shutdown ---")
send_msg(proc, {"jsonrpc":"2.0","id":99,"method":"shutdown","params":{}})
resp = read_msg(proc)
print(f"  Shutdown: {'OK' if resp else 'FAIL'}")
send_msg(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
proc.wait(timeout=3)

# 打印 stderr 日志
time.sleep(0.5)
if stderr_lines:
    print(f"\n=== Stderr Logs ({len(stderr_lines)} lines) ===")
    for line in stderr_lines[:30]:
        print(f"  {line.rstrip()}")

print("\nDone.")