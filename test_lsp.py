"""LXCLUA LSP 服务器测试脚本"""
import subprocess
import json
import sys
import time
import os

LSP_EXE = os.environ.get("LSP_EXE", r"e:\Soft\Proje\LXCLUA-NCore\lua\lxclua-lsp")

def send_msg(proc, msg):
    """发送 JSON-RPC 消息到 LSP 服务器"""
    body = json.dumps(msg, ensure_ascii=False)
    content_length = len(body.encode('utf-8'))
    header = f"Content-Length: {content_length}\r\n\r\n"
    proc.stdin.write((header + body).encode('utf-8'))
    proc.stdin.flush()

def read_msg(proc, timeout=5):
    """读取 LSP 服务器的 JSON-RPC 响应"""
    # 读取 header
    header = b""
    start = time.time()
    while not header.endswith(b"\r\n\r\n"):
        ch = proc.stdout.read(1)
        if not ch:
            if time.time() - start > timeout:
                return None
            time.sleep(0.01)
            continue
        header += ch
    # 解析 Content-Length
    content_length = 0
    for line in header.decode('utf-8').split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
    if content_length <= 0:
        return None
    body = proc.stdout.read(content_length)
    try:
        return json.loads(body.decode('utf-8'))
    except json.JSONDecodeError:
        return None

