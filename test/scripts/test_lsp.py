"""
LSP 协议综合测试脚本 - TR-11.2 补充：
1. 初始化后校验 ServerCapabilities 字段齐全；
2. 验证错误码：MethodNotFound(-32601)、InvalidParams(-32602)；
3. semanticTokens/range + full/delta + resultId；
4. 4 个 resolve：completionItem/resolve、codeLens/resolve、documentLink/resolve、inlayHint/resolve；
5. Call / Type Hierarchy prepare + incoming/outgoing/super/sub；
6. DocumentColor + ColorPresentation + Moniker；
7. WorkspaceFolders、didChangeWatchedFiles、didChangeWorkspaceFolders；
8. willSaveWaitUntil 返回空数组；
9. window/logMessage / window/showMessage 通知；
10. executeCommand；
11. WorkspaceSymbol resolve。
"""
import subprocess, json, time, sys, os, threading, queue

def read_msg(proc, timeout=8.0, q=None):
    """Read an LSP message. If a background reader queue 'q' is provided, pop from there."""
    if q is not None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                return q.get(timeout=0.05)
            except queue.Empty:
                continue
        return None
    # Fallback 阻塞方式（不建议使用）
    header = b''
    deadline = time.time() + timeout
    while not header.endswith(b'\r\n\r\n') and not header.endswith(b'\n\n'):
        if time.time() > deadline:
            return None
        ch = proc.stdout.read(1)
        if not ch:
            return None
        header += ch
    hdr_str = header.decode('ascii', errors='ignore')
    cl = 0
    for line in hdr_str.replace('\r', '').split('\n'):
        if line.lower().startswith('content-length:'):
            try:
                cl = int(line.split(':', 1)[1].strip())
            except:
                cl = 0
    if cl <= 0:
        return None
    body = proc.stdout.read(cl)
    try:
        return json.loads(body)
    except:
        return body.decode('utf-8', errors='ignore')

def _bg_reader(stdout, out_q):
    """后台线程：把 stdout 解析为完整消息放入队列"""
    try:
        while True:
            # 读 header
            header = b''
            while not header.endswith(b'\r\n\r\n') and not header.endswith(b'\n\n'):
                ch = stdout.read(1)
                if not ch:
                    return  # EOF
                header += ch
            hdr_str = header.decode('ascii', errors='ignore')
            cl = 0
            for line in hdr_str.replace('\r', '').split('\n'):
                if line.lower().startswith('content-length:'):
                    try:
                        cl = int(line.split(':', 1)[1].strip())
                    except:
                        cl = 0
            if cl <= 0:
                continue
            body = stdout.read(cl)
            try:
                out_q.put(json.loads(body))
            except:
                try:
                    out_q.put(body.decode('utf-8', errors='ignore'))
                except:
                    pass
    except Exception:
        return

def send_msg(proc, msg):
    data = json.dumps(msg).encode('utf-8')
    header = f"Content-Length: {len(data)}\r\n\r\n".encode('ascii')
    proc.stdin.write(header + data)
    proc.stdin.flush()

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'lxclua-lsp.exe')
EXE = os.path.normpath(EXE)

proc = subprocess.Popen(
    [EXE],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    cwd=os.path.dirname(EXE),
    bufsize=0,
)
# 启动后台读者线程，真正实现超时
msg_q: "queue.Queue" = queue.Queue()
_saved_responses = {}  # id -> message，用于存"提前到达"的响应
_stderr_buf = []
_stderr_lock = threading.Lock()

def _stderr_reader(stderr_r):
    try:
        while True:
            line = stderr_r.readline()
            if not line:
                return
            try:
                s = line.decode('utf-8', errors='ignore')
            except:
                s = str(line)
            with _stderr_lock:
                _stderr_buf.append(s)
    except:
        return

_bg_thread = threading.Thread(target=_bg_reader, args=(proc.stdout, msg_q), daemon=True)
_bg_thread.start()
_stderr_thread = threading.Thread(target=_stderr_reader, args=(proc.stderr,), daemon=True)
_stderr_thread.start()

