/*
** LXCLUA LSP - Protocol Handler
** Handles JSON-RPC message dispatching and LSP method routing.
** This is the core router between JSON-RPC messages and LSP features.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lspsrv.h"

/* External function declarations from feature modules */
extern char *lsp_strdup(const char *s);
extern void *lsp_alloc(size_t sz);
extern void lsp_free(void *p);
extern char *lsp_str_append(char *dst, const char *src);
extern char *lsp_fmt(const char *fmt, ...);

/* Document management */
extern int lsp_doc_open(void *server, const char *uri, const char *text, int version);
extern int lsp_doc_change(void *server, const char *uri, const char *text, int version);
extern int lsp_doc_close(void *server, const char *uri);
extern LspDocument *lsp_doc_find(void *server, const char *uri);

/* Features */
extern int lsp_completion(LspDocument *doc, int line, int col, LspCompletionItem **items);
extern char *lsp_hover(LspDocument *doc, int line, int col);
extern char *lsp_get_symbol_at(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);
extern int lsp_diagnostic(LspDocument *doc, LspDiagnostic **diags);
extern char *lsp_format(LspDocument *doc, int tab_size, int insert_spaces);
extern int lsp_rename(LspDocument *doc, int line, int col, const char *new_name);
extern char *lsp_signature_help(LspDocument *doc, int line, int col);
extern int lsp_find_references(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);
extern int lsp_document_highlight(LspDocument *doc, int line, int col, int **out_kinds, int **out_lines, int **out_cols, int *count);

/* New LSP features */
extern int lsp_document_symbol(LspDocument *doc, LspSymbol ***out_symbols);
extern int lsp_folding_range(LspDocument *doc, int **out_start_lines, int **out_end_lines, int *count);
extern char *lsp_semantic_tokens(LspDocument *doc);
extern int lsp_code_action(LspDocument *doc, int line, int col, LspDiagnostic **diag_list, int *count);
extern int lsp_prepare_rename(LspDocument *doc, int line, int col, int *out_line, int *out_col, int *out_end_line, int *out_end_col);
extern int lsp_type_definition(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);
extern int lsp_find_implementation(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);
extern int lsp_workspace_symbol(void *server, const char *query, LspSymbol ***out_symbols);
extern int lsp_selection_range(LspDocument *doc, int npositions, int *lines, int *cols, int **out_start_lines, int **out_end_lines);
extern int lsp_linked_editing_range(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);

/* Advanced LSP features (added to lspsrv_features.c) */
extern int lsp_declaration(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);
extern int lsp_code_lens(LspDocument *doc, int **out_lines, int **out_cols, char ***out_titles, char ***out_commands, int *count);
extern int lsp_document_link(LspDocument *doc, int **out_start_lines, int **out_start_cols, int **out_end_lines, int **out_end_cols, char ***out_targets, int *count);
extern int lsp_inlay_hint(LspDocument *doc, int start_line, int end_line, char ***out_labels, int **out_lines, int **out_cols, int *count);
extern int lsp_prepare_call_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col);
extern int lsp_call_hierarchy_incoming(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count);
extern int lsp_call_hierarchy_outgoing(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count);
extern int lsp_prepare_type_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col);
extern int lsp_type_hierarchy_supertypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count);
extern int lsp_type_hierarchy_subtypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count);
extern int lsp_color_presentation(LspDocument *doc, int line, int col, char ***out_labels, int *count);
extern int lsp_moniker(LspDocument *doc, int line, int col, char ***out_schemes, char ***out_identifiers, int *count);
extern char *lsp_on_type_formatting(LspDocument *doc, int line, int col, const char *ch, int tab_size, int insert_spaces);
extern char *lsp_range_formatting(LspDocument *doc, int start_line, int start_col, int end_line, int end_col, int tab_size, int insert_spaces);
extern const char *lsp_kwdb_find_doc(const char *name);

/* Helper to extract textDocument URI and position from params */
static int params_get_doc_pos(JsonValue *params, char **uri, int *line, int *col) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return -1;
    *uri = lsp_strdup(json_object_get_string(td, "uri", ""));
    
    JsonValue *pos = json_object_get(params, "position");
    if (!pos) { lsp_free(*uri); return -1; }
    *line = json_object_get_int(pos, "line", 0);
    *col = json_object_get_int(pos, "character", 0);
    return 0;
}

/* ---- Initialize Response Builder ---- */

/*
 * @brief 构建 initialize 方法的响应结果
 * @param server LSP服务器
 * @return 初始化结果JSON值
 */
static JsonValue *build_initialize_result(LspServer *srv) {
    JsonValue *result = json_new_object();
    
    /* Server capabilities */
    JsonValue *caps = json_new_object();
    
    /* Text document sync (full) */
    JsonValue *tds = json_new_object();
    json_object_set(tds, "openClose", json_new_bool(1));
    json_object_set(tds, "change", json_new_number(1)); /* 1=Full */
    json_object_set(tds, "willSave", json_new_bool(0));
    json_object_set(tds, "willSaveWaitUntil", json_new_bool(0));
    json_object_set(tds, "save", json_new_bool(1));
    json_object_set(caps, "textDocumentSync", tds);
    
    /* Completion provider */
    if (srv->capabilities.completion) {
        JsonValue *comp = json_new_object();
        json_object_set(comp, "triggerCharacters", NULL);
        json_object_set(comp, "resolveProvider", json_new_bool(0));
        json_object_set(caps, "completionProvider", comp);
    }
    
    /* Hover provider */
    if (srv->capabilities.hover) {
        json_object_set(caps, "hoverProvider", json_new_bool(1));
    }
    
    /* Definition provider */
    if (srv->capabilities.definition) {
        json_object_set(caps, "definitionProvider", json_new_bool(1));
    }
    
    /* Type definition provider */
    if (srv->capabilities.type_definition) {
        json_object_set(caps, "typeDefinitionProvider", json_new_bool(1));
    }
    
    /* Implementation provider */
    if (srv->capabilities.implementation) {
        json_object_set(caps, "implementationProvider", json_new_bool(1));
    }
    
    /* References provider */
    if (srv->capabilities.references) {
        json_object_set(caps, "referencesProvider", json_new_bool(1));
    }
    
    /* Document highlight */
    if (srv->capabilities.document_highlight) {
        json_object_set(caps, "documentHighlightProvider", json_new_bool(1));
    }
    
    /* Document symbol */
    if (srv->capabilities.document_symbol) {
        json_object_set(caps, "documentSymbolProvider", json_new_bool(1));
    }
    
    /* Signature help */
    if (srv->capabilities.signature_help) {
        JsonValue *sig = json_new_object();
        json_object_set(sig, "triggerCharacters", NULL);
        json_object_set(caps, "signatureHelpProvider", sig);
    }
    
    /* Rename */
    if (srv->capabilities.rename) {
        json_object_set(caps, "renameProvider", json_new_bool(1));
    }
    
    /* Prepare rename */
    if (srv->capabilities.prepare_rename) {
        json_object_set(caps, "prepareRenameProvider", json_new_bool(1));
    }
    
    /* Formatting */
    if (srv->capabilities.formatting) {
        json_object_set(caps, "documentFormattingProvider", json_new_bool(1));
    }
    
    /* Folding range */
    json_object_set(caps, "foldingRangeProvider", json_new_bool(1));
    
    /* Workspace symbol */
    if (srv->capabilities.workspace_symbol) {
        json_object_set(caps, "workspaceSymbolProvider", json_new_bool(1));
    }
    
    /* Selection range */
    if (srv->capabilities.selection_range) {
        json_object_set(caps, "selectionRangeProvider", json_new_bool(1));
    }
    
    /* Linked editing range */
    if (srv->capabilities.linked_editing) {
        json_object_set(caps, "linkedEditingRangeProvider", json_new_bool(1));
    }
    
    /* Semantic tokens */
    JsonValue *semtok = json_new_object();
    json_object_set(semtok, "legend", NULL);
    /* Build legend with standard LSP semantic token types */
    JsonValue *legend = json_new_object();
    JsonValue *token_types = json_new_array();
    /* Standard LSP semantic token types: namespace,type,class,enum,interface,struct,typeParameter,parameter,variable,property,enumMember,event,function,method,macro,keyword,modifier,comment,string,number,regexp,operator,decorator */
    const char *types[] = {"namespace","type","class","enum","interface","struct",
        "typeParameter","parameter","variable","property","enumMember","event",
        "function","method","macro","keyword","modifier","comment","string",
        "number","regexp","operator","decorator",NULL};
    for (int t = 0; types[t]; t++)
        json_array_add(token_types, json_new_string(types[t]));
    json_object_set(legend, "tokenTypes", token_types);
    /* Add token modifiers */
    JsonValue *token_modifiers = json_new_array();
    const char *modifiers[] = {"declaration","definition","readonly","static",
        "deprecated","abstract","async","modification","documentation","defaultLibrary",NULL};
    for (int m = 0; modifiers[m]; m++)
        json_array_add(token_modifiers, json_new_string(modifiers[m]));
    json_object_set(legend, "tokenModifiers", token_modifiers);
    json_object_set(semtok, "legend", legend);
    json_object_set(semtok, "full", json_new_bool(1));
    json_object_set(semtok, "range", json_new_bool(1));
    json_object_set(semtok, "delta", json_new_bool(1));
    json_object_set(caps, "semanticTokensProvider", semtok);
    
    /* Code action */
    if (srv->capabilities.code_action) {
        json_object_set(caps, "codeActionProvider", json_new_bool(1));
    }
    
    /* Diagnostic */
    if (srv->capabilities.diagnostic) {
        JsonValue *diag_provider = json_new_object();
        json_object_set(diag_provider, "interFileDependencies", json_new_bool(0));
        json_object_set(diag_provider, "workspaceDiagnostics", json_new_bool(0));
        json_object_set(caps, "diagnosticProvider", diag_provider);
    }
    
    /* Declaration provider */
    if (srv->capabilities.declaration) {
        json_object_set(caps, "declarationProvider", json_new_bool(1));
    }
    
    /* Code Lens provider */
    if (srv->capabilities.code_lens) {
        JsonValue *cl = json_new_object();
        json_object_set(cl, "resolveProvider", json_new_bool(0));
        json_object_set(caps, "codeLensProvider", cl);
    }
    
    /* Document Link provider */
    if (srv->capabilities.document_link) {
        JsonValue *dl = json_new_object();
        json_object_set(dl, "resolveProvider", json_new_bool(0));
        json_object_set(caps, "documentLinkProvider", dl);
    }
    
    /* Inlay Hint provider */
    if (srv->capabilities.inlay_hint) {
        JsonValue *ih = json_new_object();
        json_object_set(ih, "resolveProvider", json_new_bool(0));
        json_object_set(caps, "inlayHintProvider", ih);
    }
    
    /* Call Hierarchy provider */
    if (srv->capabilities.call_hierarchy) {
        json_object_set(caps, "callHierarchyProvider", json_new_bool(1));
    }
    
    /* Type Hierarchy provider */
    if (srv->capabilities.type_hierarchy) {
        json_object_set(caps, "typeHierarchyProvider", json_new_bool(1));
    }
    
    /* Color Presentation provider */
    if (srv->capabilities.color_presentation) {
        json_object_set(caps, "colorProvider", json_new_bool(1));
    }
    
    /* Moniker provider */
    if (srv->capabilities.moniker) {
        json_object_set(caps, "monikerProvider", json_new_bool(1));
    }
    
    /* On Type Formatting provider */
    if (srv->capabilities.on_type_formatting) {
        JsonValue *otf = json_new_object();
        json_object_set(otf, "firstTriggerCharacter", json_new_string("\n"));
        JsonValue *more_triggers = json_new_array();
        json_array_add(more_triggers, json_new_string("d"));
        json_object_set(otf, "moreTriggerCharacter", more_triggers);
        json_object_set(caps, "documentOnTypeFormattingProvider", otf);
    }
    
    /* Range Formatting provider */
    if (srv->capabilities.range_formatting) {
        json_object_set(caps, "documentRangeFormattingProvider", json_new_bool(1));
    }
    
    json_object_set(result, "capabilities", caps);
    
    /* Server info */
    JsonValue *server_info = json_new_object();
    json_object_set(server_info, "name", json_new_string("lxclua-lsp"));
    json_object_set(server_info, "version", json_new_string("1.0.0"));
    json_object_set(result, "serverInfo", server_info);
    
    return result;
}

