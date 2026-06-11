import subprocess, json, time, sys

def read_msg(proc):
    """Read an LSP message from the process stdout"""
    header = b''
    while not header.endswith(b'\r\n\r\n') and not header.endswith(b'\n\n'):
        ch = proc.stdout.read(1)
        if not ch:
            return None
        header += ch
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

proc = subprocess.Popen(
    [r'e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp.exe'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=r'e:\Soft\Proje\LXCLUA-NCore\lua'
)

code = "local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n"

# Init
send_msg(proc, {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":1,"rootUri":"file:///t","capabilities":{}}})
resp = read_msg(proc)
print("Init:", "OK" if resp else "FAIL")

# Initialized
send_msg(proc, {"jsonrpc":"2.0","method":"initialized","params":{}})

# didOpen
send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})

# Test each method
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
    ("completionResolve", "completionItem/resolve", {"label":"print","kind":3,"detail":"print(...)","documentation":"Prints values to stdout","sortText":"99999_print"}),
    ("linkedEditing", "textDocument/linkedEditingRange", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":6}}),
]

for i, (name, method, params) in enumerate(tests):
    tid = i + 2
    send_msg(proc, {"jsonrpc":"2.0","id":tid,"method":method,"params":params})
    resp = read_msg(proc)
    status = "OK" if resp else "FAIL (null)"
    if resp and "result" in resp:
        result = resp["result"]
        if isinstance(result, dict) and "items" in result:
            status = f"OK ({len(result['items'])} items)"
        elif isinstance(result, list):
            status = f"OK ({len(result)} entries)"
        elif isinstance(result, dict) and "data" in result:
            data = result["data"]
            status = f"OK (data: {len(str(data))} chars)"
        else:
            status = f"OK ({type(result).__name__})"
    elif resp and "error" in resp:
        status = f"ERR: {resp['error']}"
    print(f"  [{tid}] {name}: {status}")
    time.sleep(0.05)

# Shutdown
send_msg(proc, {"jsonrpc":"2.0","id":99,"method":"shutdown","params":{}})
read_msg(proc)
send_msg(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
proc.wait(timeout=3)
stderr = proc.stderr.read().decode('utf-8', errors='ignore')
if stderr:
    print(f"\n=== Stderr ===\n{stderr[:500]}")
print("\nDone.")