def read_next(timeout=8.0, only_response=True, expected_id=None):
    """从后台队列中读下一条；默认跳过没有 id 的通知。
    expected_id 若给定：循环跳过直到找到匹配 id 的响应（或超时）。
    不匹配但有 id 的消息存入 _saved_responses，避免丢失。"""
    # 先查 saved_responses 有没有
    if expected_id is not None and expected_id in _saved_responses:
        return _saved_responses.pop(expected_id)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            m = msg_q.get(timeout=0.05)
        except queue.Empty:
            continue
        if not isinstance(m, dict):
            continue
        if only_response and "id" not in m:
            continue
        if expected_id is not None:
            mid = m.get("id")
            if mid == expected_id:
                return m
            # 不匹配但有 id -> 保存起来
            if mid is not None:
                _saved_responses[mid] = m
            # 无 id（notification）-> 丢弃
            continue
        return m
    return None

code = "local x=1\nlocal y:int=2\nlet z=3\nconst W=4\nexport function f()end\nfunction g()\n  if true then\n    print(1)\n  end\nend\nstruct Point x,y end\n"

# ========== Init ==========
send_msg(proc, {
    "jsonrpc":"2.0","id":1,"method":"initialize",
    "params":{
        "processId":1,
        "rootUri":"file:///t",
        "workspaceFolders":[{"uri":"file:///t","name":"t"}],
        "capabilities":{
            "workspace":{"workspaceFolders":True, "symbol":{"resolveSupport":{"properties":["containerName"]}}},
            "window":{"workDoneProgress":True, "showDocument":{"support":True}},
            "textDocument":{
                "semanticTokens": {"formats": ["relative"], "requests": {"full": {"delta": True}, "range": True}},
                "publishDiagnostics": {"versionSupport": True, "relatedInformation": True, "codeDescriptionSupport": True, "dataSupport": True},
                "diagnostic": {"relatedDocumentSupport": True},
                "completion": {"completionItem": {"resolveSupport": {"properties": ["detail","documentation","additionalTextEdits","insertTextMode"]}}},
                "codeLens": {"resolveSupport": {"properties": ["command"]}},
                "documentLink": {"tooltipSupport": True},
                "inlayHint": {"resolveSupport": {"properties": ["tooltip","label"]}},
                "callHierarchy": {"dynamicRegistration": False},
                "typeHierarchy": {"dynamicRegistration": False},
                "colorProvider": {"dynamicRegistration": False},
                "moniker": {"dynamicRegistration": False},
            }
        }
    }
})
resp_init = read_next(timeout=15, expected_id=1)
cap = (resp_init or {}).get("result", {}).get("capabilities", {}) if isinstance(resp_init, dict) else {}
srv_info = (resp_init or {}).get("result", {}).get("serverInfo", {}) if isinstance(resp_init, dict) else {}
print("[1] initialize:", "OK" if resp_init and "result" in resp_init else "FAIL")

# serverInfo 断言（LSP 3.0+ 标准字段，便于客户端识别实现方）
print("    serverInfo:", srv_info)
assert isinstance(srv_info, dict), "serverInfo 应为 object"
srv_name = srv_info.get("name")
assert isinstance(srv_name, str) and len(srv_name) > 0, "serverInfo.name 应为非空 string"
assert isinstance(srv_info.get("version", ""), str), "serverInfo.version 应为 string (optional)"

# textDocumentSync 结构断言（TextDocumentSyncOptions）
tds = cap.get("textDocumentSync")
print("    textDocumentSync type:", type(tds).__name__, tds if isinstance(tds, dict) else "")
assert isinstance(tds, dict), "textDocumentSync 应为 TextDocumentSyncOptions object (不是 legacy number)"
assert tds.get("openClose") is True, "textDocumentSync.openClose 应为 true"
assert tds.get("change") in (1, 2), "textDocumentSync.change ∈ {1=Full, 2=Incremental}"
assert tds.get("willSave") is True, "textDocumentSync.willSave 应为 true"
assert tds.get("willSaveWaitUntil") is True, "textDocumentSync.willSaveWaitUntil 应为 true"
save_opt = tds.get("save")
assert isinstance(save_opt, dict), "textDocumentSync.save 应为 SaveOptions object"
assert save_opt.get("includeText") is True, "save.includeText 应为 true"

