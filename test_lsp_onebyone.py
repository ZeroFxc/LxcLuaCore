"""逐个 LSP 方法测试 - 捕获 stderr"""
import subprocess, json, sys, threading, time, os

# 用线程读取 stderr 避免死锁
stderr_lines = []
def read_stderr(proc):
    for line in iter(proc.stderr.readline, b''):
        stderr_lines.append(line.decode('utf-8', errors='ignore'))

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
    except:
        return body.decode('utf-8', errors='ignore')

def send_msg(proc, msg):
    data = json.dumps(msg).encode('utf-8')
    header = f"Content-Length: {len(data)}\r\n\r\n".encode('ascii')
    try:
        proc.stdin.write(header + data)
        proc.stdin.flush()
        return True
    except:
        return False

def run_server():
    return subprocess.Popen(
        [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=r'e:\Soft\Proje\LXCLUA-NCore\lua'
    )

def init_server(proc):
    send_msg(proc, {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}})
    resp = read_msg(proc)
    print(f"  Init: {'OK' if resp else 'FAIL'}")
    send_msg(proc, {"jsonrpc":"2.0","method":"initialized","params":{}})

code = "local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n"

def test_one(name, method, params):
    global stderr_lines
    stderr_lines = []
    proc = run_server()
    t = threading.Thread(target=read_stderr, args=(proc,), daemon=True)
    t.start()
    time.sleep(0.2)
    
    init_server(proc)
    send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})
    time.sleep(0.1)
    
    ok = send_msg(proc, {"jsonrpc":"2.0","id":2,"method":method,"params":params})
    if not ok:
        print(f"  [{name}] FAIL: cannot send (server dead)")
        proc.kill()
        return
    
    resp = read_msg(proc)
    
    send_msg(proc, {"jsonrpc":"2.0","id":99,"method":"shutdown","params":{}})
    read_msg(proc)
    send_msg(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
    proc.wait(timeout=3)
    time.sleep(0.2)
    
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
        elif "error" in resp:
            status = f"ERR: {resp['error']}"
    
    print(f"  [{name:25s}] {status}")
    if stderr_lines:
        for line in stderr_lines:
            print(f"    [SERVER] {line.rstrip()}")
    return resp and "result" in resp

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
    # 新增高级功能测试
    ("declaration", "textDocument/declaration", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":1,"character":7}}),
    ("codeLens", "textDocument/codeLens", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("documentLink", "textDocument/documentLink", {"textDocument":{"uri":"file:///t/m.lua"}}),
    ("inlayHint", "textDocument/inlayHint", {"textDocument":{"uri":"file:///t/m.lua"},"range":{"start":{"line":0,"character":0},"end":{"line":9,"character":0}}}),
    ("prepareCallHierarchy", "textDocument/prepareCallHierarchy", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":4,"character":15}}),
    ("typeHierarchy", "textDocument/prepareTypeHierarchy", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":7,"character":7}}),
    ("colorPresentation", "textDocument/colorPresentation", {"textDocument":{"uri":"file:///t/m.lua"},"color":{"red":1.0,"green":0.0,"blue":0.0,"alpha":1.0},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}}}),
    ("moniker", "textDocument/moniker", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6}}),
    ("rangeFormatting", "textDocument/rangeFormatting", {"textDocument":{"uri":"file:///t/m.lua"},"range":{"start":{"line":0,"character":0},"end":{"line":3,"character":0}},"options":{"tabSize":4,"insertSpaces":True}}),
    ("onTypeFormatting", "textDocument/onTypeFormatting", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":5},"ch":"\n"}),
    # 新增补全的 dispatch 方法
    ("semanticTokensRange", "textDocument/semanticTokens/range", {"textDocument":{"uri":"file:///t/m.lua"},"range":{"start":{"line":0,"character":0},"end":{"line":9,"character":0}}}),
    ("semanticTokensDelta", "textDocument/semanticTokens/full/delta", {"textDocument":{"uri":"file:///t/m.lua"},"previousResultId":"1"}),
    ("documentLinkResolve", "documentLink/resolve", {"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"target":"http://example.com"}),
    ("inlayHintResolve", "inlayHint/resolve", {"position":{"line":0,"character":0},"label":":int"}),
    ("executeCommand", "workspace/executeCommand", {"command":"test.command","arguments":[]}),
]

print("=== Individual LSP Method Tests ===\n")
passed = 0
failed = 0
for name, method, params in tests:
    try:
        ok = test_one(name, method, params)
        if ok:
            passed += 1
        else:
            failed += 1
    except Exception as e:
        print(f"  [{name:25s}] CRASH: {e}")
        failed += 1

print(f"\n=== Results: {passed} passed, {failed} failed ===")