/* ---- Completion Response Builder ---- */

/*
 * @brief 构建补全响应
 * @param items 补全项数组
 * @param n_items 补全项数量
 * @param is_incomplete 是否不完整（需重新请求）
 * @return 补全列表JSON值
 */
static JsonValue *build_completion_list(LspCompletionItem *items, int n_items, int is_incomplete) {
    JsonValue *list = json_new_object();
    json_object_set(list, "isIncomplete", json_new_bool(is_incomplete));
    
    JsonValue *arr = json_new_array();
    for (int i = 0; i < n_items; i++) {
        JsonValue *item = json_new_object();
        json_object_set(item, "label", json_new_string(items[i].label));
        json_object_set(item, "kind", json_new_number(items[i].kind));
        if (items[i].detail && *items[i].detail)
            json_object_set(item, "detail", json_new_string(items[i].detail));
        if (items[i].documentation && *items[i].documentation)
            json_object_set(item, "documentation", json_new_string(items[i].documentation));
        if (items[i].insert_text && strcmp(items[i].insert_text, items[i].label) != 0) {
            json_object_set(item, "insertText", json_new_string(items[i].insert_text));
            if (items[i].insert_text_format == INSERT_TEXT_SNIPPET)
                json_object_set(item, "insertTextFormat", json_new_number(INSERT_TEXT_SNIPPET));
        }
        /* Sort text for ordering */
        char sort_text[32];
        snprintf(sort_text, sizeof(sort_text), "%05d_%s", 99999 - items[i].sort_text_priority, items[i].label);
        json_object_set(item, "sortText", json_new_string(sort_text));
        json_array_add(arr, item);
    }
    json_object_set(list, "items", arr);
    return list;
}

/*
 * @brief 构建悬停响应
 * @param text Markdown内容
 * @return LSP Hover JSON值
 */
static JsonValue *build_hover_result(const char *text) {
    if (!text) return json_new_null();
    JsonValue *hover = json_new_object();
    JsonValue *contents = json_new_object();
    json_object_set(contents, "kind", json_new_string("markdown"));
    json_object_set(contents, "value", json_new_string(text));
    json_object_set(hover, "contents", contents);
    return hover;
}

/*
 * @brief 构建定义位置响应
 * @param uri 文件URI
 * @param line 行号
 * @param col 列号
 * @return LSP Location JSON值
 */
static JsonValue *build_location(const char *uri, int line, int col) {
    JsonValue *loc = json_new_object();
    json_object_set(loc, "uri", json_new_string(uri));
    JsonValue *range = json_new_object();
    JsonValue *start = json_new_object();
    json_object_set(start, "line", json_new_number(line));
    json_object_set(start, "character", json_new_number(col));
    JsonValue *end = json_new_object();
    json_object_set(end, "line", json_new_number(line));
    json_object_set(end, "character", json_new_number(col + 1));
    json_object_set(range, "start", start);
    json_object_set(range, "end", end);
    json_object_set(loc, "range", range);
    return loc;
}

/*
 * @brief 构建诊断响应
 * @param diags 诊断数组
 * @param ndiags 数量
 * @return LSP Diagnostic JSON数组
 */
static JsonValue *build_diagnostics(LspDiagnostic *diags, int ndiags) {
    JsonValue *arr = json_new_array();
    for (int i = 0; i < ndiags; i++) {
        JsonValue *d = json_new_object();
        JsonValue *range = json_new_object();
        JsonValue *start = json_new_object();
        json_object_set(start, "line", json_new_number(diags[i].line_start));
        json_object_set(start, "character", json_new_number(diags[i].col_start));
        JsonValue *end = json_new_object();
        json_object_set(end, "line", json_new_number(diags[i].line_end));
        json_object_set(end, "character", json_new_number(diags[i].col_end));
        json_object_set(range, "start", start);
        json_object_set(range, "end", end);
        json_object_set(d, "range", range);
        json_object_set(d, "severity", json_new_number(diags[i].severity));
        json_object_set(d, "message", json_new_string(diags[i].message));
        json_object_set(d, "source", json_new_string(diags[i].source ? diags[i].source : "lxclua-lsp"));
        json_array_add(arr, d);
    }
    return arr;
}

/*
 * @brief 处理 textDocument/didOpen 通知
 * @param srv 服务器
 * @param params 参数
 */
static void handle_did_open(LspServer *srv, JsonValue *params) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return;
    const char *uri = json_object_get_string(td, "uri", "");
    const char *text = json_object_get_string(td, "text", "");
    int version = json_object_get_int(td, "version", 0);
    lsp_doc_open(srv, uri, text, version);
}

/*
 * @brief 处理 textDocument/didChange 通知
 * @param srv 服务器
 * @param params 参数
 */
static void handle_did_change(LspServer *srv, JsonValue *params) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return;
    const char *uri = json_object_get_string(td, "uri", "");
    int version = json_object_get_int(td, "version", 0);
    JsonValue *changes = json_object_get(params, "contentChanges");
    if (!changes || changes->type != JSON_ARRAY || changes->as.arr.count == 0) return;
    JsonValue *first_change = changes->as.arr.items[0];
    const char *new_text = json_object_get_string(first_change, "text", "");
    lsp_doc_change(srv, uri, new_text, version);
}

/*
 * @brief 处理 textDocument/didClose 通知
 * @param srv 服务器
 * @param params 参数
 */
static void handle_did_close(LspServer *srv, JsonValue *params) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return;
    const char *uri = json_object_get_string(td, "uri", "");
    lsp_doc_close(srv, uri);
}

/* ---- Main Message Dispatcher ---- */

/*
 * @brief 处理LSP请求/通知，生成响应
 * @param server LSP服务器指针
 * @param method LSP方法名
 * @param id 请求ID（通知时为NULL）
 * @param params 请求参数
 * @return 响应消息，通知时返回NULL
 */