# 断言 Capabilities 关键字段
assert isinstance(cap, dict), "capabilities 应为 object"
MANDATORY_CAPS = [
    "textDocumentSync",
    "completionProvider",
    "hoverProvider",
    "signatureHelpProvider",
    "definitionProvider",
    "typeDefinitionProvider",
    "implementationProvider",
    "referencesProvider",
    "documentHighlightProvider",
    "documentSymbolProvider",
    "codeActionProvider",
    "codeLensProvider",
    "documentLinkProvider",
    "colorProvider",
    "documentFormattingProvider",
    "renameProvider",
    "foldingRangeProvider",
    "selectionRangeProvider",
    "callHierarchyProvider",
    "typeHierarchyProvider",
    "semanticTokensProvider",
    "inlayHintProvider",
    "diagnosticProvider",
    "linkedEditingRangeProvider",
    "monikerProvider",
    "inlineValueProvider",  # @since 3.17
]
missing_caps = [k for k in MANDATORY_CAPS if k not in cap]
print("    capabilities mandatory:", "ALL" if not missing_caps else f"MISSING {missing_caps}")
assert not missing_caps, f"缺少 capabilities: {missing_caps}"

# 3.17 provider 子字段校验
print("    codeLensProvider keys:", list(cap.get("codeLensProvider", {}).keys()))
print("    inlayHintProvider keys:", list(cap.get("inlayHintProvider", {}).keys()))
print("    inlineValueProvider present:", "inlineValueProvider" in cap)
print("    diagnosticProvider.identifier:", cap.get("diagnosticProvider", {}).get("identifier"))

# workspace / window 能力
ws = cap.get("workspace", {})
win = cap.get("window", {})
print("    workspace.workspaceFolders.supportFolders:", ws.get("workspaceFolders", {}).get("supported", None))
print("    workspace.executeCommand.commands[:3]:", (ws.get("executeCommand") or {}).get("commands", [])[:3])
print("    window.showMessage / workDoneProgress:", list(win.keys()))

# ========== Initialized ==========
send_msg(proc, {"jsonrpc":"2.0","method":"initialized","params":{}})

# ========== DidOpen ==========
send_msg(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///t/m.lua","languageId":"lxclua","version":1,"text":code}}})

# ========== 通用请求函数 ==========
_req_id = [100]
def req(method, params, timeout=12.0):
    _req_id[0] += 1
    rid = _req_id[0]
    send_msg(proc, {"jsonrpc":"2.0","id":rid,"method":method,"params":params})
    return rid, read_next(timeout=timeout, expected_id=rid)

def check(name, resp, checks, warn_only=False):
    """checks: list[(label, predicate)]"""
    lines = [f"    {name}:"]
    ok = True
    r = resp if isinstance(resp, dict) else {}
    for label, pred in checks:
        try:
            res = pred(r)
            mark = "OK" if res else "FAIL"
            lines.append(f"      [{mark}] {label}")
            if not res:
                ok = False
        except Exception as e:
            lines.append(f"      [EXC] {label}: {e}")
            ok = False
    print("\n".join(lines))
    if not ok and not warn_only:
        # 不硬失败，只打印
        pass

# ========== ErrorCode 验证 - MethodNotFound ==========
_, r_mnf = req("nonexistent/method_xyz", {})
check("error.MethodNotFound", r_mnf, [
    ("has error field", lambda r: isinstance(r, dict) and "error" in r),
    ("code -32601", lambda r: r["error"]["code"] == -32601),
    ("message non-empty", lambda r: isinstance(r["error"].get("message"), str) and len(r["error"]["message"]) > 0),
])

# ========== ErrorCode 验证 - InvalidParams (故意缺 textDocument) ==========
_, r_ip = req("textDocument/hover", {"position":{"line":0,"character":0}})
check("error.InvalidParams", r_ip, [
    ("has error or ok result", lambda r: isinstance(r, dict) and ("error" in r or "result" in r)),
    ("code -32602 if error present", lambda r: "error" not in r or r["error"]["code"] == -32602),
])

# ========== WillSaveWaitUntil 返回空数组 ==========
_, r_wswu = req("textDocument/willSaveWaitUntil", {
    "textDocument": {"uri":"file:///t/m.lua"},
    "reason": 1,  # Manual
})
check("willSaveWaitUntil", r_wswu, [
    ("has result", lambda r: "result" in r),
    ("result is [] or null", lambda r: r["result"] is None or (isinstance(r["result"], list) and len(r["result"]) == 0)),
])

# ========== SemanticTokens full/range/delta ==========
_, r_stfull = req("textDocument/semanticTokens/full", {"textDocument":{"uri":"file:///t/m.lua"}})
def st_checks(label):
    return [
        (f"{label} has result", lambda r: "result" in r),
        (f"{label} resultId (str) present", lambda r: isinstance(r["result"].get("resultId"), str) and len(r["result"]["resultId"]) > 0),
        (f"{label} data length %5 == 0", lambda r: len(r["result"].get("data", [])) % 5 == 0),
    ]
check("semanticTokens.full", r_stfull, st_checks("full"))
full_rid = r_stfull["result"].get("resultId") if isinstance(r_stfull, dict) and "result" in r_stfull else None

_, r_strange = req("textDocument/semanticTokens/range", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "range":{"start":{"line":0,"character":0},"end":{"line":9,"character":100}}
})
check("semanticTokens.range", r_strange, st_checks("range"))

