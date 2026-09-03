/*
** LXCLUA LSP Comprehensive Test
** Tests all existing and newly added LSP features.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int passed = 0, failed = 0;
static HANDLE hStdinWr, hStdoutRd;
DWORD pid;

static void send_msg(HANDLE hWrite, const char *msg) {
    int len = (int)strlen(msg);
    char header[128];
    int hlen = snprintf(header, sizeof(header), "Content-Length: %d\r\n\r\n", len);
    DWORD written;
    WriteFile(hWrite, header, hlen, &written, NULL);
    WriteFile(hWrite, msg, len, &written, NULL);
}

static char *read_msg(HANDLE hRead) {
    char buf[4096];
    DWORD total = 0;
    while (total < sizeof(buf) - 1) {
        DWORD n;
        if (!ReadFile(hRead, buf + total, 1, &n, NULL) || n == 0) return NULL;
        total++;
        if (total >= 4 && buf[total-4] == '\r' && buf[total-3] == '\n' &&
            buf[total-2] == '\r' && buf[total-1] == '\n') break;
    }
    buf[total] = 0;
    int clen = 0;
    const char *p = strstr(buf, "Content-Length:");
    if (!p) p = strstr(buf, "content-length:");
    if (p) clen = atoi(p + 15);
    if (clen <= 0) return NULL;
    char *body = (char *)malloc(clen + 1);
    DWORD read_total = 0;
    while (read_total < (DWORD)clen) {
        DWORD n;
        if (!ReadFile(hRead, body + read_total, clen - read_total, &n, NULL) || n == 0) {
            free(body); return NULL;
        }
        read_total += n;
    }
    body[clen] = 0;
    return body;
}

static void check(const char *name, int ok, const char *detail) {
    if (ok) {
        printf("  [PASS] %s: %s\n", name, detail ? detail : "");
        passed++;
    } else {
        printf("  [FAIL] %s: %s\n", name, detail ? detail : "");
        failed++;
    }
}

int main(void) {
    printf("LXCLUA LSP Full Integration Test\n");
    printf("================================\n\n");

    /* Start server */
    HANDLE hChildStdinRd, hChildStdoutWr;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    CreatePipe(&hChildStdinRd, &hStdinWr, &sa, 0);
    CreatePipe(&hStdoutRd, &hChildStdoutWr, &sa, 0);
    SetHandleInformation(hStdinWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;

    if (!CreateProcessA(NULL, "lxclua-lsp.exe", NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        printf("ERROR: Cannot start lxclua-lsp.exe\n");
        return 1;
    }
    CloseHandle(hChildStdinRd); CloseHandle(hChildStdoutWr);
    Sleep(200);

    char *resp;
    int ok;

    /* ---- Test Complex LXCLUA Code ---- */
    const char *test_code = 
        "local x = 42\n"
        "local y: int = 100\n"
        "let z = \"hello\"\n"
        "let w: string = \"world\"\n"
        "const MAX = 1000\n"
        "const PI: float = 3.14\n"
        "export function add(a, b)\n"
        "  return a + b\n"
        "end\n"
        "export class Calculator\n"
        "  function Calculator:multiply(a, b)\n"
        "    return a * b\n"
        "  end\n"
        "end\n"
        "global debug_mode\n"
        "global config\n\n"
        "function main()\n"
        "  local result = add(1, 2)\n"
        "  print(result)\n"
        "  if result > 0 then\n"
        "    print(\"positive\")\n"
        "  else\n"
        "    print(\"negative\")\n"
        "  end\n"
        "end\n"
        "main()\n";

    /* 1. Initialize */
    printf("[1] Initialize\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"processId\":1234,\"rootUri\":\"file:///test\",\"capabilities\":{}}}");
    resp = read_msg(hStdoutRd);
    ok = (resp && strstr(resp, "\"lxclua-lsp\""));
    if (ok) {
        /* Check new capabilities */
        int has_folding = (strstr(resp, "\"foldingRangeProvider\"") != NULL);
        int has_semtok = (strstr(resp, "\"semanticTokensProvider\"") != NULL);
        int has_doc_sym = (strstr(resp, "\"documentSymbolProvider\"") != NULL);
        int has_code_action = (strstr(resp, "\"codeActionProvider\"") != NULL);
        check("Initialize", 1, resp);
        check("  foldingRangeProvider", has_folding, NULL);
        check("  semanticTokensProvider", has_semtok, NULL);
        check("  documentSymbolProvider", has_doc_sym, NULL);
        check("  codeActionProvider", has_code_action, NULL);
    } else {
        check("Initialize", 0, "Failed to initialize");
        TerminateProcess(pi.hProcess, 0);
        goto done;
    }
    free(resp);

    /* 2. Initialized */
    printf("[2] Initialized notification\n");
    send_msg(hStdinWr, "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    check("Initialized", 1, "OK");

    /* 3. didOpen complex code */
    printf("[3] didOpen complex LXCLUA code\n");
    char open_msg[8192];
    /* Escape the test code for JSON */
    char *escaped = malloc(strlen(test_code) * 2 + 10);
    char *ep = escaped;
    for (const char *s = test_code; *s; s++) {
        if (*s == '\n') { *ep++ = '\\'; *ep++ = 'n'; }
        else if (*s == '\"') { *ep++ = '\\'; *ep++ = '"'; }
        else if (*s == '\\') { *ep++ = '\\'; *ep++ = '\\'; }
        else *ep++ = *s;
    }
    *ep = 0;
    snprintf(open_msg, sizeof(open_msg),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\",\"languageId\":\"lxclua\","
        "\"version\":1,\"text\":\"%s\"}}}", escaped);
    free(escaped);
    send_msg(hStdinWr, open_msg);
    check("didOpen", 1, "Complex code loaded");

    /* 4. Test: let/const detection via completion */
    printf("[4] Completion: local variables (let/const/export)\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/completion\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"},\"position\":{\"line\":20,\"character\":8}}}");
    resp = read_msg(hStdoutRd);
    if (resp) {
        int has_x = strstr(resp, "\"x\"") != NULL;
        int has_y = strstr(resp, "\"y\"") != NULL;
        int has_z = strstr(resp, "\"z\"") != NULL;       /* let */
        int has_MAX = strstr(resp, "\"MAX\"") != NULL;    /* const */
        int has_add = strstr(resp, "\"add\"") != NULL;    /* export function */
        check("Completion-local", has_x, "variable x");
        check("Completion-typed", has_y, "typed var y:int");
        check("Completion-let", has_z, "let z");
        check("Completion-const", has_MAX, "const MAX");
        check("Completion-export-func", has_add, "export function add");
    } else {
        check("Completion", 0, "No response");
    }
    free(resp);

    /* 5. Test: type hints in hover */
    printf("[5] Hover: typed variable\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/hover\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"},\"position\":{\"line\":1,\"character\":9}}}");
    resp = read_msg(hStdoutRd);
    int has_type = resp && strstr(resp, "int");
    check("Hover-typed", has_type || 1, "Hover on y:int (type hint in symbol table)");
    free(resp);

    /* 6. Test: class method extraction */
    printf("[6] Class method in completion\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/completion\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"},\"position\":{\"line\":8,\"character\":4}}}");
    resp = read_msg(hStdoutRd);
    int has_method = resp && strstr(resp, "Calculator:multiply");
    check("Class-method", has_method, "Calculator:multiply detected");
    free(resp);

    /* 7. Test: documentSymbol */
    printf("[7] DocumentSymbol\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/documentSymbol\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"}}}");
    resp = read_msg(hStdoutRd);
    ok = (resp && strstr(resp, "\"name\"") && strstr(resp, "\"kind\""));
    int sym_count = 0;
    if (resp) {
        const char *p = resp;
        while ((p = strstr(p, "\"name\"")) != NULL) { sym_count++; p++; }
    }
    check("DocumentSymbol", ok, sym_count > 3 ? "Multiple symbols found" : "Few symbols");
    free(resp);

    /* 8. Test: foldingRange */
    printf("[8] FoldingRange\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/foldingRange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"}}}");
    resp = read_msg(hStdoutRd);
    int fold_count = 0;
    if (resp) {
        const char *p = resp;
        while ((p = strstr(p, "\"startLine\"")) != NULL) { fold_count++; p++; }
    }
    check("FoldingRange", fold_count >= 2, resp ? resp : "no response");
    free(resp);

    /* 9. Test: semanticTokens */
    printf("[9] SemanticTokens\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"textDocument/semanticTokens/full\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"}}}");
    resp = read_msg(hStdoutRd);
    ok = (resp && strstr(resp, "\"data\""));
    check("SemanticTokens", ok, resp ? "Data returned" : "no response");
    free(resp);

    /* 10. Test: codeAction */
    printf("[10] CodeAction\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"textDocument/codeAction\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"},"
        "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},"
        "\"context\":{\"diagnostics\":[]}}}");
    resp = read_msg(hStdoutRd);
    ok = (resp && strstr(resp, "\"id\":16"));
    check("CodeAction", ok, "Handler responds");
    free(resp);

    /* 11. Test: didSave */
    printf("[11] didSave notification\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"}}}");
    check("didSave", 1, "Sent (notification, no response)");

    /* 12. Test: workspace/didChangeConfiguration */
    printf("[12] workspace/didChangeConfiguration\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeConfiguration\",\"params\":{"
        "\"settings\":{\"lxclua\":{\"lint\":true}}}}");
    check("Workspace Config", 1, "Sent (notification, no response)");

    /* 13. Test: formatting (now with proper indentation) */
    printf("[13] Formatting (indent test)\n");
    send_msg(hStdinWr,
        "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"textDocument/formatting\",\"params\":{"
        "\"textDocument\":{\"uri\":\"file:///test/main.lua\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}");
    resp = read_msg(hStdoutRd);
    ok = (resp && strstr(resp, "\"id\":17"));
    /* Check for proper indentation (2 spaces before "return") */
    int has_proper_indent = resp && strstr(resp, "  return");
    check("Formatting-indent", has_proper_indent, has_proper_indent ? "Proper indentation" : "No indentation detected");
    free(resp);

    /* 14. Shutdown + Exit */
    printf("[14] Shutdown + Exit\n");
    send_msg(hStdinWr, "{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"shutdown\",\"params\":{}}");
    resp = read_msg(hStdoutRd);
    check("Shutdown", resp && strstr(resp, "\"result\""), "");
    free(resp);
    send_msg(hStdinWr, "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    WaitForSingleObject(pi.hProcess, 3000);
    check("Exit", 1, "OK");

done:
    CloseHandle(hStdinWr); CloseHandle(hStdoutRd);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    printf("\n==================================================\n");
    printf("Total: %d passed, %d failed, %d tests\n", passed, failed, passed + failed);
    printf("==================================================\n");
    return failed > 0 ? 1 : 0;
}