static JsonRpcMessage *dispatch_request(LspServer *srv, const char *method, JsonValue *id, JsonValue *params) {
    if (!method) return jrpc_new_error_resp(id, JRPC_INVALID_REQUEST, "No method specified");
    
    int is_notification = (id == NULL);
    
    /* ---- Lifecycle ---- */
    if (strcmp(method, LSP_METHOD_INITIALIZE) == 0) {
        if (is_notification) return jrpc_new_error_resp(id, JRPC_INVALID_REQUEST, "initialize must be a request");
        srv->initialized = 1;
        srv->capabilities.hover = 1;
        srv->capabilities.completion = 1;
        srv->capabilities.completion_trigger = 1;
        srv->capabilities.definition = 1;
        srv->capabilities.type_definition = 1;
        srv->capabilities.implementation = 1;
        srv->capabilities.references = 1;
        srv->capabilities.document_highlight = 1;
        srv->capabilities.document_symbol = 1;
        srv->capabilities.signature_help = 1;
        srv->capabilities.rename = 1;
        srv->capabilities.prepare_rename = 1;
        srv->capabilities.formatting = 1;
        srv->capabilities.code_action = 1;
        srv->capabilities.diagnostic = 1;
        srv->capabilities.folding_range = 1;
        srv->capabilities.semantic_tokens = 1;
        srv->capabilities.workspace_symbol = 1;
        srv->capabilities.selection_range = 1;
        srv->capabilities.linked_editing = 1;
        srv->capabilities.declaration = 1;
        srv->capabilities.code_lens = 1;
        srv->capabilities.document_link = 1;
        srv->capabilities.inlay_hint = 1;
        srv->capabilities.call_hierarchy = 1;
        srv->capabilities.type_hierarchy = 1;
        srv->capabilities.color_presentation = 1;
        srv->capabilities.moniker = 1;
        srv->capabilities.on_type_formatting = 1;
        srv->capabilities.range_formatting = 1;
        JsonValue *init_result = build_initialize_result(srv);
        JsonRpcMessage *resp = jrpc_new_response(id, init_result);
        json_free(init_result);
        return resp;
    }
    if (strcmp(method, LSP_METHOD_INITIALIZED) == 0) { return NULL; /* Notification, no response */ }
    if (strcmp(method, LSP_METHOD_SHUTDOWN) == 0) {
        srv->shutdown = 1;
        return jrpc_new_response(id, json_new_null());
    }
    if (strcmp(method, LSP_METHOD_EXIT) == 0) {
        srv->exit_requested = 1;
        return NULL;
    }
    
    /* Non-initialized rejections */
    if (!srv->initialized) {
        return jrpc_new_error_resp(id, JRPC_SERVER_NOT_INIT, "Server not initialized");
    }
    
    /* ---- Document Sync ---- */
    if (strcmp(method, LSP_METHOD_DID_OPEN) == 0) {
        handle_did_open(srv, params);
        return NULL; /* Notification */
    }
    if (strcmp(method, LSP_METHOD_DID_CHANGE) == 0) {
        handle_did_change(srv, params);
        return NULL; /* Notification */
    }
    if (strcmp(method, LSP_METHOD_DID_CLOSE) == 0) {
        handle_did_close(srv, params);
        return NULL; /* Notification */
    }
    
    /* ---- Document Save ---- */
    if (strcmp(method, LSP_METHOD_DID_SAVE) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        /* Re-parse the document on save */
        LspDocument *doc = lsp_doc_find(srv, uri);
        if (doc) lsp_doc_parse(doc, 1);
        return NULL; /* Notification */
    }
    
    /* ---- Workspace ---- */
    if (strcmp(method, LSP_METHOD_WORKSPACE_CFG_CHG) == 0) {
        /* Store settings for later use - handled as notification */
        return NULL; /* Notification */
    }
    
    /* ---- Completion ---- */
    if (strcmp(method, LSP_METHOD_COMPLETION) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspCompletionItem *items = NULL;
        int n_items = 0;
        if (doc) n_items = lsp_completion(doc, line, col, &items);
        JsonValue *result = build_completion_list(items, n_items, 0);
        lsp_free(uri);
        if (items) {
            for (int i = 0; i < n_items; i++) {
                lsp_free(items[i].label);
                lsp_free(items[i].detail);
                lsp_free(items[i].documentation);
                lsp_free(items[i].insert_text);
            }
            lsp_free(items);
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Hover ---- */
    if (strcmp(method, LSP_METHOD_HOVER) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *hover_text = doc ? lsp_hover(doc, line, col) : NULL;
        JsonValue *result = build_hover_result(hover_text);
        lsp_free(uri); lsp_free(hover_text);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Definition ---- */
    if (strcmp(method, LSP_METHOD_DEFINITION) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int def_line = -1, def_col = -1;
        char *def_uri = NULL;
        char *sym_name = doc ? lsp_get_symbol_at(doc, line, col, &def_line, &def_col, &def_uri) : NULL;
        JsonValue *result;
        if (def_line >= 0 && def_uri) {
            result = build_location(def_uri, def_line, def_col);
        } else if (sym_name) {
            /* Symbol found but we couldn't resolve the exact position */
            result = json_new_null();
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(def_uri); lsp_free(sym_name);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- References ---- */
    if (strcmp(method, LSP_METHOD_REFERENCES) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *ref_lines = NULL, *ref_cols = NULL;
        int nrefs = 0;
        if (doc) lsp_find_references(doc, line, col, &ref_lines, &ref_cols, &nrefs);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nrefs; i++) {
            JsonValue *loc = build_location(uri, ref_lines[i], ref_cols[i]);
            json_array_add(arr, loc);
        }
        lsp_free(uri); lsp_free(ref_lines); lsp_free(ref_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Document Highlight ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_HIGHLIGHT) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *hl_kinds = NULL, *hl_lines = NULL, *hl_cols = NULL;
        int nhl = 0;
        if (doc) lsp_document_highlight(doc, line, col, &hl_kinds, &hl_lines, &hl_cols, &nhl);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nhl; i++) {
            JsonValue *hl = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(hl_lines[i]));
            json_object_set(s, "character", json_new_number(hl_cols[i]));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(hl_lines[i]));
            json_object_set(e, "character", json_new_number(hl_cols[i] + 1));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(hl, "range", rng);
            json_object_set(hl, "kind", json_new_number(hl_kinds[i]));
            json_array_add(arr, hl);
        }
        lsp_free(uri); lsp_free(hl_kinds); lsp_free(hl_lines); lsp_free(hl_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Document Symbol ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_SYMBOL) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspSymbol **syms = NULL;
        int nsyms = doc ? lsp_document_symbol(doc, &syms) : 0;
        
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nsyms; i++) {
            JsonValue *sym = json_new_object();
            json_object_set(sym, "name", json_new_string(syms[i]->name));
            json_object_set(sym, "kind", json_new_number(syms[i]->kind));
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(syms[i]->line));
            json_object_set(s, "character", json_new_number(syms[i]->col));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(syms[i]->end_line));
            json_object_set(e, "character", json_new_number(syms[i]->end_col));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(sym, "range", rng);
            /* selectionRange needs its own copy to avoid double-free */
            {
                JsonValue *sel_rng = json_new_object();
                JsonValue *ss = json_new_object();
                json_object_set(ss, "line", json_new_number(syms[i]->line));
                json_object_set(ss, "character", json_new_number(syms[i]->col));
                JsonValue *se = json_new_object();
                json_object_set(se, "line", json_new_number(syms[i]->end_line));
                json_object_set(se, "character", json_new_number(syms[i]->end_col));
                json_object_set(sel_rng, "start", ss);
                json_object_set(sel_rng, "end", se);
                json_object_set(sym, "selectionRange", sel_rng);
            }
            if (syms[i]->detail) json_object_set(sym, "detail", json_new_string(syms[i]->detail));
            json_array_add(arr, sym);
            /* Free symbol */
            lsp_free(syms[i]->name);
            lsp_free(syms[i]->detail);
            lsp_free(syms[i]->documentation);
            lsp_free(syms[i]);
        }
        lsp_free(syms);
        
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Signature Help ---- */
    if (strcmp(method, LSP_METHOD_SIGNATURE_HELP) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *sig = doc ? lsp_signature_help(doc, line, col) : NULL;
        JsonValue *result;
        if (sig) { result = json_parse(sig, (int)strlen(sig)); }
        else { result = json_new_null(); }
        lsp_free(uri); lsp_free(sig);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Rename ---- */
    if (strcmp(method, LSP_METHOD_RENAME) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        const char *new_name = json_object_get_string(params, "newName", "");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int nrenames = doc ? lsp_rename(doc, line, col, new_name) : 0;
        /* Return workspace edit with all renames */
        JsonValue *result = json_new_object();
        JsonValue *changes = json_new_object();
        JsonValue *edits = json_new_array();
        if (doc && doc->tokens) {
            int offset = lsp_linecol_to_offset(doc->text, line, col);
            int wstart, wend;
            char *word = lsp_get_word_at(doc->text, offset, &wstart, &wend);
            if (word) {
                for (int i = 0; i < doc->ntokens; i++) {
                    if (doc->tokens[i].type == TOK_NAME && strcmp(doc->tokens[i].text, word) == 0) {
                        JsonValue *te = json_new_object();
                        JsonValue *rng = json_new_object();
                        JsonValue *s = json_new_object();
                        json_object_set(s, "line", json_new_number(doc->tokens[i].line));
                        json_object_set(s, "character", json_new_number(doc->tokens[i].col));
                        JsonValue *e = json_new_object();
                        json_object_set(e, "line", json_new_number(doc->tokens[i].line));
                        json_object_set(e, "character", json_new_number(doc->tokens[i].col + doc->tokens[i].len));
                        json_object_set(rng, "start", s);
                        json_object_set(rng, "end", e);
                        json_object_set(te, "range", rng);
                        json_object_set(te, "newText", json_new_string(new_name));
                        json_array_add(edits, te);
                    }
                }
                lsp_free(word);
            }
        }
        json_object_set(changes, uri, edits);
        json_object_set(result, "changes", changes);
        lsp_free(uri);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Prepare Rename ---- */
    if (strcmp(method, LSP_METHOD_PREPARE_RENAME) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int rline, rcol, rend_line, rend_col;
        int valid = doc ? lsp_prepare_rename(doc, line, col, &rline, &rcol, &rend_line, &rend_col) : 0;
        JsonValue *result;
        if (valid) {
            result = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(rline));
            json_object_set(s, "character", json_new_number(rcol));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(rend_line));
            json_object_set(e, "character", json_new_number(rend_col));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(result, "range", rng);
            json_object_set(result, "placeholder", json_new_string(doc ? doc->text + lsp_linecol_to_offset(doc->text, rline, rcol) : ""));
        } else {
            result = json_new_null();
        }
        lsp_free(uri);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Type Definition ---- */
    if (strcmp(method, LSP_METHOD_TYPE_DEFINITION) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int def_line = -1, def_col = -1;
        char *def_uri = NULL;
        int found = doc ? lsp_type_definition(doc, line, col, &def_line, &def_col, &def_uri) : 0;
        JsonValue *result;
        if (found && def_line >= 0) {
            result = build_location(def_uri ? def_uri : uri, def_line, def_col);
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(def_uri);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Implementation ---- */
    if (strcmp(method, LSP_METHOD_IMPLEMENTATION) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *impl_lines = NULL, *impl_cols = NULL;
        int nimpl = 0;
        if (doc) lsp_find_implementation(doc, line, col, &impl_lines, &impl_cols, &nimpl);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nimpl; i++) {
            JsonValue *loc = build_location(uri, impl_lines[i], impl_cols[i]);
            json_array_add(arr, loc);
        }
        lsp_free(uri); lsp_free(impl_lines); lsp_free(impl_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Formatting ---- */
    if (strcmp(method, LSP_METHOD_FORMATTING) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        int tab_size = json_object_get_int(params, "tabSize", 4);
        int insert_spaces = json_object_get_bool(params, "insertSpaces", 1);
        JsonValue *options = json_object_get(params, "options");
        if (options) {
            tab_size = json_object_get_int(options, "tabSize", tab_size);
            insert_spaces = json_object_get_bool(options, "insertSpaces", insert_spaces);
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *formatted = doc ? lsp_format(doc, tab_size, insert_spaces) : NULL;
        JsonValue *result;
        if (formatted) {
            JsonValue *arr = json_new_array();
            JsonValue *te = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(0));
            json_object_set(s, "character", json_new_number(0));
            JsonValue *e = json_new_object();
            int last_line = doc ? doc->nlines - 1 : 0;
            json_object_set(e, "line", json_new_number(last_line));
            json_object_set(e, "character", json_new_number(0));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(te, "range", rng);
            json_object_set(te, "newText", json_new_string(formatted));
            json_array_add(arr, te);
            result = arr;
        } else {
            result = json_new_null();
        }
        lsp_free(formatted);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Diagnostic ---- */
    if (strcmp(method, LSP_METHOD_DIAGNOSTIC) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspDiagnostic *diags = NULL;
        int ndiags = doc ? lsp_diagnostic(doc, &diags) : 0;
        JsonValue *result = json_new_object();
        json_object_set(result, "kind", json_new_string("full"));
        json_object_set(result, "resultId", json_new_string("1"));
        JsonValue *diag_arr = build_diagnostics(diags, ndiags);
        json_object_set(result, "items", diag_arr);
        if (diags) {
            for (int i = 0; i < ndiags; i++) {
                lsp_free(diags[i].message);
                lsp_free(diags[i].source);
            }
            lsp_free(diags);
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Folding Range ---- */
    if (strcmp(method, LSP_METHOD_FOLDING_RANGE) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *starts = NULL, *ends = NULL;
        int nfolds = 0;
        if (doc) lsp_folding_range(doc, &starts, &ends, &nfolds);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nfolds; i++) {
            JsonValue *fold = json_new_object();
            json_object_set(fold, "startLine", json_new_number(starts[i]));
            json_object_set(fold, "endLine", json_new_number(ends[i]));
            json_array_add(arr, fold);
        }
        lsp_free(starts); lsp_free(ends);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Semantic Tokens ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        
        JsonValue *result = json_new_object();
        JsonValue *data_arr = json_new_array();
        
        if (doc && doc->tokens && doc->ntokens > 0) {
            int prev_line = 0, prev_col = 0;
            for (int i = 0; i < doc->ntokens; i++) {
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_EOS) continue;
                if (!tok->text) continue;
                
                int token_type = -1;
                int modifiers = 0;
                
                switch (tok->type) {
                    case TOK_NAME:
                        token_type = 8;
                        for (int j = 0; j < doc->nvars; j++) {
                            if (doc->vars[j].name && tok->text &&
                                strcmp(tok->text, doc->vars[j].name) == 0 &&
                                doc->vars[j].def_line == tok->line && doc->vars[j].def_col == tok->col) {
                                int k = doc->vars[j].kind;
                                if (k == SYMBOL_FUNCTION) token_type = 12;
                                else if (k == SYMBOL_METHOD) token_type = 13;
                                else if (k == SYMBOL_STRUCT) token_type = 5;
                                else if (k == SYMBOL_ENUM) token_type = 3;
                                else if (k == SYMBOL_NAMESPACE) token_type = 0;
                                else if (k == SYMBOL_CLASS) token_type = 2;
                                else if (k == SYMBOL_CONSTANT) token_type = 8;
                                else if (k == SYMBOL_FIELD) token_type = 9;
                                break;
                            }
                        }
                        break;
                    case TOK_STRING: case TOK_INTERPSTRING: case TOK_RAWSTRING:
                        token_type = 18; break;
                    case TOK_COMMENT: case TOK_MCOMMENT:
                        token_type = 17; break;
                    case TOK_INT: case TOK_FLT:
                        token_type = 19; break;
                    default:
                        if (tok->type == TOK_TYPE_INT || tok->type == TOK_TYPE_FLOAT || tok->type == TOK_BOOL ||
                            tok->type == TOK_CHAR || tok->type == TOK_DOUBLE || tok->type == TOK_LONG ||
                            tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM)
                            token_type = 1;
                        else if (tok->type >= TOK_AND && tok->type <= TOK_LET)
                            token_type = 15;
                        else if (tok->type >= TOK_IDIV && tok->type <= TOK_DOLLDOLL)
                            token_type = 21;
                        break;
                }
                
                if (token_type < 0) continue;
                
                int d_line = tok->line - prev_line;
                int d_col = (d_line == 0) ? tok->col - prev_col : tok->col;
                
                json_array_add(data_arr, json_new_number(d_line));
                json_array_add(data_arr, json_new_number(d_col));
                json_array_add(data_arr, json_new_number(tok->len));
                json_array_add(data_arr, json_new_number(token_type));
                json_array_add(data_arr, json_new_number(modifiers));
                
                prev_line = tok->line;
                prev_col = tok->col;
            }
        }
        
        json_object_set(result, "data", data_arr);
        /* 生成 resultId 并缓存用于 delta 增量检测 */
        {
            char rid[128];
            snprintf(rid, sizeof(rid), "%s_v%d_t%d", uri, doc ? doc->version : 0, doc ? doc->ntokens : 0);
            json_object_set(result, "resultId", json_new_string(rid));
            lsp_free(srv->prev_semantic_result_id);
            srv->prev_semantic_result_id = lsp_strdup(rid);
            srv->prev_semantic_version = doc ? doc->version : 0;
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Code Action ---- */
    if (strcmp(method, LSP_METHOD_CODE_ACTION) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        JsonValue *range = json_object_get(params, "range");
        int line = range ? json_object_get_int(json_object_get(range, "start"), "line", 0) : 0;
        int col = range ? json_object_get_int(json_object_get(range, "start"), "character", 0) : 0;
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspDiagnostic *diag_list = NULL;
        int ndiag = 0;
        if (doc) lsp_code_action(doc, line, col, &diag_list, &ndiag);
        /* 为每个诊断生成 CodeAction */
        JsonValue *arr = json_new_array();
        for (int i = 0; i < ndiag; i++) {
            JsonValue *action = json_new_object();
            /* title */
            char title[512];
            const char *prefix = diag_list[i].severity == SEVERITY_ERROR ? "Fix error" : 
                                 diag_list[i].severity == SEVERITY_WARNING ? "Fix warning" : "Fix";
            snprintf(title, sizeof(title), "%s: %s", prefix, diag_list[i].message);
            json_object_set(action, "title", json_new_string(title));
            json_object_set(action, "kind", json_new_string("quickfix"));
            
            /* diagnostics 关联 */
            JsonValue *diags_arr = json_new_array();
            JsonValue *d = json_new_object();
            JsonValue *dr = json_new_object();
            JsonValue *ds = json_new_object();
            json_object_set(ds, "line", json_new_number(diag_list[i].line_start));
            json_object_set(ds, "character", json_new_number(diag_list[i].col_start));
            JsonValue *de = json_new_object();
            json_object_set(de, "line", json_new_number(diag_list[i].line_end));
            json_object_set(de, "character", json_new_number(diag_list[i].col_end));
            json_object_set(dr, "start", ds);
            json_object_set(dr, "end", de);
            json_object_set(d, "range", dr);
            json_object_set(d, "message", json_new_string(diag_list[i].message));
            json_object_set(d, "severity", json_new_number(diag_list[i].severity));
            if (diag_list[i].source)
                json_object_set(d, "source", json_new_string(diag_list[i].source));
            json_array_add(diags_arr, d);
            json_object_set(action, "diagnostics", diags_arr);
            
            /* edit: 标记问题范围，newText 留空让用户手动修复 */
            JsonValue *edit = json_new_object();
            JsonValue *changes = json_new_object();
            JsonValue *edits_arr = json_new_array();
            JsonValue *te = json_new_object();
            /* 复用上面创建的 dr range */
            JsonValue *te_r = json_new_object();
            JsonValue *te_s = json_new_object();
            json_object_set(te_s, "line", json_new_number(diag_list[i].line_start));
            json_object_set(te_s, "character", json_new_number(diag_list[i].col_start));
            JsonValue *te_e = json_new_object();
            json_object_set(te_e, "line", json_new_number(diag_list[i].line_end));
            json_object_set(te_e, "character", json_new_number(diag_list[i].col_end));
            json_object_set(te_r, "start", te_s);
            json_object_set(te_r, "end", te_e);
            json_object_set(te, "range", te_r);
            json_object_set(te, "newText", json_new_string(""));
            json_array_add(edits_arr, te);
            json_object_set(changes, uri, edits_arr);
            json_object_set(edit, "changes", changes);
            json_object_set(action, "edit", edit);
            
            json_array_add(arr, action);
        }
        /* 清理 */
        for (int i = 0; i < ndiag; i++) {
            lsp_free(diag_list[i].message);
            lsp_free(diag_list[i].source);
        }
        lsp_free(diag_list);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Cancel Request ---- */
    if (strcmp(method, LSP_METHOD_CANCEL_REQUEST) == 0) {
        /* Stub: acknowledge cancel, no actual cancellation implementation */
        return NULL;
    }
    
    /* ---- Workspace Symbol ---- */
    if (strcmp(method, LSP_METHOD_WORKSPACE_SYMBOL) == 0) {
        const char *query = json_object_get_string(params, "query", "");
        LspSymbol **syms = NULL;
        int nsyms = lsp_workspace_symbol(srv, query, &syms);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nsyms; i++) {
            JsonValue *sym = json_new_object();
            json_object_set(sym, "name", json_new_string(syms[i]->name));
            json_object_set(sym, "kind", json_new_number(syms[i]->kind));
            /* Location */
            JsonValue *loc = json_new_object();
            json_object_set(loc, "uri", json_new_string(syms[i]->detail ? syms[i]->detail : "")); /* detail contains URI */
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(syms[i]->line));
            json_object_set(s, "character", json_new_number(syms[i]->col));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(syms[i]->end_line));
            json_object_set(e, "character", json_new_number(syms[i]->end_col));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(loc, "range", rng);
            json_object_set(sym, "location", loc);
            json_array_add(arr, sym);
            lsp_free(syms[i]->name);
            lsp_free(syms[i]->detail);
            lsp_free(syms[i]->documentation);
            lsp_free(syms[i]);
        }
        lsp_free(syms);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Selection Range ---- */
    if (strcmp(method, LSP_METHOD_SELECTION_RANGE) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        JsonValue *positions_arr = json_object_get(params, "positions");
        LspDocument *doc = lsp_doc_find(srv, uri);
        
        int npos = positions_arr ? json_array_len(positions_arr) : 0;
        int *plines = NULL, *pcols = NULL;
        if (npos > 0) {
            plines = (int *)lsp_alloc(npos * sizeof(int));
            pcols = (int *)lsp_alloc(npos * sizeof(int));
            for (int i = 0; i < npos; i++) {
                JsonValue *pos = json_array_get(positions_arr, i);
                plines[i] = pos ? json_object_get_int(pos, "line", 0) : 0;
                pcols[i] = pos ? json_object_get_int(pos, "character", 0) : 0;
            }
        }
        
        int *starts = NULL, *ends = NULL;
        int nresult = doc ? lsp_selection_range(doc, npos, plines, pcols, &starts, &ends) : 0;
        
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nresult; i++) {
            JsonValue *sr = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(starts[i]));
            json_object_set(s, "character", json_new_number(0));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(ends[i]));
            json_object_set(e, "character", json_new_number(0));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(sr, "range", rng);
            /* Parent: previous item if any */
            if (i > 0) {
                JsonValue *parent_rng = json_new_object();
                JsonValue *ps = json_new_object();
                json_object_set(ps, "line", json_new_number(starts[i-1]));
                json_object_set(ps, "character", json_new_number(0));
                JsonValue *pe = json_new_object();
                json_object_set(pe, "line", json_new_number(ends[i-1]));
                json_object_set(pe, "character", json_new_number(0));
                json_object_set(parent_rng, "start", ps);
                json_object_set(parent_rng, "end", pe);
                json_object_set(sr, "parent", parent_rng);
            }
            json_array_add(arr, sr);
        }
        
        lsp_free(plines); lsp_free(pcols);
        lsp_free(starts); lsp_free(ends);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Completion Resolve ---- */
    if (strcmp(method, LSP_METHOD_COMPLETION_RESOLVE) == 0) {
        /* For now, just return the item as-is (no additional resolution) */
        JsonValue *result;
        json_deep_copy(&result, params);
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* ---- Linked Editing Range ---- */
    if (strcmp(method, LSP_METHOD_LINKED_EDITING) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *llines = NULL, *lcols = NULL;
        int nlinked = 0;
        int has_result = doc ? lsp_linked_editing_range(doc, line, col, &llines, &lcols, &nlinked) : 0;
        JsonValue *result;
        if (has_result && nlinked > 0) {
            result = json_new_object();
            JsonValue *ranges = json_new_array();
            for (int i = 0; i < nlinked; i++) {
                JsonValue *rng = json_new_object();
                JsonValue *s = json_new_object();
                json_object_set(s, "line", json_new_number(llines[i]));
                json_object_set(s, "character", json_new_number(lcols[i]));
                JsonValue *e = json_new_object();
                /* Find the length of the word at this position */
                int woffset = lsp_linecol_to_offset(doc->text, llines[i], lcols[i]);
                int ws, we;
                char *w = lsp_get_word_at(doc->text, woffset, &ws, &we);
                int wlen = w ? (we - ws) : 1;
                lsp_free(w);
                json_object_set(e, "line", json_new_number(llines[i]));
                json_object_set(e, "character", json_new_number(lcols[i] + wlen));
                json_object_set(rng, "start", s);
                json_object_set(rng, "end", e);
                json_array_add(ranges, rng);
            }
            json_object_set(result, "ranges", ranges);
            json_object_set(result, "wordPattern", json_new_string("[a-zA-Z_][a-zA-Z0-9_]*"));
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(llines); lsp_free(lcols);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Declaration ---- */
    if (strcmp(method, LSP_METHOD_DECLARATION) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        int def_line = -1, def_col = -1;
        char *def_uri = NULL;
        int found = doc ? lsp_declaration(doc, line, col, &def_line, &def_col, &def_uri) : 0;
        JsonValue *result;
        if (found && def_line >= 0 && def_uri) {
            result = build_location(def_uri, def_line, def_col);
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(def_uri);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Code Lens ---- */
    if (strcmp(method, LSP_METHOD_CODE_LENS) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *cl_lines = NULL, *cl_cols = NULL;
        char **cl_titles = NULL, **cl_commands = NULL;
        int ncl = 0;
        if (doc) lsp_code_lens(doc, &cl_lines, &cl_cols, &cl_titles, &cl_commands, &ncl);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < ncl; i++) {
            JsonValue *cl = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(cl_lines[i]));
            json_object_set(s, "character", json_new_number(cl_cols[i]));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(cl_lines[i]));
            json_object_set(e, "character", json_new_number(cl_cols[i] + 1));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(cl, "range", rng);
            if (cl_commands[i]) {
                JsonValue *cmd = json_new_object();
                json_object_set(cmd, "title", json_new_string(cl_titles[i] ? cl_titles[i] : ""));
                json_object_set(cmd, "command", json_new_string(cl_commands[i]));
                json_object_set(cl, "command", cmd);
            }
            json_array_add(arr, cl);
            lsp_free(cl_titles[i]);
            lsp_free(cl_commands[i]);
        }
        lsp_free(cl_lines); lsp_free(cl_cols);
        lsp_free(cl_titles); lsp_free(cl_commands);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Code Lens Resolve ---- */
    if (strcmp(method, LSP_METHOD_CODE_LENS_RESOLVE) == 0) {
        JsonValue *result;
        json_deep_copy(&result, params);
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* ---- Document Link ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_LINK) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        int *dl_sl = NULL, *dl_sc = NULL, *dl_el = NULL, *dl_ec = NULL;
        char **dl_targets = NULL;
        int ndl = 0;
        if (doc) lsp_document_link(doc, &dl_sl, &dl_sc, &dl_el, &dl_ec, &dl_targets, &ndl);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < ndl; i++) {
            JsonValue *link = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(dl_sl[i]));
            json_object_set(s, "character", json_new_number(dl_sc[i]));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(dl_el[i]));
            json_object_set(e, "character", json_new_number(dl_ec[i]));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(link, "range", rng);
            json_object_set(link, "target", json_new_string(dl_targets[i] ? dl_targets[i] : ""));
            json_array_add(arr, link);
            lsp_free(dl_targets[i]);
        }
        lsp_free(dl_sl); lsp_free(dl_sc); lsp_free(dl_el); lsp_free(dl_ec);
        lsp_free(dl_targets);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Inlay Hint ---- */
    if (strcmp(method, LSP_METHOD_INLAY_HINT) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        JsonValue *range = json_object_get(params, "range");
        int start_line = range ? json_object_get_int(json_object_get(range, "start"), "line", 0) : 0;
        int end_line = range ? json_object_get_int(json_object_get(range, "end"), "line", 100) : 100;
        LspDocument *doc = lsp_doc_find(srv, uri);
        char **ih_labels = NULL;
        int *ih_lines = NULL, *ih_cols = NULL;
        int nih = 0;
        if (doc) lsp_inlay_hint(doc, start_line, end_line, &ih_labels, &ih_lines, &ih_cols, &nih);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nih; i++) {
            JsonValue *hint = json_new_object();
            JsonValue *pos = json_new_object();
            json_object_set(pos, "line", json_new_number(ih_lines[i]));
            json_object_set(pos, "character", json_new_number(ih_cols[i]));
            json_object_set(hint, "position", pos);
            json_object_set(hint, "label", json_new_string(ih_labels[i] ? ih_labels[i] : ""));
            json_object_set(hint, "kind", json_new_number(1)); /* 1=Type, 2=Parameter */
            json_array_add(arr, hint);
            lsp_free(ih_labels[i]);
        }
        lsp_free(ih_labels); lsp_free(ih_lines); lsp_free(ih_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Call Hierarchy Prepare ---- */
    if (strcmp(method, LSP_METHOD_CALL_HIERARCHY_PREPARE) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *ch_name = NULL;
        int ch_line, ch_col;
        int found = doc ? lsp_prepare_call_hierarchy(doc, line, col, &ch_name, &ch_line, &ch_col) : 0;
        JsonValue *result;
        if (found) {
            JsonValue *arr = json_new_array();
            JsonValue *item = json_new_object();
            json_object_set(item, "name", json_new_string(ch_name));
            json_object_set(item, "kind", json_new_number(SYMBOL_FUNCTION));
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(ch_line));
            json_object_set(s, "character", json_new_number(ch_col));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(ch_line));
            json_object_set(e, "character", json_new_number(ch_col + (ch_name ? (int)strlen(ch_name) : 0)));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(item, "range", rng);
            json_object_set(item, "selectionRange", rng);
            json_object_set(item, "uri", json_new_string(uri));
            json_array_add(arr, item);
            result = arr;
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(ch_name);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Call Hierarchy Incoming ---- */
    if (strcmp(method, LSP_METHOD_CALL_HIERARCHY_INCOMING) == 0) {
        JsonValue *item = json_object_get(params, "item");
        int ch_line = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "line", 0) : 0;
        int ch_col = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "character", 0) : 0;
        const char *item_uri = item ? json_object_get_string(item, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, item_uri);
        int *from_lines = NULL, *from_cols = NULL, *to_lines = NULL, *to_cols = NULL;
        int nch = 0;
        if (doc) lsp_call_hierarchy_incoming(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);
        JsonValue *arr = json_new_array();
        if (nch > 0 && from_lines && from_cols && to_lines && to_cols) {
            for (int i = 0; i < nch; i++) {
                JsonValue *ch = json_new_object();
                /* from item: 从文本中提取真实函数名 */
                JsonValue *from = json_new_object();
                int caller_offset = lsp_linecol_to_offset(doc->text, from_lines[i], from_cols[i]);
                int cws, cwe;
                char *caller_name = lsp_get_word_at(doc->text, caller_offset, &cws, &cwe);
                json_object_set(from, "name", json_new_string(caller_name ? caller_name : "function"));
                if (caller_name) lsp_free(caller_name);
                json_object_set(from, "kind", json_new_number(SYMBOL_FUNCTION));
                JsonValue *frng = json_new_object();
                JsonValue *fs = json_new_object();
                json_object_set(fs, "line", json_new_number(from_lines[i]));
                json_object_set(fs, "character", json_new_number(from_cols[i]));
                JsonValue *fe = json_new_object();
                json_object_set(fe, "line", json_new_number(from_lines[i]));
                json_object_set(fe, "character", json_new_number(from_cols[i] + 1));
                json_object_set(frng, "start", fs);
                json_object_set(frng, "end", fe);
                json_object_set(from, "range", frng);
                json_object_set(from, "selectionRange", frng);
                json_object_set(from, "uri", json_new_string(item_uri));
                json_object_set(ch, "from", from);
                /* from ranges */
                JsonValue *from_ranges = json_new_array();
                JsonValue *trng = json_new_object();
                JsonValue *ts = json_new_object();
                json_object_set(ts, "line", json_new_number(to_lines[i]));
                json_object_set(ts, "character", json_new_number(to_cols[i]));
                JsonValue *te = json_new_object();
                json_object_set(te, "line", json_new_number(to_lines[i]));
                json_object_set(te, "character", json_new_number(to_cols[i] + 1));
                json_object_set(trng, "start", ts);
                json_object_set(trng, "end", te);
                json_array_add(from_ranges, trng);
                json_object_set(ch, "fromRanges", from_ranges);
                json_array_add(arr, ch);
            }
            lsp_free(from_lines); lsp_free(from_cols);
            lsp_free(to_lines); lsp_free(to_cols);
        }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Call Hierarchy Outgoing ---- */
    if (strcmp(method, LSP_METHOD_CALL_HIERARCHY_OUTGOING) == 0) {
        JsonValue *item = json_object_get(params, "item");
        int ch_line = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "line", 0) : 0;
        int ch_col = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "character", 0) : 0;
        const char *item_uri = item ? json_object_get_string(item, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, item_uri);
        int *from_lines = NULL, *from_cols = NULL, *to_lines = NULL, *to_cols = NULL;
        int nch = 0;
        if (doc) lsp_call_hierarchy_outgoing(doc, ch_line, ch_col, &from_lines, &from_cols, &to_lines, &to_cols, &nch);
        JsonValue *arr = json_new_array();
        if (nch > 0 && from_lines && from_cols && to_lines && to_cols) {
            for (int i = 0; i < nch; i++) {
                JsonValue *ch = json_new_object();
                /* to item: 从文本中提取被调用函数名 */
                JsonValue *to = json_new_object();
                int callee_offset = lsp_linecol_to_offset(doc->text, to_lines[i], to_cols[i]);
                int cws, cwe;
                char *callee_name = lsp_get_word_at(doc->text, callee_offset, &cws, &cwe);
                json_object_set(to, "name", json_new_string(callee_name ? callee_name : "function"));
                if (callee_name) lsp_free(callee_name);
                json_object_set(to, "kind", json_new_number(SYMBOL_FUNCTION));
                JsonValue *trng = json_new_object();
                JsonValue *ts = json_new_object();
                json_object_set(ts, "line", json_new_number(to_lines[i]));
                json_object_set(ts, "character", json_new_number(to_cols[i]));
                JsonValue *te = json_new_object();
                json_object_set(te, "line", json_new_number(to_lines[i]));
                json_object_set(te, "character", json_new_number(to_cols[i] + 1));
                json_object_set(trng, "start", ts);
                json_object_set(trng, "end", te);
                json_object_set(to, "range", trng);
                json_object_set(to, "selectionRange", trng);
                json_object_set(to, "uri", json_new_string(item_uri));
                json_object_set(ch, "to", to);
                /* from ranges */
                JsonValue *from_ranges = json_new_array();
                JsonValue *frng = json_new_object();
                JsonValue *fs = json_new_object();
                json_object_set(fs, "line", json_new_number(from_lines[i]));
                json_object_set(fs, "character", json_new_number(from_cols[i]));
                JsonValue *fe = json_new_object();
                json_object_set(fe, "line", json_new_number(from_lines[i]));
                json_object_set(fe, "character", json_new_number(from_cols[i] + 1));
                json_object_set(frng, "start", fs);
                json_object_set(frng, "end", fe);
                json_array_add(from_ranges, frng);
                json_object_set(ch, "fromRanges", from_ranges);
                json_array_add(arr, ch);
            }
            lsp_free(from_lines); lsp_free(from_cols);
            lsp_free(to_lines); lsp_free(to_cols);
        }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Type Hierarchy Prepare ---- */
    if (strcmp(method, LSP_METHOD_TYPE_HIERARCHY_PREPARE) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *th_name = NULL;
        int th_line, th_col;
        int found = doc ? lsp_prepare_type_hierarchy(doc, line, col, &th_name, &th_line, &th_col) : 0;
        JsonValue *result;
        if (found) {
            JsonValue *arr = json_new_array();
            JsonValue *item = json_new_object();
            json_object_set(item, "name", json_new_string(th_name));
            json_object_set(item, "kind", json_new_number(SYMBOL_STRUCT));
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(th_line));
            json_object_set(s, "character", json_new_number(th_col));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(th_line));
            json_object_set(e, "character", json_new_number(th_col + (th_name ? (int)strlen(th_name) : 0)));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(item, "range", rng);
            json_object_set(item, "selectionRange", rng);
            json_object_set(item, "uri", json_new_string(uri));
            json_array_add(arr, item);
            result = arr;
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(th_name);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Type Hierarchy Supertypes ---- */
    if (strcmp(method, LSP_METHOD_TYPE_HIERARCHY_SUPERTYPES) == 0) {
        JsonValue *item = json_object_get(params, "item");
        int th_line = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "line", 0) : 0;
        int th_col = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "character", 0) : 0;
        const char *item_uri = item ? json_object_get_string(item, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, item_uri);
        char **sp_names = NULL;
        int *sp_lines = NULL, *sp_cols = NULL;
        int nsp = 0;
        if (doc) lsp_type_hierarchy_supertypes(doc, th_line, th_col, &sp_names, &sp_lines, &sp_cols, &nsp);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nsp; i++) {
            JsonValue *ti = json_new_object();
            json_object_set(ti, "name", json_new_string(sp_names[i]));
            json_object_set(ti, "kind", json_new_number(SYMBOL_STRUCT));
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(sp_lines[i]));
            json_object_set(s, "character", json_new_number(sp_cols[i]));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(sp_lines[i]));
            json_object_set(e, "character", json_new_number(sp_cols[i] + (sp_names[i] ? (int)strlen(sp_names[i]) : 0)));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(ti, "range", rng);
            json_object_set(ti, "selectionRange", rng);
            json_object_set(ti, "uri", json_new_string(item_uri));
            json_array_add(arr, ti);
            lsp_free(sp_names[i]);
        }
        lsp_free(sp_names); lsp_free(sp_lines); lsp_free(sp_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Type Hierarchy Subtypes ---- */
    if (strcmp(method, LSP_METHOD_TYPE_HIERARCHY_SUBTYPES) == 0) {
        JsonValue *item = json_object_get(params, "item");
        int th_line = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "line", 0) : 0;
        int th_col = item ? json_object_get_int(json_object_get(json_object_get(item, "range"), "start"), "character", 0) : 0;
        const char *item_uri = item ? json_object_get_string(item, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, item_uri);
        char **sb_names = NULL;
        int *sb_lines = NULL, *sb_cols = NULL;
        int nsb = 0;
        if (doc) lsp_type_hierarchy_subtypes(doc, th_line, th_col, &sb_names, &sb_lines, &sb_cols, &nsb);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nsb; i++) {
            JsonValue *ti = json_new_object();
            json_object_set(ti, "name", json_new_string(sb_names[i]));
            json_object_set(ti, "kind", json_new_number(SYMBOL_STRUCT));
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(sb_lines[i]));
            json_object_set(s, "character", json_new_number(sb_cols[i]));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(sb_lines[i]));
            json_object_set(e, "character", json_new_number(sb_cols[i] + (sb_names[i] ? (int)strlen(sb_names[i]) : 0)));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(ti, "range", rng);
            json_object_set(ti, "selectionRange", rng);
            json_object_set(ti, "uri", json_new_string(item_uri));
            json_array_add(arr, ti);
            lsp_free(sb_names[i]);
        }
        lsp_free(sb_names); lsp_free(sb_lines); lsp_free(sb_cols);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Color Presentation ---- */
    if (strcmp(method, LSP_METHOD_COLOR_PRESENTATION) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        if (!td) return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument");
        char *uri = lsp_strdup(json_object_get_string(td, "uri", ""));
        int line = 0, col = 0;
        /* colorPresentation 用 range 而非 position */
        JsonValue *range = json_object_get(params, "range");
        if (range) {
            JsonValue *start = json_object_get(range, "start");
            if (start) {
                line = json_object_get_int(start, "line", 0);
                col = json_object_get_int(start, "character", 0);
            }
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char **cp_labels = NULL;
        int ncp = 0;
        if (doc) lsp_color_presentation(doc, line, col, &cp_labels, &ncp);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < ncp; i++) {
            JsonValue *cp = json_new_object();
            json_object_set(cp, "label", json_new_string(cp_labels[i]));
            json_array_add(arr, cp);
            lsp_free(cp_labels[i]);
        }
        lsp_free(cp_labels);
        lsp_free(uri);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Moniker ---- */
    if (strcmp(method, LSP_METHOD_MONIKER) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        LspDocument *doc = lsp_doc_find(srv, uri);
        char **mk_schemes = NULL, **mk_ids = NULL;
        int nmk = 0;
        if (doc) lsp_moniker(doc, line, col, &mk_schemes, &mk_ids, &nmk);
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nmk; i++) {
            JsonValue *mk = json_new_object();
            json_object_set(mk, "scheme", json_new_string(mk_schemes[i]));
            json_object_set(mk, "identifier", json_new_string(mk_ids[i]));
            json_array_add(arr, mk);
            lsp_free(mk_schemes[i]);
            lsp_free(mk_ids[i]);
        }
        lsp_free(mk_schemes); lsp_free(mk_ids);
        lsp_free(uri);
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- On Type Formatting ---- */
    if (strcmp(method, LSP_METHOD_ON_TYPE_FORMATTING) == 0) {
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0)
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        const char *ch = json_object_get_string(params, "ch", "");
        int tab_size = json_object_get_int(params, "tabSize", 4);
        int insert_spaces = json_object_get_bool(params, "insertSpaces", 1);
        JsonValue *options = json_object_get(params, "options");
        if (options) {
            tab_size = json_object_get_int(options, "tabSize", tab_size);
            insert_spaces = json_object_get_bool(options, "insertSpaces", insert_spaces);
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *otf_result = doc ? lsp_on_type_formatting(doc, line, col, ch, tab_size, insert_spaces) : NULL;
        JsonValue *result;
        if (otf_result) {
            result = json_parse(otf_result, (int)strlen(otf_result));
        } else {
            result = json_new_null();
        }
        lsp_free(uri); lsp_free(otf_result);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Range Formatting ---- */
    if (strcmp(method, LSP_METHOD_RANGE_FORMATTING) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        JsonValue *range = json_object_get(params, "range");
        int start_line = range ? json_object_get_int(json_object_get(range, "start"), "line", 0) : 0;
        int start_col = range ? json_object_get_int(json_object_get(range, "start"), "character", 0) : 0;
        int end_line = range ? json_object_get_int(json_object_get(range, "end"), "line", 0) : 0;
        int end_col = range ? json_object_get_int(json_object_get(range, "end"), "character", 0) : 0;
        int tab_size = json_object_get_int(params, "tabSize", 4);
        int insert_spaces = json_object_get_bool(params, "insertSpaces", 1);
        JsonValue *options = json_object_get(params, "options");
        if (options) {
            tab_size = json_object_get_int(options, "tabSize", tab_size);
            insert_spaces = json_object_get_bool(options, "insertSpaces", insert_spaces);
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *formatted = doc ? lsp_range_formatting(doc, start_line, start_col, end_line, end_col, tab_size, insert_spaces) : NULL;
        JsonValue *result;
        if (formatted) {
            JsonValue *arr = json_new_array();
            JsonValue *te = json_new_object();
            JsonValue *rng = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(start_line));
            json_object_set(s, "character", json_new_number(start_col));
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(end_line));
            json_object_set(e, "character", json_new_number(end_col));
            json_object_set(rng, "start", s);
            json_object_set(rng, "end", e);
            json_object_set(te, "range", rng);
            json_object_set(te, "newText", json_new_string(formatted));
            json_array_add(arr, te);
            result = arr;
        } else {
            result = json_new_null();
        }
        lsp_free(formatted);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Will Save (notification) ---- */
    if (strcmp(method, LSP_METHOD_WILL_SAVE) == 0 ||
        strcmp(method, LSP_METHOD_WILL_SAVE_WAIT) == 0) {
        return NULL; /* Notification: inform server before save */
    }
    
    /* ---- Semantic Tokens Range ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS_RANGE) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        JsonValue *result = json_new_object();
        JsonValue *data_arr = json_new_array();
        JsonValue *range = json_object_get(params, "range");
        int start_line = range ? json_object_get_int(json_object_get(range, "start"), "line", 0) : 0;
        int end_line = range ? json_object_get_int(json_object_get(range, "end"), "line", 0) : (doc && doc->nlines ? doc->nlines - 1 : 0);
        
        if (doc && doc->tokens && doc->ntokens > 0) {
            int prev_line = 0, prev_col = 0;
            for (int i = 0; i < doc->ntokens; i++) {
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_EOS || !tok->text) continue;
                if (tok->line < start_line || tok->line > end_line) continue;  /* 过滤范围外 */
                
                int token_type = -1;
                int modifiers = 0;
                
                switch (tok->type) {
                    case TOK_NAME:
                        token_type = 8;
                        for (int j = 0; j < doc->nvars; j++) {
                            if (doc->vars[j].name && tok->text &&
                                strcmp(tok->text, doc->vars[j].name) == 0 &&
                                doc->vars[j].def_line == tok->line && doc->vars[j].def_col == tok->col) {
                                int k = doc->vars[j].kind;
                                if (k == SYMBOL_FUNCTION) token_type = 12;
                                else if (k == SYMBOL_METHOD) token_type = 13;
                                else if (k == SYMBOL_STRUCT) token_type = 5;
                                else if (k == SYMBOL_ENUM) token_type = 3;
                                else if (k == SYMBOL_NAMESPACE) token_type = 0;
                                else if (k == SYMBOL_CLASS) token_type = 2;
                                else if (k == SYMBOL_CONSTANT) token_type = 8;
                                else if (k == SYMBOL_FIELD) token_type = 9;
                                break;
                            }
                        }
                        break;
                    case TOK_STRING: case TOK_INTERPSTRING: case TOK_RAWSTRING:
                        token_type = 18; break;
                    case TOK_COMMENT: case TOK_MCOMMENT:
                        token_type = 17; break;
                    case TOK_INT: case TOK_FLT:
                        token_type = 19; break;
                    default:
                        if (tok->type == TOK_TYPE_INT || tok->type == TOK_TYPE_FLOAT || tok->type == TOK_BOOL ||
                            tok->type == TOK_CHAR || tok->type == TOK_DOUBLE || tok->type == TOK_LONG ||
                            tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM)
                            token_type = 1;
                        else if (tok->type >= TOK_AND && tok->type <= TOK_LET)
                            token_type = 15;
                        else if (tok->type >= TOK_IDIV && tok->type <= TOK_DOLLDOLL)
                            token_type = 21;
                        break;
                }
                
                if (token_type < 0) continue;
                
                int d_line = tok->line - prev_line;
                int d_col = (d_line == 0) ? tok->col - prev_col : tok->col;
                json_array_add(data_arr, json_new_number(d_line));
                json_array_add(data_arr, json_new_number(d_col));
                json_array_add(data_arr, json_new_number(tok->len));
                json_array_add(data_arr, json_new_number(token_type));
                json_array_add(data_arr, json_new_number(0));
                prev_line = tok->line; prev_col = tok->col;
            }
        }
        json_object_set(result, "data", data_arr);
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Semantic Tokens Delta ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS_DELTA) == 0) {
        /* 增量语义标记：基于 previousResultId 和文档版本实现增量检测 */
        const char *prev_result_id = json_object_get_string(params, "previousResultId", "");
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        
        /* 检查是否有缓存的上一轮结果 */
        if (srv->prev_semantic_result_id && srv->prev_semantic_result_id[0] &&
            strcmp(prev_result_id, srv->prev_semantic_result_id) == 0 &&
            doc && doc->version == srv->prev_semantic_version) {
            /* 文档版本和 resultId 均未变化，返回空增量编辑 */
            JsonValue *result = json_new_object();
            json_object_set(result, "resultId", json_new_string(prev_result_id));
            json_object_set(result, "edits", json_new_array());
            JsonRpcMessage *resp = jrpc_new_response(id, result);
            json_free(result);
            return resp;
        }
        
        /* 版本变化或首次请求，无法计算增量，返回 null 让客户端回退到 full */
        JsonRpcMessage *resp = jrpc_new_response(id, json_new_null());
        return resp;
    }
    
    /* ---- Document Link Resolve ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_LINK_RESOLVE) == 0) {
        /* 解析文档链接的target，原样返回 */
        JsonValue *result = json_new_object();
        /* 复制 range */
        JsonValue *rng = json_object_get(params, "range");
        if (rng) {
            JsonValue *nr = json_new_object();
            JsonValue *s = json_new_object();
            json_object_set(s, "line", json_new_number(json_object_get_int(json_object_get(rng, "start"), "line", 0)));
            json_object_set(s, "character", json_new_number(json_object_get_int(json_object_get(rng, "start"), "character", 0)));
            json_object_set(nr, "start", s);
            JsonValue *e = json_new_object();
            json_object_set(e, "line", json_new_number(json_object_get_int(json_object_get(rng, "end"), "line", 0)));
            json_object_set(e, "character", json_new_number(json_object_get_int(json_object_get(rng, "end"), "character", 0)));
            json_object_set(nr, "end", e);
            json_object_set(result, "range", nr);
        }
        json_object_set(result, "target", json_new_string(json_object_get_string(params, "target", "")));
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Inlay Hint Resolve ---- */
    if (strcmp(method, LSP_METHOD_INLAY_HINT_RESOLVE) == 0) {
        /* 解析嵌入提示，原样返回 */
        JsonValue *result = json_new_object();
        JsonValue *pos = json_object_get(params, "position");
        if (pos) {
            JsonValue *np = json_new_object();
            json_object_set(np, "line", json_new_number(json_object_get_int(pos, "line", 0)));
            json_object_set(np, "character", json_new_number(json_object_get_int(pos, "character", 0)));
            json_object_set(result, "position", np);
        }
        json_object_set(result, "label", json_new_string(json_object_get_string(params, "label", "")));
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Workspace Will File Operations (notification) ---- */
    if (strcmp(method, LSP_METHOD_WILL_CREATE_FILES) == 0 ||
        strcmp(method, LSP_METHOD_WILL_RENAME_FILES) == 0 ||
        strcmp(method, LSP_METHOD_WILL_DELETE_FILES) == 0) {
        return NULL; /* Notification: acknowledge, no veto */
    }
    
    /* ---- Execute Command ---- */
    if (strcmp(method, LSP_METHOD_EXECUTE_COMMAND) == 0) {
        /* 执行工作区命令：当前无支持的命令，返回空对象 */
        JsonValue *result = json_new_object();
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Workspace notifications ---- */
    if (strcmp(method, LSP_METHOD_DID_CREATE_FILES) == 0 ||
        strcmp(method, LSP_METHOD_DID_RENAME_FILES) == 0 ||
        strcmp(method, LSP_METHOD_DID_DELETE_FILES) == 0 ||
        strcmp(method, LSP_METHOD_WATCHED_FILES_CHG) == 0) {
        return NULL; /* Notification */
    }
    
    /* Unknown method */
    return jrpc_new_error_resp(id, JRPC_METHOD_NOT_FOUND, lsp_fmt("Method not found: %s", method));
}

/* ---- Public API ---- */

/*
 * @brief 初始化LSP服务器
 * @return 服务器指针
 */
void *lsp_init(void) {
    LspServer *srv = (LspServer *)lsp_alloc(sizeof(LspServer));
    srv->initialized = 0;
    srv->shutdown = 0;
    srv->exit_requested = 0;
    srv->ndocs = 0;
    memset(&srv->capabilities, 0, sizeof(srv->capabilities));
    memset(&srv->client_caps, 0, sizeof(srv->client_caps));
    srv->next_request_id = 1;
    srv->prev_semantic_version = 0;
    srv->prev_semantic_result_id = NULL;
    return srv;
}

/*
 * @brief 处理传入的LSP消息
 * @param server 服务器指针
 * @param data 原始输入数据
 * @param len 数据长度
 * @param response 输出-响应字符串（调用者需要释放）
 * @return 1表示有响应需要发送，0表示无响应，-1表示需要退出
 */
int lsp_handle_message(void *server, const char *data, int len, char **response) {
    *response = NULL;
    LspServer *srv = (LspServer *)server;
    
    JsonRpcMessage *msg = jrpc_parse(data, len);
    if (!msg) {
        /* Parse error - send error response */
        JsonValue *null_id = json_new_null();
        JsonRpcMessage *err = jrpc_new_error_resp(null_id, JRPC_PARSE_ERROR, "Parse error");
        *response = jrpc_serialize(err);
        json_free(null_id);
        jrpc_free(err);
        return 1;
    }
    
    JsonRpcMessage *resp = NULL;
    
    if (jrpc_is_response(msg)) {
        /* Received a response (we don't track pending requests for now) */
        jrpc_free(msg);
        return 0;
    }
    
    fprintf(stderr, "[LSP-DBG] method='%s' is_notif=%d\n", msg->method ? msg->method : "(null)", jrpc_is_notification(msg));
    fflush(stderr);
    
    if (jrpc_is_notification(msg)) {
        dispatch_request(srv, msg->method, NULL, msg->params);
    } else {
        resp = dispatch_request(srv, msg->method, msg->id, msg->params);
        if (resp) {
            *response = jrpc_serialize(resp);
            jrpc_free(resp);
        }
    }
    
    jrpc_free(msg);
    return *response != NULL ? 1 : 0;
}

/*
 * @brief 关闭LSP服务器
 * @param server 服务器指针
 */
void lsp_shutdown(void *server) {
    LspServer *srv = (LspServer *)server;
    srv->shutdown = 1;
}

/*
 * @brief 释放LSP服务器所有资源
 * @param server 服务器指针
 */
void lsp_srv_free(void *server) {
    LspServer *srv = (LspServer *)server;
    if (!srv) return;
    for (int i = 0; i < srv->ndocs; i++) {
        /* lsp_doc_free would free each document */
        lsp_free(srv->docs[i]->uri);
        lsp_free(srv->docs[i]->text);
        lsp_free(srv->docs[i]->line_offsets);
        if (srv->docs[i]->tokens) lsp_lex_free(srv->docs[i]->tokens, srv->docs[i]->ntokens);
        lsp_free(srv->docs[i]);
    }
    lsp_free(srv->prev_semantic_result_id);
    lsp_free(srv);
}