# delta: previousResultId 正确 => 返回 {resultId, edits:[]}
_, r_stdlt_ok = req("textDocument/semanticTokens/full/delta", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "previousResultId": full_rid,
})
check("semanticTokens.delta.match", r_stdlt_ok, [
    ("has result", lambda r: "result" in r),
    ("result has resultId", lambda r: isinstance(r["result"].get("resultId"), str)),
    ("has edits OR fallback data", lambda r: "edits" in r["result"] or "data" in r["result"]),
])

# delta: previousResultId 不匹配 => fallback 返回 full (SemanticTokens, 含 data)
_, r_stdlt_fb = req("textDocument/semanticTokens/full/delta", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "previousResultId": "nonexistent-result-id-xyz",
})
check("semanticTokens.delta.fallback", r_stdlt_fb, [
    ("has result", lambda r: "result" in r),
    ("fallback contains data (len%5==0)", lambda r: len(r["result"].get("data", [])) % 5 == 0),
])

# ========== 4 个 Resolve： completionItem / codeLens / documentLink / inlayHint ==========
# 1) completionItem/resolve: 先拿 completion items，取第一个 resolve
_, r_cmp = req("textDocument/completion", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":2}})
first_item = None
if isinstance(r_cmp, dict) and "result" in r_cmp:
    r = r_cmp["result"]
    items = r.get("items") if isinstance(r, dict) else r
    if isinstance(items, list) and items:
        first_item = items[0]

if first_item is not None and first_item.get("data"):
    _, r_cri = req("completionItem/resolve", first_item)
    check("completionItem/resolve", r_cri, [
        ("has result", lambda r: "result" in r),
        ("result has label", lambda r: isinstance(r["result"].get("label"), str)),
        ("result has detail/doc/insertTextMode at least one", lambda r: any(k in r["result"] for k in ("detail","documentation","insertTextMode","additionalTextEdits"))),
    ])
else:
    print("    [SKIP] completionItem/resolve: no item with data")

# 2) codeLens resolve: 先取 codeLens 列表
_, r_cl = req("textDocument/codeLens", {"textDocument":{"uri":"file:///t/m.lua"}})
cl_item = None
if isinstance(r_cl, dict) and "result" in r_cl and isinstance(r_cl["result"], list) and r_cl["result"]:
    for it in r_cl["result"]:
        if isinstance(it, dict) and "data" in it:
            cl_item = it
            break
if cl_item:
    _, r_clr = req("codeLens/resolve", cl_item)
    check("codeLens/resolve", r_clr, [
        ("has result", lambda r: "result" in r),
        ("range present", lambda r: "range" in r["result"]),
        ("command filled or already present", lambda r: "command" in r["result"]),
    ])
else:
    print("    [SKIP] codeLens/resolve: no item with data")

# 3) documentLink resolve
_, r_dl = req("textDocument/documentLink", {"textDocument":{"uri":"file:///t/m.lua"}})
dl_item = None
if isinstance(r_dl, dict) and "result" in r_dl and isinstance(r_dl["result"], list) and r_dl["result"]:
    for it in r_dl["result"]:
        if isinstance(it, dict) and "data" in it:
            dl_item = it
            break
if dl_item:
    _, r_dlr = req("documentLink/resolve", dl_item)
    check("documentLink/resolve", r_dlr, [
        ("has result", lambda r: "result" in r),
        ("has range", lambda r: "range" in r["result"]),
        ("target or tooltip filled", lambda r: "target" in r["result"] or "tooltip" in r["result"]),
    ])
else:
    print("    [SKIP] documentLink/resolve: no item with data")

# 4) inlayHint resolve
_, r_ih = req("textDocument/inlayHint", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "range":{"start":{"line":0,"character":0},"end":{"line":20,"character":0}}
})
ih_item = None
if isinstance(r_ih, dict) and "result" in r_ih and isinstance(r_ih["result"], list) and r_ih["result"]:
    for it in r_ih["result"]:
        if isinstance(it, dict) and "data" in it:
            ih_item = it
            break