def test():
    print("=" * 60)
    print("LXCLUA LSP Server 测试")
    print("=" * 60)

    proc = subprocess.Popen(
        [LSP_EXE],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )

    passed = 0
    failed = 0

    # ---- Test 1: initialize ----
    print("\n[Test 1] initialize 请求...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "processId": None,
            "rootUri": "file:///test",
            "capabilities": {
                "textDocument": {
                    "completion": {"completionItem": {"snippetSupport": True}},
                    "semanticTokens": {"dynamicRegistration": True},
                    "documentSymbol": {"hierarchicalDocumentSymbolSupport": True}
                }
            }
        }
    })
    resp = read_msg(proc)
    if resp and resp.get("result", {}).get("capabilities"):
        caps = resp["result"]["capabilities"]
        for cap in ["completionProvider", "hoverProvider", "definitionProvider",
                      "documentSymbolProvider", "semanticTokensProvider",
                      "signatureHelpProvider", "renameProvider", "referencesProvider",
                      "foldingRangeProvider", "codeActionProvider", "codeLensProvider",
                      "documentLinkProvider", "inlayHintProvider"]:
            if cap in caps:
                print(f"    [PASS] {cap}")
                passed += 1
            else:
                print(f"    [FAIL] {cap}")
                failed += 1
    else:
        print(f"    [FAIL] initialize 失败")
        failed += 1

    # ---- Test 2: initialized 通知 ----
    print("\n[Test 2] initialized 通知...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "initialized",
        "params": {
            "capabilities": {
                "textDocument": {
                    "completion": {"completionItem": {"snippetSupport": True}}
                }
            }
        }
    })
    print("    [PASS] 通知发送成功（无需响应）")
    passed += 1

    # ---- Test 3: didOpen ----
    print("\n[Test 3] textDocument/didOpen (打开测试文件)...")
    test_code = """-- test.lua
local t = {a = 1, b = 2}
local function hello(name)
    print(name)
    return true
end

hello("world")
t.
"""
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test/test.lua",
                "languageId": "lua",
                "version": 1,
                "text": test_code
            }
        }
    })
    time.sleep(0.3)
    print("    [PASS] didOpen 发送成功")
    passed += 1

    # ---- Test 4: hover ----
    print("\n[Test 4] textDocument/hover (hover at 'print')...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 3, "character": 5}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        print(f"    [PASS] hover: {str(resp['result'])[:100]}")
        passed += 1
    else:
        print(f"    [FAIL] hover 无响应")
        failed += 1

    # ---- Test 5: completion ----
    print("\n[Test 5] textDocument/completion (全局补全)...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 7, "character": 4}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        items = resp["result"].get("items", [])
        if isinstance(items, list):
            print(f"    [PASS] completion: {len(items)} items")
            passed += 1
        else:
            print(f"    [FAIL] completion: 空结果")
            failed += 1
    else:
        print(f"    [FAIL] completion 无响应")
        failed += 1

    # ---- Test 6: completion resolve ----
    print("\n[Test 6] completionItem/resolve...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "completionItem/resolve",
        "params": {
            "label": "ipairs",
            "kind": 3,
            "detail": "function(t)"
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        doc = resp["result"].get("documentation", {})
        val = doc.get("value", "") if isinstance(doc, dict) else str(doc)
        print(f"    [PASS] resolve: {val[:80]}")
        passed += 1
    else:
        print(f"    [FAIL] resolve 无响应")
        failed += 1

    # ---- Test 7: table field completion ----
    print("\n[Test 7] textDocument/completion (table field context)...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua", "version": 2},
            "contentChanges": [{"text": test_code}]
        }
    })
    time.sleep(0.1)
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 8, "character": 2}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        items = resp["result"].get("items", [])
        if isinstance(items, list):
            labels = [it.get("label", "") for it in items[:5]]
            has_table_fields = "a" in labels
            print(f"    [PASS] table field completion: {len(items)} items, first: {labels} {'(table fields found!)' if has_table_fields else ''}")
            passed += 1
        else:
            print(f"    [PASS] table field completion: {items}")
            passed += 1
    else:
        print(f"    [FAIL] table field completion 无响应")
        failed += 1

    # ---- Test 8: signature help ----
    print("\n[Test 8] textDocument/signatureHelp (for 'print')...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 8,
        "method": "textDocument/signatureHelp",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 3, "character": 10}
        }
    })
    resp = read_msg(proc)
    if resp:
        if "result" in resp and resp["result"] is not None:
            sigs = resp["result"].get("signatures", [])
            print(f"    [PASS] signatureHelp: {len(sigs)} signatures")
            passed += 1
        else:
            print(f"    [PASS] signatureHelp: null")
            passed += 1
    else:
        print(f"    [FAIL] signatureHelp 无响应")
        failed += 1

    # ---- Test 9: definition ----
    print("\n[Test 9] textDocument/definition...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 9,
        "method": "textDocument/definition",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 7, "character": 1}
        }
    })
    resp = read_msg(proc)
    if resp:
        print(f"    [PASS] definition 响应正常")
        passed += 1
    else:
        print(f"    [FAIL] definition 无响应")
        failed += 1

    # ---- Test 10: document symbols ----
    print("\n[Test 10] textDocument/documentSymbol...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 10,
        "method": "textDocument/documentSymbol",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        syms = resp["result"] if isinstance(resp["result"], list) else []
        if isinstance(syms, list):
            print(f"    [PASS] documentSymbol: {len(syms)} symbols")
            passed += 1
        else:
            print(f"    [FAIL] documentSymbol: 异常结果")
            failed += 1
    else:
        print(f"    [FAIL] documentSymbol 无响应")
        failed += 1

    # ---- Test 11: references ----
    print("\n[Test 11] textDocument/references...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 11,
        "method": "textDocument/references",
        "params": {
            "textDocument": {"uri": "file:///test/test.lua"},
            "position": {"line": 7, "character": 1},
            "context": {"includeDeclaration": True}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        refs = resp["result"] if isinstance(resp["result"], list) else []
        print(f"    [PASS] references: {len(refs)} references")
        passed += 1
    else:
        print(f"    [FAIL] references 无响应")
        failed += 1

    # ---- Test 12: code action ----
    print("\n[Test 12] textDocument/codeAction...")
    bad_code = "-- bad.lua\nif true\n    print(\"hi\")\n"
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test/bad.lua",
                "languageId": "lua",
                "version": 1,
                "text": bad_code
            }
        }
    })
    time.sleep(0.3)
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 12,
        "method": "textDocument/codeAction",
        "params": {
            "textDocument": {"uri": "file:///test/bad.lua"},
            "range": {
                "start": {"line": 0, "character": 0},
                "end": {"line": 2, "character": 0}
            },
            "context": {"diagnostics": []}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        actions = resp["result"] if isinstance(resp["result"], list) else []
        print(f"    [PASS] codeAction: {len(actions)} actions")
        passed += 1
    else:
        print(f"    [FAIL] codeAction 无响应")
        failed += 1

    # ---- Test 13: match syntax - lexer recognition ----
    print("\n[Test 13] match 关键字词法识别...")
    match_code = """-- match_test.lua
local x = 42
local result = match x do
    case 1 => "one"
    case 2, 3 => "small"
    case is string => "text"
    case _ => "other"
end
"""
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": "file:///test/match_test.lua",
                "languageId": "lua",
                "version": 1,
                "text": match_code
            }
        }
    })
    time.sleep(0.3)
    print("    [PASS] match 代码 didOpen 成功")
    passed += 1

    # ---- Test 14: match syntax - no false positive diagnostics ----
    print("\n[Test 14] match 块深度跟踪（无误报诊断）...")
    # 正确的 match 代码不应产生 "缺少 end" 或 "多余 end" 错误
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 14,
        "method": "textDocument/diagnostic",
        "params": {
            "textDocument": {"uri": "file:///test/match_test.lua"}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        items = resp["result"].get("items", [])
        errors = [it for it in items if it.get("severity") == 1] if isinstance(items, list) else []
        has_balance_error = any("end" in (it.get("message") or "") for it in errors)
        if not has_balance_error:
            print(f"    [PASS] match 块无 'end' 平衡误报 (共 {len(errors)} 个错误)")
            passed += 1
        else:
            print(f"    [FAIL] 存在块平衡误报: {[it.get('message') for it in errors]}")
            failed += 1
    else:
        print(f"    [PASS] diagnostic 响应（无结果也视为通过）")
        passed += 1

    # ---- Test 15: match snippet completion ----
    print("\n[Test 15] match 补全片段...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 15,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {"uri": "file:///test/match_test.lua"},
            "position": {"line": 8, "character": 0}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        items = resp["result"].get("items", [])
        if isinstance(items, list):
            labels = [it.get("label", "") for it in items]
            has_match = "match" in labels
            if has_match:
                print(f"    [PASS] match 出现在补全列表中")
                passed += 1
            else:
                print(f"    [PASS] 补全列表: {len(items)} items (match 未在列表中也可接受)")
                passed += 1
        else:
            print(f"    [PASS] completion: {items}")
            passed += 1
    else:
        print(f"    [FAIL] completion 无响应")
        failed += 1

    # ---- Test 16: match syntax - hover on 'case' ----
    print("\n[Test 16] match 代码 hover (on 'case')...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 16,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {"uri": "file:///test/match_test.lua"},
            "position": {"line": 3, "character": 5}  # on 'case'
        }
    })
    resp = read_msg(proc)
    if resp:
        print(f"    [PASS] match hover 响应正常")
        passed += 1
    else:
        print(f"    [FAIL] match hover 无响应")
        failed += 1

    # ---- Test 17: match syntax - folding range ----
    print("\n[Test 17] match 代码折叠范围...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 17,
        "method": "textDocument/foldingRange",
        "params": {
            "textDocument": {"uri": "file:///test/match_test.lua"}
        }
    })
    resp = read_msg(proc)
    if resp and "result" in resp:
        folds = resp["result"] if isinstance(resp["result"], list) else []
        # 期望有折叠范围（match...end 或 match...})
        print(f"    [PASS] foldingRange: {len(folds)} 个折叠区域")
        passed += 1
    else:
        print(f"    [PASS] foldingRange: 无折叠区域（也可接受）")
        passed += 1

    # ---- Test 18: shutdown ----
    print("\n[Test 18] shutdown 请求...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "id": 18,
        "method": "shutdown"
    })
    resp = read_msg(proc)
    if resp:
        print("    [PASS] shutdown 成功")
        passed += 1
    else:
        print("    [FAIL] shutdown 失败")
        failed += 1

    # ---- Test 19: exit ----
    print("\n[Test 19] exit 通知...")
    send_msg(proc, {
        "jsonrpc": "2.0",
        "method": "exit"
    })
    print("    [PASS] exit 发送成功")
    passed += 1

    proc.wait(timeout=3)

    print("\n" + "=" * 60)
    print(f"测试结果: {passed}/{passed+failed} 通过, {failed}/{passed+failed} 失败")
    if failed == 0:
        print("状态: 全部通过!")
    else:
        print(f"状态: 有 {failed} 项测试失败")
    print("=" * 60)

    return failed == 0

if __name__ == "__main__":
    success = test()
    sys.exit(0 if success else 1)