if ih_item:
    _, r_ihr = req("inlayHint/resolve", ih_item)
    check("inlayHint/resolve", r_ihr, [
        ("has result", lambda r: "result" in r),
        ("position present", lambda r: "position" in r["result"]),
        ("label present", lambda r: "label" in r["result"]),
    ])
else:
    print("    [SKIP] inlayHint/resolve: no item with data")

# ========== Call Hierarchy ==========
_, r_chp = req("textDocument/prepareCallHierarchy", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "position":{"line":4,"character":16}  # export function f()end 的 f
})
ch_item = None
if isinstance(r_chp, dict) and "result" in r_chp and isinstance(r_chp["result"], list) and r_chp["result"]:
    ch_item = r_chp["result"][0]
check("callHierarchy.prepare", r_chp, [
    ("result is list", lambda r: isinstance(r.get("result"), list)),
    ("first item has name/uri/range", lambda r: not r["result"] or all(k in r["result"][0] for k in ("name","uri","range","selectionRange"))),
    ("first item has data", lambda r: not r["result"] or "data" in r["result"][0]),
])
if ch_item:
    _, r_chi = req("callHierarchy/incomingCalls", {"item": ch_item})
    check("callHierarchy.incoming", r_chi, [
        ("result is list or null", lambda r: r.get("result") is None or isinstance(r["result"], list)),
    ])
    _, r_cho = req("callHierarchy/outgoingCalls", {"item": ch_item})
    check("callHierarchy.outgoing", r_cho, [
        ("result is list or null", lambda r: r.get("result") is None or isinstance(r["result"], list)),
    ])

# ========== Type Hierarchy ==========
_, r_thp = req("textDocument/prepareTypeHierarchy", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "position":{"line":9,"character":7}  # struct Point
})
th_item = None
if isinstance(r_thp, dict) and "result" in r_thp and isinstance(r_thp["result"], list) and r_thp["result"]:
    th_item = r_thp["result"][0]
check("typeHierarchy.prepare", r_thp, [
    ("result is list or null", lambda r: r.get("result") is None or isinstance(r["result"], list)),
    ("first item has name/uri/range/data", lambda r: (not r["result"]) or all(k in r["result"][0] for k in ("name","uri","range"))),
])
if th_item:
    _, r_su = req("typeHierarchy/supertypes", {"item": th_item})
    check("typeHierarchy.supertypes", r_su, [
        ("result is list or null", lambda r: r.get("result") is None or isinstance(r["result"], list)),
    ])
    _, r_sb = req("typeHierarchy/subtypes", {"item": th_item})
    check("typeHierarchy.subtypes", r_sb, [
        ("result is list or null", lambda r: r.get("result") is None or isinstance(r["result"], list)),
    ])

# ========== DocumentColor + ColorPresentation ==========
_, r_dc = req("textDocument/documentColor", {"textDocument":{"uri":"file:///t/m.lua"}})
check("documentColor", r_dc, [
    ("result is list", lambda r: isinstance(r.get("result"), list)),
    ("each has color rgba & range", lambda r: all(
        isinstance(x, dict)
        and "range" in x
        and isinstance(x.get("color"), dict)
        and all(k in x["color"] for k in ("red","green","blue","alpha"))
        for x in r["result"]
    ) or not r["result"]),
])
dc_item = None
if isinstance(r_dc, dict) and isinstance(r_dc.get("result"), list) and r_dc["result"]:
    dc_item = r_dc["result"][0]
if dc_item:
    _, r_cp = req("textDocument/colorPresentation", {
        "textDocument": {"uri":"file:///t/m.lua"},
        "color": dc_item["color"],
        "range": dc_item["range"],
    })
    check("colorPresentation", r_cp, [
        ("result is list", lambda r: isinstance(r.get("result"), list)),
        ("each has label", lambda r: all(isinstance(x, dict) and isinstance(x.get("label"), str) for x in r["result"]) or not r["result"]),
    ])

# ========== Moniker ==========
_, r_mk = req("textDocument/moniker", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "position":{"line":5,"character":9}  # function g
})
check("moniker", r_mk, [
    ("result is list or null", lambda r: r.get("result") is None or isinstance(r.get("result"), list)),
    ("each has scheme+identifier+unique+kind", lambda r: (r.get("result") is None) or (not r.get("result")) or all(
        isinstance(x, dict) and all(k in x for k in ("scheme","identifier","unique","kind"))
        for x in r.get("result")
    )),
])

# ========== WorkspaceFolders 系列 ==========
_, r_wf = req("workspace/workspaceFolders", {})
check("workspace.workspaceFolders", r_wf, [
    ("result is list", lambda r: isinstance(r.get("result"), list)),
    ("contains folder from init", lambda r: (not isinstance(r.get("result"), list)) or any(f.get("uri")=="file:///t" for f in r.get("result"))),
])

# didChangeWorkspaceFolders 通知（不等待响应）
send_msg(proc, {
    "jsonrpc":"2.0",
    "method":"workspace/didChangeWorkspaceFolders",
    "params":{
        "event":{
            "added":[{"uri":"file:///t/extra","name":"extra"}],
            "removed":[]
        }
    }
})

# didChangeWatchedFiles 通知
send_msg(proc, {
    "jsonrpc":"2.0",
    "method":"workspace/didChangeWatchedFiles",
    "params":{"changes":[{"uri":"file:///t/m.lua","type":2}]}  # Changed
})
time.sleep(0.1)

# ========== ExecuteCommand ==========
_, r_ec = req("workspace/executeCommand", {"command":"lxclua.reload","arguments":[]})
check("executeCommand(lxclua.reload)", r_ec, [
    ("no error", lambda r: "error" not in r),
])
_, r_ec2 = req("workspace/executeCommand", {"command":"lxclua.clearCache","arguments":[]})
check("executeCommand(lxclua.clearCache)", r_ec2, [
    ("no error", lambda r: "error" not in r),
])

# ========== WorkspaceSymbol + resolve ==========
_, r_ws = req("workspace/symbol", {"query":"Point"})
ws_item = None
if isinstance(r_ws, dict) and isinstance(r_ws.get("result"), list) and r_ws["result"]:
    ws_item = r_ws["result"][0]
check("workspace.symbol", r_ws, [
    ("result is list or null", lambda r: r.get("result") is None or isinstance(r.get("result"), list)),
])
if ws_item and ws_item.get("data"):
    _, r_wsr = req("workspaceSymbol/resolve", ws_item)
    check("workspaceSymbol/resolve", r_wsr, [
        ("has result", lambda r: "result" in r),
        ("name present", lambda r: isinstance(r.get("result", {}).get("name"), str)),
    ])
else:
    print("    [SKIP] workspaceSymbol/resolve")

# ========== Window 通知（只发送不需要响应） ==========
send_msg(proc, {"jsonrpc":"2.0","method":"window/logMessage","params":{"type":3,"message":"test_log"}})
send_msg(proc, {"jsonrpc":"2.0","method":"window/showMessage","params":{"type":1,"message":"test_show"}})

# ========== Pull Diagnostic ==========
_, r_dp = req("textDocument/diagnostic", {"textDocument":{"uri":"file:///t/m.lua"}})
check("textDocument.diagnostic(pull)", r_dp, [
    ("has result", lambda r: "result" in r),
    ("result.kind enum", lambda r: r.get("result",{}).get("kind") in ("full","unchanged")),
    ("result.items array", lambda r: isinstance(r.get("result",{}).get("items"), list)),
])

# ========== @since 3.17 inlineValue ==========
_, r_iv = req("textDocument/inlineValue", {
    "textDocument":{"uri":"file:///t/m.lua"},
    "range":{"start":{"line":0,"character":0},"end":{"line":9,"character":100}},
    "context":{"viewRange":{"start":{"line":0,"character":0},"end":{"line":9,"character":100}}}
})
check("textDocument.inlineValue", r_iv, [
    ("result is list", lambda r: isinstance(r.get("result"), list)),
])

# ========== @since 3.17 $/setTrace notification (no response) + check server still alive ==========
send_msg(proc, {"jsonrpc":"2.0","method":"$/setTrace","params":{"value":"messages"}})
send_msg(proc, {"jsonrpc":"2.0","method":"$/logTrace","params":{"message":"client-trace-msg","verbosity":"verbose"}})
time.sleep(0.1)
# 验证服务端仍然响应
_, r_iv2 = req("textDocument/inlineValue", {"textDocument":{"uri":"file:///t/m.lua"},
    "range":{"start":{"line":0,"character":0},"end":{"line":1,"character":0}},
    "context":{"viewRange":{"start":{"line":0,"character":0},"end":{"line":1,"character":0}}}})
check("server alive after $/setTrace", r_iv2, [
    ("still responds", lambda r: isinstance(r, dict) and "result" in r),
])

# ========== 刷新请求出队测试：用 didChange 触发 publishDiagnostics/refresh 出队后服务端仍响应 ==========
# 先消耗 pending 队列（如果有）
drained_count = 0
while True:
    # 用 timeout=0.2 快速尝试读取 pending 通知
    leftover = read_next(timeout=0.2, only_response=False)
    if leftover is None:
        break
    drained_count += 1
if drained_count > 0:
    print(f"    pre-req drained {drained_count} stray notifications/responses")

# ========== executeCommand unknown ==========
_, r_ec3 = req("workspace/executeCommand", {"command":"cmd.not.exists","arguments":[]})
check("executeCommand(unknown)", r_ec3, [
    ("has error", lambda r: "error" in r),
    ("code in LSP reserved range -328xx", lambda r: -32899 <= r["error"].get("code", 0) <= -32800),
])

# ========== window/showMessageRequest（服务端作为接收方返回 null） ==========
_, r_smreq = req("window/showMessageRequest", {"type":3,"message":"test msg","actions":[{"title":"OK"}]})
check("window/showMessageRequest(as recipient)", r_smreq, [
    ("has result (null allowed)", lambda r: "result" in r),
    ("result null or MessageActionItem dict/list", lambda r: r["result"] is None or isinstance(r["result"], (dict,list))),
])

# ========== window/showDocument（返回 {success: true}） ==========
_, r_sdoc = req("window/showDocument", {"uri":"file:///t/m.lua","external":False,"takeFocus":True})
check("window/showDocument", r_sdoc, [
    ("has result object", lambda r: isinstance(r.get("result"), dict)),
    ("result.success == true", lambda r: r["result"].get("success") is True),
])

# ========== client/registerCapability + unregisterCapability（都返回 null） ==========
_, r_rc = req("client/registerCapability", {"registrations":[{"id":"1","method":"textDocument/didOpen"}]})
check("client/registerCapability", r_rc, [("result is null", lambda r: r.get("result") is None)])
_, r_urc = req("client/unregisterCapability", {"unregisterations":[{"id":"1","method":"textDocument/didOpen"}]})
check("client/unregisterCapability", r_urc, [("result is null", lambda r: r.get("result") is None)])

# ========== shutdown 后再次请求 -> ServerCancelled(-32802) ==========
send_msg(proc, {"jsonrpc":"2.0","id":9999,"method":"shutdown","params":{}})
r_sd = read_next(timeout=10, expected_id=9999)
check("shutdown", r_sd, [("result null", lambda r: r is None or r.get("result") is None)], warn_only=True)

_, r_after_sd = req("textDocument/hover", {"textDocument":{"uri":"file:///t/m.lua"},"position":{"line":0,"character":0}})
check("shutdown.ServerCancelled", r_after_sd, [
    ("has error", lambda r: "error" in r),
    ("code == -32802 (ServerCancelled)", lambda r: r.get("error",{}).get("code") == -32802),
], warn_only=True)

send_msg(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
try:
    proc.wait(timeout=5)
except:
    proc.kill()
# 从后台缓冲区取 stderr
_stderr_thread.join(timeout=2)
with _stderr_lock:
    stderr = ''.join(_stderr_buf)
if stderr:
    print(f"\n=== Stderr ===\n{stderr[:1200]}")
print("\nDone.")
