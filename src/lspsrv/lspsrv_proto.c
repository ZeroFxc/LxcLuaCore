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

/* ---- Semantic Tokens Common Helpers ---- */

static const char *semantic_extract_uri_short(const char *uri) {
    if (!uri || !*uri) return "doc";
    const char *slash = strrchr(uri, '/');
    const char *bslash = strrchr(uri, '\\');
    const char *best = slash;
    if (bslash && (!best || bslash > best)) best = bslash;
    return best ? (best + 1) : uri;
}

static void semantic_gen_result_id(LspServer *srv, const char *uri, int version) {
    const char *uri_short = semantic_extract_uri_short(uri);
    size_t need = 32 + strlen(uri_short) + 20 + 20;
    srv->semantic_result_id = (char *)lsp_realloc(srv->semantic_result_id, need);
    sprintf(srv->semantic_result_id, "stk-%s-%d-%u", uri_short, version, ++srv->semantic_token_seq);
}

static int semantic_token_to_type(LspDocument *doc, LspToken *tok, int *out_modifiers) {
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
                    else if (k == SYMBOL_INTERFACE) token_type = 4;
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
                tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM ||
                tok->type == TOK_CLASS || tok->type == TOK_INTERFACE || tok->type == TOK_TRAIT)
                token_type = 1;
            else if (tok->type >= TOK_AND && tok->type <= TOK_USE)
                token_type = 15;
            else if (tok->type >= TOK_IDIV && tok->type <= TOK_DOLLDOLL)
                token_type = 21;
            break;
    }
    if (token_type < 0 || token_type > 23) {
        *out_modifiers = 0;
        return -1;
    }
    if (modifiers < 0) modifiers = 0;
    if (modifiers > 1023) modifiers = 1023;
    *out_modifiers = modifiers;
    return token_type;
}

static JsonValue *build_semantic_tokens_data(LspServer *srv, JsonValue *id, LspDocument *doc,
                                             int start_line_limit, int end_line_limit,
                                             int use_limits, int *out_count) {
    JsonValue *data_arr = json_new_array();
    int count = 0;
    *out_count = 0;
    if (!doc || !doc->tokens || doc->ntokens <= 0) {
        return data_arr;
    }
    int prev_line = 0, prev_col = 0;
    for (int i = 0; i < doc->ntokens; i++) {
        if ((i & 0x1ff) == 0 && lsp_is_cancelled(srv, id)) {
            json_free(data_arr);
            *out_count = -1;
            return NULL;
        }
        LspToken *tok = &doc->tokens[i];
        if (tok->type == TOK_EOS || !tok->text) continue;
        if (use_limits) {
            if (tok->line < start_line_limit || tok->line > end_line_limit) continue;
        }
        int modifiers = 0;
        int token_type = semantic_token_to_type(doc, tok, &modifiers);
        if (token_type < 0) continue;
        int d_line = tok->line - prev_line;
        int d_col = (d_line == 0) ? tok->col - prev_col : tok->col;
        json_array_add(data_arr, json_new_number(d_line));
        json_array_add(data_arr, json_new_number(d_col));
        json_array_add(data_arr, json_new_number(tok->len));
        json_array_add(data_arr, json_new_number(token_type));
        json_array_add(data_arr, json_new_number(modifiers));
        count++;
        prev_line = tok->line;
        prev_col = tok->col;
    }
    *out_count = count;
    return data_arr;
}

static void semantic_save_last_result_id(LspServer *srv) {
    lsp_free(srv->last_semantic_result_id);
    srv->last_semantic_result_id = srv->semantic_result_id ? lsp_strdup(srv->semantic_result_id) : NULL;
}

/* Helper to extract textDocument URI and position from params */
static int params_get_doc_pos(JsonValue *params, char **uri, int *line, int *col) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return -1;
    const char *uri_str = json_object_get_string(td, "uri", NULL);
    if (!uri_str || !*uri_str) return -1;
    *uri = lsp_strdup(uri_str);
    
    JsonValue *pos = json_object_get(params, "position");
    if (!pos) { lsp_free(*uri); return -1; }
    JsonValue *line_val = json_object_get(pos, "line");
    JsonValue *col_val = json_object_get(pos, "character");
    if (!line_val || !col_val) { lsp_free(*uri); return -1; }
    *line = json_object_get_int(pos, "line", 0);
    *col = json_object_get_int(pos, "character", 0);
    return 0;
}

/* Helper: check params has textDocument.uri (returns 0 if valid) */
static int params_check_textdocument_uri(JsonValue *params) {
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return -1;
    const char *uri = json_object_get_string(td, "uri", NULL);
    if (!uri || !*uri) return -1;
    return 0;
}

/* Helper: check params has position.line and position.character (returns 0 if valid) */
static int params_check_position(JsonValue *params) {
    JsonValue *pos = json_object_get(params, "position");
    if (!pos) return -1;
    JsonValue *line = json_object_get(pos, "line");
    JsonValue *ch = json_object_get(pos, "character");
    if (!line || !ch) return -1;
    return 0;
}

/* @since 3.17 ContentModified 跟踪：
 *   - 对每个带 textDocument.uri 的文档级请求，处理开始时调用 cm_begin 记录 uri + 起始版本
 *   - didChange 通知会 bump doc->version，因此若在处理请求期间客户端并发写，版本会增加
 *   - 处理结束时调用 cm_end_check：若版本仍一致返回 0，否则返回非 0，调用者应返回 ContentModified(-32801)
 * 串行处理模型下只需 1 组槽位；简化起见不维护 per-id 映射。 */
static void cm_begin(LspServer *srv, JsonValue *params) {
    lsp_free(srv->cm_uri);
    srv->cm_uri = NULL;
    srv->cm_version = -1;
    JsonValue *td = json_object_get(params, "textDocument");
    if (!td) return;
    const char *uri = json_object_get_string(td, "uri", NULL);
    if (!uri || !*uri) return;
    LspDocument *doc = lsp_doc_find(srv, uri);
    if (!doc) return;
    srv->cm_uri = lsp_strdup(uri);
    srv->cm_version = doc->version;
}
static int cm_end_check(LspServer *srv) {
    if (!srv->cm_uri || srv->cm_version < 0) return 0;
    LspDocument *doc = lsp_doc_find(srv, srv->cm_uri);
    if (!doc) { lsp_free(srv->cm_uri); srv->cm_uri = NULL; srv->cm_version = -1; return 0; }
    int modified = (doc->version != srv->cm_version);
    lsp_free(srv->cm_uri); srv->cm_uri = NULL; srv->cm_version = -1;
    return modified;
}

/* Helper: convert JsonValue id (string or number) to int hash/value */
static int json_id_to_int(JsonValue *id) {
    if (!id) return 0;
    if (id->type == JSON_NUMBER) return (int)id->as.num_val;
    if (id->type == JSON_STRING && id->as.str_val) {
        const char *s = id->as.str_val;
        unsigned int h = 2166136261u;
        while (*s) { h ^= (unsigned char)(*s++); h *= 16777619u; }
        return (int)h;
    }
    return 0;
}

/* Push cancel id into srv->cancel_ids[] with LRU eviction (max LSP_MAX_CANCEL_IDS=64) */
static void lsp_push_cancel_id(LspServer *srv, JsonValue *id) {
    if (!srv || !id) return;
    int key = json_id_to_int(id);
    if (key == 0) return;
    for (int i = 0; i < srv->cancel_count; i++) {
        if (srv->cancel_ids[i] == key) return;
    }
    if (srv->cancel_count >= LSP_MAX_CANCEL_IDS) {
        for (int i = 1; i < LSP_MAX_CANCEL_IDS; i++)
            srv->cancel_ids[i - 1] = srv->cancel_ids[i];
        srv->cancel_count = LSP_MAX_CANCEL_IDS - 1;
    }
    srv->cancel_ids[srv->cancel_count++] = key;
    if (key > srv->cancel_id_max) srv->cancel_id_max = key;
}

/* Check whether id is present in srv->cancel_ids[]; return 1 if cancelled, 0 otherwise */
int lsp_is_cancelled(LspServer *srv, JsonValue *id) {
    if (!srv || !id) return 0;
    int key = json_id_to_int(id);
    if (key == 0) return 0;
    for (int i = 0; i < srv->cancel_count; i++) {
        if (srv->cancel_ids[i] == key) return 1;
    }
    return 0;
}

/* Helper: convert JsonValue token (string or number) to int key */
static int json_token_to_int(JsonValue *token) {
    return json_id_to_int(token);
}

/* Store progress: find token in progress_ids[], update or append (max 64) */
static void lsp_store_progress(LspServer *srv, JsonValue *token, JsonValue *value) {
    if (!srv || !token) return;
    int key = json_token_to_int(token);
    char *serialized = value ? json_stringify(value) : NULL;
    int idx = -1;
    for (int i = 0; i < srv->progress_count; i++) {
        if (srv->progress_ids[i] == key) { idx = i; break; }
    }
    if (idx >= 0) {
        lsp_free(srv->progress_values[idx]);
        srv->progress_values[idx] = serialized;
    } else {
        if (srv->progress_count >= LSP_MAX_PROGRESS_IDS) {
            lsp_free(srv->progress_values[0]);
            for (int i = 1; i < LSP_MAX_PROGRESS_IDS; i++) {
                srv->progress_ids[i - 1] = srv->progress_ids[i];
                srv->progress_values[i - 1] = srv->progress_values[i];
            }
            srv->progress_count = LSP_MAX_PROGRESS_IDS - 1;
        }
        srv->progress_ids[srv->progress_count] = key;
        srv->progress_values[srv->progress_count] = serialized;
        srv->progress_count++;
    }
}

/* Build and return a window/logMessage notification JsonRpcMessage (caller sends/serializes).
 * Returns NULL on failure. Caller jrpc_free()'s the result. */
static JsonRpcMessage *lsp_make_log_message(int type, const char *message) {
    JsonValue *params = json_new_object();
    json_object_set(params, "type", json_new_number(type));
    json_object_set(params, "message", json_new_string(message ? message : ""));
    JsonRpcMessage *notif = jrpc_new_notification(LSP_METHOD_WINDOW_LOG, params);
    json_free(params);
    return notif;
}

/* ---- Enqueue a pre-serialized outbound notification frame onto
 *      srv->pending_notifications[] so main-loop can send.
 *      This is the ONLY mechanism for: publishDiagnostics / $/progress /
 *      window/logMessage / window/showMessage / ... (server -> client pushes). */
static int lsp_enqueue_notification(LspServer *srv, char *frame) {
    if (!srv || !frame) return -1;
    if (srv->n_pending_notifications >= LSP_MAX_PENDING_NOTIFICATIONS) {
        lsp_free(frame);
        return -1;
    }
    srv->pending_notifications[srv->n_pending_notifications++] = frame;
    return 0;
}

/* ---- @since 3.17: 向客户端发起一个 server->client 请求（如 workspace/xxx/refresh）。
 *      使用 next_server_request_id 自增作为 id；把序列化帧放入同一个
 *      pending_notifications 队列（主循环不区分 notif/req 直接发送即可）。
 *      不追踪客户端响应，若客户端返回 response 主循环根据 id 丢弃即可。*/
static int lsp_enqueue_server_request(LspServer *srv, const char *method, JsonValue *params) {
    if (!srv || !method) return -1;
    srv->next_server_request_id++;
    JsonValue *id_val = json_new_number(srv->next_server_request_id);
    JsonRpcMessage *req_msg = (JsonRpcMessage *)lsp_alloc(sizeof(JsonRpcMessage));
    if (!req_msg) {
        json_free(id_val);
        return -1;
    }
    memset(req_msg, 0, sizeof(JsonRpcMessage));
    req_msg->jsonrpc = lsp_strdup("2.0");
    req_msg->id = id_val;
    req_msg->method = lsp_strdup(method);
    if (params) {
        req_msg->params = params; /* 接管参数所有权，避免额外拷贝 */
    }
    char *frame = jrpc_serialize(req_msg);
    jrpc_free(req_msg);
    if (!frame) return -1;
    return lsp_enqueue_notification(srv, frame);
}

/* ---- @since 3.17: 公开接口 - 请求 client 刷新某类数据 ---- */
int lsp_request_refresh(LspServer *srv, const char *method) {
    if (!srv || !method) return -1;
    /* 只允许合法的 5 个 refresh 方法 */
    if (strcmp(method, LSP_METHOD_CODE_LENS_REFRESH) != 0 &&
        strcmp(method, LSP_METHOD_INLAY_HINT_REFRESH) != 0 &&
        strcmp(method, LSP_METHOD_INLINE_VALUE_REFRESH) != 0 &&
        strcmp(method, LSP_METHOD_SEMANTIC_TOKENS_REFRESH) != 0 &&
        strcmp(method, LSP_METHOD_DIAGNOSTIC_REFRESH) != 0) {
        return -1;
    }
    return lsp_enqueue_server_request(srv, method, NULL);
}

/* ---- @since 3.17: 公开接口 - 发送 telemetry/event 通知 ---- */
int lsp_send_telemetry(LspServer *srv, const char *data_json) {
    if (!srv) return -1;
    JsonValue *params = NULL;
    if (data_json && *data_json) {
        params = json_parse(data_json, (int)strlen(data_json));
    }
    /* 如果解析失败，用 null 作为参数 */
    if (!params) params = json_new_null();
    JsonRpcMessage *notif = jrpc_new_notification(LSP_METHOD_TELEMETRY_EVENT, params);
    json_free(params);
    if (!notif) return -1;
    char *frame = jrpc_serialize(notif);
    jrpc_free(notif);
    if (!frame) return -1;
    return lsp_enqueue_notification(srv, frame);
}

/* ---- @since 3.15: window/workDoneProgress/create (server->client 请求) + $/progress 通知 ---- */

/**
 * @brief 辅助：用 JSON 值构造 notification 并入队
 */
static int enqueue_notif_from_json(LspServer *srv, const char *method, JsonValue *params) {
    JsonRpcMessage *n = jrpc_new_notification(method, params);
    if (!n) return -1;
    char *frame = jrpc_serialize(n);
    jrpc_free(n);
    if (!frame) return -1;
    return lsp_enqueue_notification(srv, frame);
}

int lsp_work_done_progress_create(LspServer *srv, char **out_token, const char *title,
                                  int cancellable, const char *message, int percentage) {
    if (!srv) return -1;
    if (out_token) *out_token = NULL;
    /* 分配字符串 token：wdp-{progress_count}-{next_server_request_id} */
    if (srv->progress_count >= (int)(sizeof(srv->progress_ids)/sizeof(srv->progress_ids[0]))) return -1;
    char token_buf[64];
    snprintf(token_buf, sizeof(token_buf), "wdp-%d-%d", srv->progress_count + 1, srv->next_server_request_id);
    srv->progress_values[srv->progress_count] = lsp_strdup(token_buf);
    srv->progress_count++;

    /* 1) window/workDoneProgress/create 请求 params = { token: string } */
    JsonValue *params = json_new_object();
    json_object_set(params, "token", json_new_string(token_buf));
    int rc1 = lsp_enqueue_server_request(srv, LSP_METHOD_PROGRESS_START, params);
    json_free(params);
    if (rc1 != 0) return -1;

    /* 2) $/progress begin notification: { token, value: { kind:"begin", title, cancellable?, message?, percentage? } } */
    JsonValue *val = json_new_object();
    json_object_set(val, "kind", json_new_string("begin"));
    json_object_set(val, "title", json_new_string(title ? title : ""));
    if (cancellable) json_object_set(val, "cancellable", json_new_bool(1));
    if (message && *message) json_object_set(val, "message", json_new_string(message));
    if (percentage >= 0 && percentage <= 100) json_object_set(val, "percentage", json_new_number(percentage));
    JsonValue *bp = json_new_object();
    json_object_set(bp, "token", json_new_string(token_buf));
    json_object_set(bp, "value", val);
    int rc2 = enqueue_notif_from_json(srv, LSP_METHOD_PROGRESS_REPORT, bp);
    json_free(bp);
    if (out_token) *out_token = lsp_strdup(token_buf);
    return rc2;
}

int lsp_progress_report(LspServer *srv, const char *token, const char *kind,
                        const char *message, int percentage) {
    if (!srv || !token || !kind) return -1;
    JsonValue *val = json_new_object();
    json_object_set(val, "kind", json_new_string(kind));
    if (strcmp(kind, "end") == 0) {
        if (message && *message) json_object_set(val, "message", json_new_string(message));
    } else if (strcmp(kind, "report") == 0) {
        if (message && *message) json_object_set(val, "message", json_new_string(message));
        if (percentage >= 0 && percentage <= 100) json_object_set(val, "percentage", json_new_number(percentage));
    } else if (strcmp(kind, "begin") != 0) {
        json_free(val);
        return -1;
    }
    JsonValue *bp = json_new_object();
    json_object_set(bp, "token", json_new_string(token));
    json_object_set(bp, "value", val);
    int rc = enqueue_notif_from_json(srv, LSP_METHOD_PROGRESS_REPORT, bp);
    json_free(bp);
    return rc;
}

/* ---- Initialize Response Builder ---- */

/*
 * @brief 构建 initialize 方法的响应结果
 * @param server LSP服务器
 * @return 初始化结果JSON值
 */
static JsonValue *build_initialize_result(LspServer *srv) {
    JsonValue *result = json_new_object();
    JsonValue *caps = json_new_object();

    /* ---- 1. TextDocumentSyncOptions ---- */
    {
        JsonValue *tds = json_new_object();
        json_object_set(tds, "openClose", json_new_bool(1));
        json_object_set(tds, "change", json_new_number(1));
        json_object_set(tds, "willSave", json_new_bool(1));
        json_object_set(tds, "willSaveWaitUntil", json_new_bool(1));
        JsonValue *save_opt = json_new_object();
        json_object_set(save_opt, "includeText", json_new_bool(1));
        json_object_set(tds, "save", save_opt);
        json_object_set(caps, "textDocumentSync", tds);
    }

    /* ---- Completion provider (resolveProvider from capabilities) ---- */
    if (srv->capabilities.completion) {
        JsonValue *comp = json_new_object();
        json_object_set(comp, "triggerCharacters", NULL);
        json_object_set(comp, "resolveProvider", json_new_bool(srv->capabilities.completion_resolve));
        json_object_set(caps, "completionProvider", comp);
    }

    /* ---- Hover provider ---- */
    if (srv->capabilities.hover) {
        json_object_set(caps, "hoverProvider", json_new_bool(srv->capabilities.hover));
    }

    /* ---- Definition provider ---- */
    if (srv->capabilities.definition) {
        json_object_set(caps, "definitionProvider", json_new_bool(srv->capabilities.definition));
    }

    /* ---- Type definition provider ---- */
    if (srv->capabilities.type_definition) {
        json_object_set(caps, "typeDefinitionProvider", json_new_bool(srv->capabilities.type_definition));
    }

    /* ---- Implementation provider ---- */
    if (srv->capabilities.implementation) {
        json_object_set(caps, "implementationProvider", json_new_bool(srv->capabilities.implementation));
    }

    /* ---- References provider ---- */
    if (srv->capabilities.references) {
        json_object_set(caps, "referencesProvider", json_new_bool(srv->capabilities.references));
    }

    /* ---- Document highlight ---- */
    if (srv->capabilities.document_highlight) {
        json_object_set(caps, "documentHighlightProvider", json_new_bool(srv->capabilities.document_highlight));
    }

    /* ---- Document symbol ---- */
    if (srv->capabilities.document_symbol) {
        json_object_set(caps, "documentSymbolProvider", json_new_bool(srv->capabilities.document_symbol));
    }

    /* ---- Signature help (补 retriggerCharacters) ---- */
    if (srv->capabilities.signature_help) {
        JsonValue *sig = json_new_object();
        json_object_set(sig, "triggerCharacters", NULL);
        json_object_set(sig, "retriggerCharacters", NULL);
        json_object_set(caps, "signatureHelpProvider", sig);
    }

    /* ---- 8. RenameProvider 对象 (含 prepareProvider) ---- */
    if (srv->capabilities.rename) {
        JsonValue *rp = json_new_object();
        json_object_set(rp, "prepareProvider", json_new_bool(srv->capabilities.prepare_rename));
        json_object_set(rp, "workDoneProgress", json_new_bool(0));
        json_object_set(caps, "renameProvider", rp);
    }

    /* ---- Formatting ---- */
    if (srv->capabilities.formatting) {
        json_object_set(caps, "documentFormattingProvider", json_new_bool(srv->capabilities.formatting));
    }

    /* ---- 2. Folding range (依据 capabilities) ---- */
    if (srv->capabilities.folding_range) {
        json_object_set(caps, "foldingRangeProvider", json_new_bool(srv->capabilities.folding_range));
    }

    /* ---- Workspace symbol ---- */
    if (srv->capabilities.workspace_symbol) {
        json_object_set(caps, "workspaceSymbolProvider", json_new_bool(srv->capabilities.workspace_symbol));
    }

    /* ---- Selection range ---- */
    if (srv->capabilities.selection_range) {
        json_object_set(caps, "selectionRangeProvider", json_new_bool(srv->capabilities.selection_range));
    }

    /* ---- Linked editing range ---- */
    if (srv->capabilities.linked_editing) {
        json_object_set(caps, "linkedEditingRangeProvider", json_new_bool(srv->capabilities.linked_editing));
    }

    /* ---- 6. Semantic tokens (full/range/delta 三布尔与路由一致) ---- */
    if (srv->capabilities.semantic_tokens) {
        JsonValue *semtok = json_new_object();
        JsonValue *legend = json_new_object();
        JsonValue *token_types = json_new_array();
        const char *types[] = {"namespace","type","class","enum","interface","struct",
            "typeParameter","parameter","variable","property","enumMember","event",
            "function","method","macro","keyword","modifier","comment","string",
            "number","regexp","operator","decorator",NULL};
        for (int t = 0; types[t]; t++)
            json_array_add(token_types, json_new_string(types[t]));
        json_object_set(legend, "tokenTypes", token_types);
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
    }

    /* ---- 9. Code action (有 resolve 则为对象，否则为布尔) ---- */
    if (srv->capabilities.code_action) {
        if (srv->capabilities.code_action_resolve) {
            JsonValue *cap = json_new_object();
            JsonValue *kinds = json_new_array();
            json_array_add(kinds, json_new_string(CODE_ACTION_KIND_QUICKFIX));
            json_array_add(kinds, json_new_string(CODE_ACTION_KIND_REFACTOR_REWRITE));
            json_array_add(kinds, json_new_string(CODE_ACTION_KIND_SOURCE_ORGANIZE_IMPORTS));
            json_object_set(cap, "codeActionKinds", kinds);
            json_object_set(cap, "resolveProvider", json_new_bool(1));
            json_object_set(caps, "codeActionProvider", cap);
        } else {
            json_object_set(caps, "codeActionProvider", json_new_bool(1));
        }
    }

    /* ---- 7. Diagnostic provider (补 identifier 等三字段) ---- */
    if (srv->capabilities.diagnostic) {
        JsonValue *diag_provider = json_new_object();
        json_object_set(diag_provider, "identifier", json_new_string("lxclua-diagnostic"));
        json_object_set(diag_provider, "interFileDependencies", json_new_bool(0));
        json_object_set(diag_provider, "workspaceDiagnostics", json_new_bool(0));
        json_object_set(caps, "diagnosticProvider", diag_provider);
    }

    /* ---- Declaration provider ---- */
    if (srv->capabilities.declaration) {
        json_object_set(caps, "declarationProvider", json_new_bool(srv->capabilities.declaration));
    }

    /* ---- 3. Code Lens provider (resolveProvider 依据 capabilities) ---- */
    if (srv->capabilities.code_lens) {
        JsonValue *cl = json_new_object();
        json_object_set(cl, "resolveProvider", json_new_bool(srv->capabilities.code_lens_resolve));
        json_object_set(cl, "workDoneProgress", json_new_bool(0));
        json_object_set(caps, "codeLensProvider", cl);
    }

    /* ---- 3. Document Link provider (resolveProvider 依据 capabilities) ---- */
    if (srv->capabilities.document_link) {
        JsonValue *dl = json_new_object();
        json_object_set(dl, "resolveProvider", json_new_bool(srv->capabilities.document_link_resolve));
        json_object_set(caps, "documentLinkProvider", dl);
    }

    /* ---- 3. Inlay Hint provider (resolveProvider 依据 capabilities) ---- */
    if (srv->capabilities.inlay_hint) {
        JsonValue *ih = json_new_object();
        json_object_set(ih, "resolveProvider", json_new_bool(srv->capabilities.inlay_hint_resolve));
        json_object_set(ih, "workDoneProgress", json_new_bool(0));
        json_object_set(caps, "inlayHintProvider", ih);
    }

    /* ---- Call Hierarchy provider ---- */
    if (srv->capabilities.call_hierarchy) {
        json_object_set(caps, "callHierarchyProvider", json_new_bool(srv->capabilities.call_hierarchy));
    }

    /* ---- Type Hierarchy provider ---- */
    if (srv->capabilities.type_hierarchy) {
        json_object_set(caps, "typeHierarchyProvider", json_new_bool(srv->capabilities.type_hierarchy));
    }

    /* ---- 10. Color provider (ColorProviderOptions 空对象) ---- */
    if (srv->capabilities.color_presentation) {
        JsonValue *cp = json_new_object();
        json_object_set(caps, "colorProvider", cp);
    }

    /* ---- Moniker provider ---- */
    if (srv->capabilities.moniker) {
        json_object_set(caps, "monikerProvider", json_new_bool(srv->capabilities.moniker));
    }

    /* ---- On Type Formatting provider ---- */
    if (srv->capabilities.on_type_formatting) {
        JsonValue *otf = json_new_object();
        json_object_set(otf, "firstTriggerCharacter", json_new_string("\n"));
        JsonValue *more_triggers = json_new_array();
        json_array_add(more_triggers, json_new_string("d"));
        json_object_set(otf, "moreTriggerCharacter", more_triggers);
        json_object_set(caps, "documentOnTypeFormattingProvider", otf);
    }

    /* ---- Range Formatting provider ---- */
    if (srv->capabilities.range_formatting) {
        json_object_set(caps, "documentRangeFormattingProvider", json_new_bool(srv->capabilities.range_formatting));
    }

    /* ---- @since 3.17 Inline Value provider ---- */
    if (srv->capabilities.inline_value) {
        /* InlineValueOptions: { workDoneProgress?: boolean } */
        JsonValue *iv = json_new_object();
        json_object_set(iv, "workDoneProgress", json_new_bool(0));
        json_object_set(caps, "inlineValueProvider", iv);
    }

    /* ---- 4. Workspace 子对象 ---- */
    {
        JsonValue *ws = json_new_object();

        /* workspaceFolders */
        JsonValue *wsf = json_new_object();
        json_object_set(wsf, "supported", json_new_bool(1));
        json_object_set(wsf, "changeNotifications", json_new_bool(1));
        json_object_set(ws, "workspaceFolders", wsf);

        /* symbol */
        JsonValue *sym = json_new_object();
        json_object_set(sym, "resolveProvider", json_new_bool(1));
        JsonValue *sk_val = json_new_object();
        JsonValue *sk_set = json_new_array();
        for (int i = 1; i <= 26; i++)
            json_array_add(sk_set, json_new_number(i));
        json_object_set(sk_val, "valueSet", sk_set);
        json_object_set(sym, "symbolKind", sk_val);
        json_object_set(ws, "symbol", sym);

        /* didChangeConfiguration */
        json_object_set(ws, "didChangeConfiguration", json_new_object());

        /* didChangeWatchedFiles */
        JsonValue *dcwf = json_new_object();
        json_object_set(dcwf, "relativePatternSupport", json_new_bool(0));
        json_object_set(ws, "didChangeWatchedFiles", dcwf);

        /* fileOperations (6 个空 filters 数组) */
        JsonValue *fo = json_new_object();
        JsonValue *wc_obj = json_new_object();
        json_object_set(wc_obj, "filters", json_new_array());
        json_object_set(fo, "willCreate", wc_obj);
        JsonValue *dc_obj = json_new_object();
        json_object_set(dc_obj, "filters", json_new_array());
        json_object_set(fo, "didCreate", dc_obj);
        JsonValue *wr_obj = json_new_object();
        json_object_set(wr_obj, "filters", json_new_array());
        json_object_set(fo, "willRename", wr_obj);
        JsonValue *dr_obj = json_new_object();
        json_object_set(dr_obj, "filters", json_new_array());
        json_object_set(fo, "didRename", dr_obj);
        JsonValue *wd_obj = json_new_object();
        json_object_set(wd_obj, "filters", json_new_array());
        json_object_set(fo, "willDelete", wd_obj);
        JsonValue *dd_obj = json_new_object();
        json_object_set(dd_obj, "filters", json_new_array());
        json_object_set(fo, "didDelete", dd_obj);
        json_object_set(ws, "fileOperations", fo);

        /* executeCommand */
        JsonValue *ec = json_new_object();
        JsonValue *cmds = json_new_array();
        json_array_add(cmds, json_new_string("lxclua.reload"));
        json_array_add(cmds, json_new_string("lxclua.clearCache"));
        json_object_set(ec, "commands", cmds);
        json_object_set(ws, "executeCommand", ec);

        json_object_set(caps, "workspace", ws);
    }

    /* ---- 5. Window 子对象 ---- */
    {
        JsonValue *win = json_new_object();

        /* workDoneProgress */
        json_object_set(win, "workDoneProgress", json_new_bool(1));

        /* showMessage.messageActions */
        JsonValue *sm = json_new_object();
        JsonValue *sm_ma = json_new_array();
        JsonValue *a1 = json_new_object(); json_object_set(a1, "type", json_new_number(1)); json_array_add(sm_ma, a1);
        JsonValue *a2 = json_new_object(); json_object_set(a2, "type", json_new_number(2)); json_array_add(sm_ma, a2);
        JsonValue *a3 = json_new_object(); json_object_set(a3, "type", json_new_number(3)); json_array_add(sm_ma, a3);
        json_object_set(sm, "messageActions", sm_ma);
        json_object_set(win, "showMessage", sm);

        /* showMessageRequest.messageActions */
        JsonValue *smr = json_new_object();
        JsonValue *smr_ma = json_new_array();
        JsonValue *b1 = json_new_object(); json_object_set(b1, "type", json_new_number(1)); json_array_add(smr_ma, b1);
        JsonValue *b2 = json_new_object(); json_object_set(b2, "type", json_new_number(2)); json_array_add(smr_ma, b2);
        JsonValue *b3 = json_new_object(); json_object_set(b3, "type", json_new_number(3)); json_array_add(smr_ma, b3);
        json_object_set(smr, "messageActions", smr_ma);
        json_object_set(win, "showMessageRequest", smr);

        /* showDocument.support */
        JsonValue *sd = json_new_object();
        json_object_set(sd, "support", json_new_bool(1));
        json_object_set(win, "showDocument", sd);

        json_object_set(caps, "window", win);
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
        
        char data_str[512];
        snprintf(data_str, sizeof(data_str), "%d:%s", i, items[i].label);
        json_object_set(item, "data", json_new_string(data_str));
        
        json_object_set(item, "insertTextMode", json_new_number(2));
        
        if (items[i].insert_text && strcmp(items[i].insert_text, items[i].label) != 0) {
            json_object_set(item, "insertText", json_new_string(items[i].insert_text));
            if (items[i].insert_text_format == INSERT_TEXT_SNIPPET)
                json_object_set(item, "insertTextFormat", json_new_number(INSERT_TEXT_SNIPPET));
        }
        
        char sort_text[32];
        snprintf(sort_text, sizeof(sort_text), "%05d_%s", 99999 - items[i].sort_text_priority, items[i].label);
        json_object_set(item, "sortText", json_new_string(sort_text));
        json_array_add(arr, item);
    }
    json_object_set(list, "items", arr);
    return list;
}

/*
 * @brief 构建签名帮助响应
 * @param raw_json lsp_signature_help 返回的原始 JSON 字符串（可为 NULL）
 * @param params LSP 请求参数（用于获取 context.triggerKind）
 * @return LSP SignatureHelp JSON值
 */
static JsonValue *build_signature_help_result(const char *raw_json, JsonValue *params) {
    JsonValue *result = json_new_object();
    JsonValue *sigs_arr = json_new_array();

    if (raw_json && *raw_json) {
        JsonValue *parsed = json_parse(raw_json, (int)strlen(raw_json));
        if (parsed && parsed->type == JSON_OBJECT) {
            JsonValue *existing_sigs = json_object_get(parsed, "signatures");
            if (existing_sigs && existing_sigs->type == JSON_ARRAY) {
                for (size_t i = 0; i < existing_sigs->as.arr.count; i++) {
                    JsonValue *src = existing_sigs->as.arr.items[i];
                    JsonValue *dst = json_new_object();
                    const char *label = json_object_get_string(src, "label", "");
                    json_object_set(dst, "label", json_new_string(label));
                    JsonValue *doc_val = json_object_get(src, "documentation");
                    if (doc_val) {
                        if (doc_val->type == JSON_STRING) {
                            JsonValue *mc = json_new_object();
                            json_object_set(mc, "kind", json_new_string("markdown"));
                            json_object_set(mc, "value", json_new_string(doc_val->as.str_val));
                            json_object_set(dst, "documentation", mc);
                        } else {
                            JsonValue *copy;
                            json_deep_copy(&copy, doc_val);
                            if (copy) json_object_set(dst, "documentation", copy);
                        }
                    }
                    JsonValue *params_arr = json_new_array();
                    JsonValue *src_params = json_object_get(src, "parameters");
                    if (src_params && src_params->type == JSON_ARRAY) {
                        for (size_t j = 0; j < src_params->as.arr.count; j++) {
                            JsonValue *sp = src_params->as.arr.items[j];
                            JsonValue *dp = json_new_object();
                            const char *pname = json_object_get_string(sp, "name", "");
                            json_object_set(dp, "name", json_new_string(pname));
                            JsonValue *plabel = json_object_get(sp, "label");
                            if (plabel) {
                                JsonValue *copy;
                                json_deep_copy(&copy, plabel);
                                if (copy) json_object_set(dp, "label", copy);
                            } else {
                                json_object_set(dp, "label", json_new_string(pname));
                            }
                            JsonValue *pdoc = json_object_get(sp, "documentation");
                            if (pdoc) {
                                JsonValue *copy;
                                json_deep_copy(&copy, pdoc);
                                if (copy) json_object_set(dp, "documentation", copy);
                            }
                            json_array_add(params_arr, dp);
                        }
                    }
                    json_object_set(dst, "parameters", params_arr);
                    json_array_add(sigs_arr, dst);
                }
            }
            json_free(parsed);
        }
    }

    json_object_set(result, "signatures", sigs_arr);
    json_object_set(result, "activeSignature", json_new_number(0));
    json_object_set(result, "activeParameter", json_new_number(0));

    return result;
}

/*
 * @brief 构建悬停响应
 * @param text Markdown内容
 * @param doc LSP文档指针(可为NULL)
 * @param line 行号
 * @param col 列号
 * @return LSP Hover JSON值
 */
static JsonValue *build_hover_result(const char *text, LspDocument *doc, int line, int col) {
    if (!text) return json_new_null();
    JsonValue *hover = json_new_object();
    JsonValue *contents = json_new_object();
    json_object_set(contents, "kind", json_new_string("markdown"));
    json_object_set(contents, "value", json_new_string(text));
    json_object_set(hover, "contents", contents);
    if (doc) {
        int offset = lsp_linecol_to_offset(doc->text, line, col);
        int ws, we;
        char *word = lsp_get_word_at(doc->text, offset, &ws, &we);
        if (word) {
            int wline_s = 0, wcol_s = 0, wline_e = 0, wcol_e = 0;
            if (lsp_offset_to_linecol(doc->text, ws, &wline_s, &wcol_s) == 0 &&
                lsp_offset_to_linecol(doc->text, we, &wline_e, &wcol_e) == 0) {
                JsonValue *range = json_new_object();
                JsonValue *s = json_new_object();
                json_object_set(s, "line", json_new_number(wline_s));
                json_object_set(s, "character", json_new_number(wcol_s));
                JsonValue *e = json_new_object();
                json_object_set(e, "line", json_new_number(wline_e));
                json_object_set(e, "character", json_new_number(wcol_e));
                json_object_set(range, "start", s);
                json_object_set(range, "end", e);
                json_object_set(hover, "range", range);
            }
            lsp_free(word);
        }
    }
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
JsonValue *lsp_build_diagnostics_arr(LspDiagnostic *diags, int ndiags) {
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
        {
            int code_val = i + 1;
            json_object_set(d, "code", json_new_number(code_val));
        }
        {
            JsonValue *code_desc = json_new_object();
            char href_buf[256];
            snprintf(href_buf, sizeof(href_buf), "https://lxclua.example/diagnostics/%d", i + 1);
            json_object_set(code_desc, "href", json_new_string(href_buf));
            json_object_set(d, "codeDescription", code_desc);
        }
        {
            JsonValue *tags_arr = json_new_array();
            if (diags[i].severity == 5) {
                json_array_add(tags_arr, json_new_number(2));
            }
            json_object_set(d, "tags", tags_arr);
        }
        json_object_set(d, "relatedInformation", json_new_array());
        json_object_set(d, "data", json_new_null());
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
    /* TextDocumentSyncKind.Full(1)：客户端承诺每个 contentChanges[0] 都是全量文本，
     * 因此直接取最后一条（理论上只有一条）的 text 替换即可。 */
    JsonValue *last_change = changes->as.arr.items[changes->as.arr.count - 1];
    const char *new_text = json_object_get_string(last_change, "text", "");
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
    
    /* ---- 通知类白名单（用于末尾跳过错误响应） ---- */
    int is_whitelisted_notification = 0;
    if (is_notification) {
        if (strcmp(method, LSP_METHOD_INITIALIZED) == 0 ||
            strcmp(method, LSP_METHOD_DID_OPEN) == 0 ||
            strcmp(method, LSP_METHOD_DID_CHANGE) == 0 ||
            strcmp(method, LSP_METHOD_DID_CLOSE) == 0 ||
            strcmp(method, LSP_METHOD_DID_SAVE) == 0 ||
            strcmp(method, LSP_METHOD_WILL_SAVE) == 0 ||
            strcmp(method, LSP_METHOD_WORKSPACE_CFG_CHG) == 0 ||
            strcmp(method, LSP_METHOD_CANCEL_REQUEST) == 0 ||
            strcmp(method, LSP_METHOD_PROGRESS_REPORT) == 0 ||
            strcmp(method, LSP_METHOD_SET_TRACE) == 0 ||
            strcmp(method, LSP_METHOD_LOG_TRACE) == 0 ||
            strcmp(method, LSP_METHOD_DID_CREATE_FILES) == 0 ||
            strcmp(method, LSP_METHOD_DID_RENAME_FILES) == 0 ||
            strcmp(method, LSP_METHOD_DID_DELETE_FILES) == 0 ||
            strcmp(method, LSP_METHOD_WATCHED_FILES_CHG) == 0 ||
            strcmp(method, "workspace/didChangeWorkspaceFolders") == 0) {
            is_whitelisted_notification = 1;
        }
    }
    
    /* ---- 1. 进入具体 if 分支前先做初始化检查 ---- */
    if (strcmp(method, "initialize") != 0 &&
        strcmp(method, "exit") != 0 &&
        strcmp(method, "initialized") != 0 &&
        !srv->initialized) {
        if (is_whitelisted_notification) return NULL;
        return jrpc_new_error_resp(id, LSP_ERR_ServerNotInitialized, "Server not initialized");
    }
    
    /* ---- 2. shutdown 状态检查：srv->shutdown=1 且 method != exit 时返回错误 ---- */
    if (srv->shutdown && strcmp(method, "exit") != 0) {
        if (is_whitelisted_notification) return NULL;
        /* @since LSP 3.17: shutdown 后再发请求，服务端应返回 ServerCancelled(-32802) */
        return jrpc_new_error_resp(id, LSP_ERR_ServerCancelled, "Server is shutting down");
    }
    
    /* ---- Lifecycle ---- */
    if (strcmp(method, LSP_METHOD_INITIALIZE) == 0) {
        if (is_notification) return jrpc_new_error_resp(id, JRPC_INVALID_REQUEST, "initialize must be a request");
        srv->initialized = 1;
        srv->client_caps.workspace.apply_edit = 1;
        if (params) {
            JsonValue *cc = json_object_get(params, "capabilities");
            if (cc) {
                JsonValue *ws = json_object_get(cc, "workspace");
                if (ws) {
                    srv->client_caps.workspace.apply_edit = json_object_get_bool(ws, "applyEdit", 1);
                }
            }
            JsonValue *wsf = json_object_get(params, "workspaceFolders");
            if (wsf && wsf->type == JSON_ARRAY && wsf->as.arr.count > 0) {
                int n = (int)wsf->as.arr.count;
                if (n > LSP_MAX_WORKSPACE_FOLDERS) n = LSP_MAX_WORKSPACE_FOLDERS;
                srv->n_workspace_folders = 0;
                for (int i = 0; i < n; i++) {
                    JsonValue *f = wsf->as.arr.items[i];
                    if (!f) continue;
                    const char *uri = json_object_get_string(f, "uri", "");
                    const char *name = json_object_get_string(f, "name", "");
                    if (!uri) uri = "";
                    if (!name) name = "";
                    srv->workspaceFolders[i].uri = lsp_strdup(uri);
                    srv->workspaceFolders[i].name = lsp_strdup(name);
                    srv->n_workspace_folders++;
                }
            }
        }
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
        srv->capabilities.code_lens_resolve = 1;
        srv->capabilities.document_link = 1;
        srv->capabilities.document_link_resolve = 1;
        srv->capabilities.inlay_hint = 1;
        srv->capabilities.inlay_hint_resolve = 1;
        srv->capabilities.call_hierarchy = 1;
        srv->capabilities.type_hierarchy = 1;
        srv->capabilities.color_presentation = 1;
        srv->capabilities.moniker = 1;
        srv->capabilities.on_type_formatting = 1;
        srv->capabilities.range_formatting = 1;
        srv->capabilities.inline_value = 1;   /* @since 3.17 textDocument/inlineValue */
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
        if (srv->shutdown) {
            srv->exit_requested = 1;
            srv->exit_code = 0;
        } else {
            srv->exit_requested = 1;
            srv->exit_code = 1;
        }
        return NULL;
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
        const char *text = json_object_get_string(params, "text", NULL);
        if (text) {
            for (int i = 0; i < srv->ndocs; i++) {
                if (strcmp(srv->docs[i]->uri, uri) == 0) {
                    lsp_free(srv->docs[i]->text);
                    srv->docs[i]->text = lsp_strdup(text);
                    srv->docs[i]->text_len = strlen(text);
                    lsp_free(srv->docs[i]->line_offsets);
                    lsp_build_line_offsets(srv->docs[i]->text, (int)srv->docs[i]->text_len,
                                           &srv->docs[i]->line_offsets, &srv->docs[i]->nlines);
                    break;
                }
            }
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        if (doc) lsp_doc_parse(doc, 1);
        return NULL;
    }
    
    /* ---- Workspace ---- */
    if (strcmp(method, LSP_METHOD_WORKSPACE_CFG_CHG) == 0) {
        /* Store settings for later use - handled as notification */
        return NULL; /* Notification */
    }
    if (strcmp(method, "workspace/didChangeWorkspaceFolders") == 0) {
        if (params) {
            JsonValue *event = json_object_get(params, "event");
            if (event) {
                JsonValue *added = json_object_get(event, "added");
                if (added && added->type == JSON_ARRAY) {
                    for (size_t i = 0; i < added->as.arr.count; i++) {
                        JsonValue *f = added->as.arr.items[i];
                        if (!f) continue;
                        const char *uri = json_object_get_string(f, "uri", "");
                        const char *name = json_object_get_string(f, "name", "");
                        if (!uri) uri = "";
                        if (!name) name = "";
                        if (srv->n_workspace_folders < LSP_MAX_WORKSPACE_FOLDERS) {
                            srv->workspaceFolders[srv->n_workspace_folders].uri = lsp_strdup(uri);
                            srv->workspaceFolders[srv->n_workspace_folders].name = lsp_strdup(name);
                            srv->n_workspace_folders++;
                        }
                    }
                }
                JsonValue *removed = json_object_get(event, "removed");
                if (removed && removed->type == JSON_ARRAY) {
                    for (size_t i = 0; i < removed->as.arr.count; i++) {
                        JsonValue *f = removed->as.arr.items[i];
                        if (!f) continue;
                        const char *uri = json_object_get_string(f, "uri", "");
                        if (!uri) continue;
                        for (int j = 0; j < srv->n_workspace_folders; j++) {
                            if (srv->workspaceFolders[j].uri && strcmp(srv->workspaceFolders[j].uri, uri) == 0) {
                                lsp_free(srv->workspaceFolders[j].uri);
                                lsp_free(srv->workspaceFolders[j].name);
                                srv->workspaceFolders[j].uri = NULL;
                                srv->workspaceFolders[j].name = NULL;
                                for (int k = j; k < srv->n_workspace_folders - 1; k++) {
                                    srv->workspaceFolders[k] = srv->workspaceFolders[k + 1];
                                }
                                srv->n_workspace_folders--;
                                break;
                            }
                        }
                    }
                }
            }
        }
        return NULL;
    }
    if (strcmp(method, LSP_METHOD_WATCHED_FILES_CHG) == 0) {
        if (params) {
            JsonValue *changes = json_object_get(params, "changes");
            if (changes && changes->type == JSON_ARRAY) {
                for (size_t i = 0; i < changes->as.arr.count; i++) {
                    JsonValue *c = changes->as.arr.items[i];
                    if (!c) continue;
                    int type = json_object_get_int(c, "type", 0);
                    const char *uri = json_object_get_string(c, "uri", "");
                    if (!uri || !*uri) continue;
                    LspDocument *doc = lsp_doc_find(srv, uri);
                    if (type == 1 || type == 2) {
                        if (doc) lsp_doc_parse(doc, 1);
                    } else if (type == 3) {
                        if (doc) lsp_doc_close(srv, uri);
                    }
                }
            }
        }
        return NULL;
    }
    
    /* ---- Completion ---- */
    if (strcmp(method, LSP_METHOD_COMPLETION) == 0) {
        cm_begin(srv, params);  /* @since 3.17 记录起始版本，didChange 期间版本变化返回 ContentModified */
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
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
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during completion"); }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Hover ---- */
    if (strcmp(method, LSP_METHOD_HOVER) == 0) {
        cm_begin(srv, params);
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *hover_text = doc ? lsp_hover(doc, line, col) : NULL;
        JsonValue *result = build_hover_result(hover_text, doc, line, col);
        lsp_free(uri); lsp_free(hover_text);
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during hover"); }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Definition ---- */
    if (strcmp(method, LSP_METHOD_DEFINITION) == 0) {
        cm_begin(srv, params);
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
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
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during definition"); }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- References ---- */
    if (strcmp(method, LSP_METHOD_REFERENCES) == 0) {
        cm_begin(srv, params);
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
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
        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during references"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Document Highlight ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_HIGHLIGHT) == 0) {
        cm_begin(srv, params);
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
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
        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during documentHighlight"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Document Symbol ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_SYMBOL) == 0) {
        cm_begin(srv, params);
        if (lsp_is_cancelled(srv, id)) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspSymbol **syms = NULL;
        int nsyms = doc ? lsp_document_symbol(doc, &syms) : 0;

        if (lsp_is_cancelled(srv, id)) {
            for (int i = 0; i < nsyms; i++) {
                lsp_free(syms[i]->name);
                lsp_free(syms[i]->detail);
                lsp_free(syms[i]->documentation);
                lsp_free(syms[i]);
            }
            lsp_free(syms);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        
        JsonValue *arr = json_new_array();
        for (int i = 0; i < nsyms; i++) {
            if (lsp_is_cancelled(srv, id)) {
                for (int j = i; j < nsyms; j++) {
                    lsp_free(syms[j]->name);
                    lsp_free(syms[j]->detail);
                    lsp_free(syms[j]->documentation);
                    lsp_free(syms[j]);
                }
                lsp_free(syms);
                json_free(arr);
                return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
            }
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
            if (syms[i]->documentation) json_object_set(sym, "documentation", json_new_string(syms[i]->documentation));
            json_object_set(sym, "children", json_new_array());
            json_array_add(arr, sym);
            /* Free symbol */
            lsp_free(syms[i]->name);
            lsp_free(syms[i]->detail);
            lsp_free(syms[i]->documentation);
            lsp_free(syms[i]);
        }
        lsp_free(syms);

        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during documentSymbol"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }

    /* ---- Signature Help ---- */
    if (strcmp(method, LSP_METHOD_SIGNATURE_HELP) == 0) {
        cm_begin(srv, params);
        char *uri = NULL; int line = 0, col = 0;
        if (params_get_doc_pos(params, &uri, &line, &col) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, JRPC_INVALID_PARAMS, "Missing textDocument/position");
        }
        LspDocument *doc = lsp_doc_find(srv, uri);
        char *sig = doc ? lsp_signature_help(doc, line, col) : NULL;
        JsonValue *result = build_signature_help_result(sig, params);
        lsp_free(uri); lsp_free(sig);
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during signatureHelp"); }
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
            char *placeholder = NULL;
            if (doc) {
                int start_off = lsp_linecol_to_offset(doc->text, rline, rcol);
                int end_off = lsp_linecol_to_offset(doc->text, rend_line, rend_col);
                if (start_off >= 0 && end_off >= start_off) {
                    int len = end_off - start_off;
                    placeholder = (char *)lsp_alloc((size_t)len + 1);
                    if (placeholder) {
                        memcpy(placeholder, doc->text + start_off, (size_t)len);
                        placeholder[len] = '\0';
                    }
                }
            }
            json_object_set(result, "placeholder", json_new_string(placeholder ? placeholder : ""));
            lsp_free(placeholder);
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
        if (lsp_is_cancelled(srv, id))
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
        if (lsp_is_cancelled(srv, id)) {
            lsp_free(formatted);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
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
        cm_begin(srv, params);
        if (lsp_is_cancelled(srv, id)) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        char result_id_buf[512];
        result_id_buf[0] = '\0';
        {
            const char *uri_short = uri;
            const char *last_slash = strrchr(uri, '/');
            if (last_slash) uri_short = last_slash + 1;
            if (doc && doc->version > 0) {
                snprintf(result_id_buf, sizeof(result_id_buf), "doc-%s-%d", uri_short, doc->version);
            } else {
                snprintf(result_id_buf, sizeof(result_id_buf), "doc-%s-0", uri_short);
            }
        }
        int is_unchanged = 0;
        {
            for (int k = 0; k < srv->n_diag_result_ids; k++) {
                if (srv->diag_result_uris[k] && strcmp(srv->diag_result_uris[k], uri) == 0) {
                    if (srv->diag_result_ids[k] && strcmp(srv->diag_result_ids[k], result_id_buf) == 0) {
                        is_unchanged = 1;
                    }
                    break;
                }
            }
        }
        if (is_unchanged) {
            JsonValue *result = json_new_object();
            json_object_set(result, "kind", json_new_string("unchanged"));
            json_object_set(result, "resultId", json_new_string(result_id_buf));
            JsonRpcMessage *resp = jrpc_new_response(id, result);
            json_free(result);
            return resp;
        }
        {
            int found_idx = -1;
            for (int k = 0; k < srv->n_diag_result_ids; k++) {
                if (srv->diag_result_uris[k] && strcmp(srv->diag_result_uris[k], uri) == 0) {
                    found_idx = k;
                    break;
                }
            }
            if (found_idx >= 0) {
                lsp_free(srv->diag_result_ids[found_idx]);
                srv->diag_result_ids[found_idx] = lsp_strdup(result_id_buf);
            } else if (srv->n_diag_result_ids < LSP_MAX_DIAG_RESULT_IDS) {
                srv->diag_result_uris[srv->n_diag_result_ids] = lsp_strdup(uri);
                srv->diag_result_ids[srv->n_diag_result_ids] = lsp_strdup(result_id_buf);
                srv->n_diag_result_ids++;
            }
        }
        LspDiagnostic *diags = NULL;
        int ndiags = doc ? lsp_diagnostic(doc, &diags) : 0;
        if (lsp_is_cancelled(srv, id)) {
            if (diags) {
                for (int i = 0; i < ndiags; i++) {
                    lsp_free(diags[i].message);
                    lsp_free(diags[i].source);
                }
                lsp_free(diags);
            }
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        JsonValue *result = json_new_object();
        json_object_set(result, "kind", json_new_string("full"));
        json_object_set(result, "resultId", json_new_string(result_id_buf));
        JsonValue *diag_arr = lsp_build_diagnostics_arr(diags, ndiags);
        json_object_set(result, "items", diag_arr);
        if (diags) {
            for (int i = 0; i < ndiags; i++) {
                lsp_free(diags[i].message);
                lsp_free(diags[i].source);
            }
            lsp_free(diags);
        }
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during diagnostic"); }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }

    /* ---- Folding Range ---- */
    if (strcmp(method, LSP_METHOD_FOLDING_RANGE) == 0) {
        cm_begin(srv, params);
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
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
        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during foldingRange"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }
    
    /* ---- Semantic Tokens ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS) == 0) {
        cm_begin(srv, params);
        if (lsp_is_cancelled(srv, id)) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        
        JsonValue *result = json_new_object();
        JsonValue *data_arr = json_new_array();
        
        if (doc && doc->tokens && doc->ntokens > 0) {
            int prev_line = 0, prev_col = 0;
            for (int i = 0; i < doc->ntokens; i++) {
                if ((i & 0x1ff) == 0 && lsp_is_cancelled(srv, id)) {
                    json_free(data_arr);
                    json_free(result);
                    return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
                }
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_EOS || !tok->text) continue;
                
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
                                else if (k == SYMBOL_INTERFACE) token_type = 4;
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
                            tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM ||
                            tok->type == TOK_CLASS || tok->type == TOK_INTERFACE || tok->type == TOK_TRAIT)
                            token_type = 1;
                        else if (tok->type >= TOK_AND && tok->type <= TOK_USE)
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
                
                prev_line = tok->line; prev_col = tok->col;
            }
        }
        
        json_object_set(result, "data", data_arr);
        {
            char rid[128];
            snprintf(rid, sizeof(rid), "%s_v%d_t%d", uri, doc ? doc->version : 0, doc ? doc->ntokens : 0);
            json_object_set(result, "resultId", json_new_string(rid));
            lsp_free(srv->prev_semantic_result_id);
            srv->prev_semantic_result_id = lsp_strdup(rid);
            srv->prev_semantic_version = doc ? doc->version : 0;
        }
        if (cm_end_check(srv)) { json_free(result); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during semanticTokens"); }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }

    /* ---- Code Action ---- */
    if (strcmp(method, LSP_METHOD_CODE_ACTION) == 0) {
        cm_begin(srv, params);
        if (lsp_is_cancelled(srv, id)) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        JsonValue *range = json_object_get(params, "range");
        int line = range ? json_object_get_int(json_object_get(range, "start"), "line", 0) : 0;
        int col = range ? json_object_get_int(json_object_get(range, "start"), "character", 0) : 0;
        LspDocument *doc = lsp_doc_find(srv, uri);
        LspDiagnostic *diag_list = NULL;
        int ndiag = 0;
        if (doc) lsp_code_action(doc, line, col, &diag_list, &ndiag);

        if (lsp_is_cancelled(srv, id)) {
            for (int i = 0; i < ndiag; i++) {
                lsp_free(diag_list[i].message);
                lsp_free(diag_list[i].source);
            }
            lsp_free(diag_list);
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        }

        /* context.only 过滤：只保留匹配的 CodeActionKind */
        int only_quickfix = 0;
        int has_only_filter = 0;
        JsonValue *ctx = json_object_get(params, "context");
        if (ctx) {
            JsonValue *only = json_object_get(ctx, "only");
            if (only && only->type == JSON_ARRAY && only->as.arr.count > 0) {
                has_only_filter = 1;
                for (size_t k = 0; k < only->as.arr.count; k++) {
                    JsonValue *kind = only->as.arr.items[k];
                    if (kind && kind->type == JSON_STRING) {
                        const char *ks = kind->as.str_val;
                        if (strncmp(ks, "quickfix", 8) == 0) only_quickfix = 1;
                    }
                }
            }
        }
        int want_quickfix = !has_only_filter || only_quickfix;
        
        /* 为每个诊断生成带实际修复文本的 CodeAction */
        JsonValue *arr = json_new_array();
        int qf_count = 0;
        for (int i = 0; i < ndiag; i++) {
            if (lsp_is_cancelled(srv, id)) {
                for (int j = i; j < ndiag; j++) {
                    lsp_free(diag_list[j].message);
                    lsp_free(diag_list[j].source);
                }
                lsp_free(diag_list);
                json_free(arr);
                return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
            }
            if (!want_quickfix) continue;
            const char *msg = diag_list[i].message;
            const char *fix_text = NULL;
            int is_trailing_ws = 0;
            char *line_text = NULL;
            
            /* 根据诊断消息确定修复文本 */
            if (msg && strstr(msg, "行尾有多余空白字符")) {
                int offset = lsp_linecol_to_offset(doc->text, diag_list[i].line_start, 0);
                line_text = lsp_get_line_text(doc->text, offset);
                if (line_text) {
                    int end = (int)strlen(line_text) - 1;
                    while (end >= 0 && (line_text[end] == ' ' || line_text[end] == '\t')) end--;
                    line_text[end + 1] = '\0';
                    fix_text = line_text;
                    is_trailing_ws = 1;
                }
            } else if (msg && strstr(msg, "多余的逗号")) {
                fix_text = "";
            } else if (msg && strstr(msg, "末尾多余逗号")) {
                fix_text = "";
            } else if (msg && strstr(msg, "未闭合的长字符串")) {
                fix_text = "]]";
            } else if (msg && strstr(msg, "未闭合的字符串")) {
                fix_text = "\"";
            } else if (msg && strstr(msg, "Unclosed block")) {
                fix_text = "end";
            } else if (msg && strstr(msg, "Unexpected 'end'")) {
                fix_text = "";
            } else {
                fix_text = "";
            }
            
            JsonValue *action = json_new_object();
            
            char title[512];
            const char *prefix = diag_list[i].severity == SEVERITY_ERROR ? "修复错误" : 
                                 diag_list[i].severity == SEVERITY_WARNING ? "修复警告" : "修复";
            if (is_trailing_ws) {
                snprintf(title, sizeof(title), "%s: 移除行尾空白", prefix);
            } else if (strstr(msg ? msg : "", "多余的逗号") || strstr(msg ? msg : "", "末尾多余逗号")) {
                snprintf(title, sizeof(title), "%s: 移除多余逗号", prefix);
            } else if (msg && strstr(msg, "未闭合的")) {
                snprintf(title, sizeof(title), "%s: 补全%s", prefix, 
                         strstr(msg, "长字符串") ? "长字符串 ]] " : "字符串引号");
            } else if (msg && strstr(msg, "Unclosed block")) {
                snprintf(title, sizeof(title), "%s: 添加缺失的 end", prefix);
            } else if (msg && strstr(msg, "Unexpected 'end'")) {
                snprintf(title, sizeof(title), "%s: 移除多余的 end", prefix);
            } else {
                snprintf(title, sizeof(title), "%s: %s", prefix, msg ? msg : "");
            }
            json_object_set(action, "title", json_new_string(title));
            json_object_set(action, "kind", json_new_string("quickfix"));

            if (qf_count == 0) {
                json_object_set(action, "isPreferred", json_new_bool(1));
            }
            qf_count++;
            
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
            
            JsonValue *edit = json_new_object();
            JsonValue *changes = json_new_object();
            JsonValue *edits_arr = json_new_array();
            JsonValue *te = json_new_object();
            
            if (is_trailing_ws) {
                JsonValue *te_r = json_new_object();
                JsonValue *te_s = json_new_object();
                json_object_set(te_s, "line", json_new_number(diag_list[i].line_start));
                json_object_set(te_s, "character", json_new_number(0));
                JsonValue *te_e = json_new_object();
                json_object_set(te_e, "line", json_new_number(diag_list[i].line_start));
                json_object_set(te_e, "character", json_new_number(diag_list[i].col_end));
                json_object_set(te_r, "start", te_s);
                json_object_set(te_r, "end", te_e);
                json_object_set(te, "range", te_r);
            } else {
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
            }
            json_object_set(te, "newText", json_new_string(fix_text));
            json_array_add(edits_arr, te);
            json_object_set(changes, uri, edits_arr);
            json_object_set(edit, "changes", changes);
            json_object_set(action, "edit", edit);

            char data_str[64];
            snprintf(data_str, sizeof(data_str), "%d", i);
            json_object_set(action, "data", json_new_string(data_str));
            
            json_array_add(arr, action);
            
            lsp_free(line_text);
        }
        for (int i = 0; i < ndiag; i++) {
            lsp_free(diag_list[i].message);
            lsp_free(diag_list[i].source);
        }
        lsp_free(diag_list);
        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during codeAction"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }

    /* ---- Cancel Request ---- */
    if (strcmp(method, LSP_METHOD_CANCEL_REQUEST) == 0) {
        JsonValue *cancel_id = params ? json_object_get(params, "id") : NULL;
        if (cancel_id) lsp_push_cancel_id(srv, cancel_id);
        return NULL;
    }

    /* ---- Progress Report notification ---- */
    if (strcmp(method, LSP_METHOD_PROGRESS_REPORT) == 0) {
        JsonValue *token = params ? json_object_get(params, "token") : NULL;
        JsonValue *value = params ? json_object_get(params, "value") : NULL;
        if (token) lsp_store_progress(srv, token, value);
        return NULL;
    }

    /* ---- Work Done Progress Create (request) ---- */
    if (strcmp(method, LSP_METHOD_PROGRESS_START) == 0) {
        JsonValue *token = params ? json_object_get(params, "token") : NULL;
        if (token) lsp_store_progress(srv, token, json_new_null());
        return jrpc_new_response(id, json_new_null());
    }

    /* ---- @since 3.17 $/setTrace Notification ----
     * 设置服务端日志输出级别（off | messages | verbose）。影响后续 stderr / $/logTrace 输出。 */
    if (strcmp(method, LSP_METHOD_SET_TRACE) == 0) {
        const char *val = params ? json_object_get_string(params, "value", "off") : "off";
        if (val) {
            if (strcmp(val, "verbose") == 0)
                srv->trace_level = LSP_TRACE_VERBOSE;
            else if (strcmp(val, "messages") == 0)
                srv->trace_level = LSP_TRACE_MESSAGES;
            else
                srv->trace_level = LSP_TRACE_OFF;
        }
        return NULL; /* Notification, no response */
    }

    /* ---- @since 3.17 $/logTrace Notification (client -> server) ----
     * 可选，客户端发来自己的 trace log；服务端忽略即可，不做转发。 */
    if (strcmp(method, LSP_METHOD_LOG_TRACE) == 0) {
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
            JsonValue *loc = json_new_object();
            json_object_set(loc, "uri", json_new_string(syms[i]->detail ? syms[i]->detail : ""));
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
            char data_buf[64];
            snprintf(data_buf, sizeof(data_buf), "wsym-%d", i);
            json_object_set(sym, "data", json_new_string(data_buf));
            json_array_add(arr, sym);
            lsp_free(syms[i]->name);
            lsp_free(syms[i]->detail);
            lsp_free(syms[i]->documentation);
            lsp_free(syms[i]);
        }
        lsp_free(syms);
        /* 工作区符号跨文件不参与 ContentModified 检测，无需 cm_end_check */
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }

    /* ---- Selection Range ---- */
    if (strcmp(method, LSP_METHOD_SELECTION_RANGE) == 0) {
        cm_begin(srv, params);
        if (params_check_textdocument_uri(params) != 0) {
            cm_end_check(srv);
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        }
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
            JsonValue *levels[3] = {0};
            for (int lv = 0; lv < 3; lv++) {
                int idx = i - lv;
                if (idx < 0) break;
                JsonValue *sr = json_new_object();
                JsonValue *rng = json_new_object();
                JsonValue *s = json_new_object();
                json_object_set(s, "line", json_new_number(starts[idx]));
                json_object_set(s, "character", json_new_number(0));
                JsonValue *e = json_new_object();
                json_object_set(e, "line", json_new_number(ends[idx]));
                json_object_set(e, "character", json_new_number(0));
                json_object_set(rng, "start", s);
                json_object_set(rng, "end", e);
                json_object_set(sr, "range", rng);
                levels[lv] = sr;
            }
            for (int lv = 1; lv < 3; lv++) {
                if (levels[lv] && levels[lv-1]) {
                    json_object_set(levels[lv-1], "parent", levels[lv]);
                }
            }
            if (levels[0]) json_array_add(arr, levels[0]);
        }
        
        lsp_free(plines); lsp_free(pcols);
        lsp_free(starts); lsp_free(ends);
        if (cm_end_check(srv)) { json_free(arr); return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during selectionRange"); }
        JsonRpcMessage *resp = jrpc_new_response(id, arr);
        json_free(arr);
        return resp;
    }

    /* ---- Completion Resolve ---- */
    if (strcmp(method, LSP_METHOD_COMPLETION_RESOLVE) == 0) {
        JsonValue *result;
        json_deep_copy(&result, params);
        if (result) {
            const char *data_str = json_object_get_string(result, "data", NULL);
            if (data_str && *data_str) {
                const char *colon = strchr(data_str, ':');
                if (colon) {
                    const char *label = colon + 1;
                    const char *doc = lsp_kwdb_find_doc(label);
                    if (doc && *doc) {
                        json_object_set(result, "documentation", json_new_string(doc));
                    }
                    char detail_buf[256];
                    snprintf(detail_buf, sizeof(detail_buf), "%s", label);
                    json_object_set(result, "detail", json_new_string(detail_buf));
                }
            }
        }
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
                int woffset = doc ? lsp_linecol_to_offset(doc->text, llines[i], lcols[i]) : 0;
                int ws = 0, we = 0;
                int wlen = 1;
                if (doc) {
                    char *w = lsp_get_word_at(doc->text, woffset, &ws, &we);
                    if (w) { wlen = we - ws; lsp_free(w); }
                }
                json_object_set(e, "line", json_new_number(llines[i]));
                json_object_set(e, "character", json_new_number(lcols[i] + wlen));
                json_object_set(rng, "start", s);
                json_object_set(rng, "end", e);
                json_array_add(ranges, rng);
            }
            json_object_set(result, "ranges", ranges);
            json_object_set(result, "wordPattern", json_new_string("\\w+"));
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
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
            {
                char data_buf[64];
                snprintf(data_buf, sizeof(data_buf), "codelens-%d", i);
                json_object_set(cl, "data", json_new_string(data_buf));
            }
            json_object_set(cl, "resolveProvider", json_new_bool(1));
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
        if (result) {
            const char *data_str = json_object_get_string(result, "data", NULL);
            JsonValue *td_uri = json_object_get(params, "textDocument");
            const char *doc_uri = td_uri ? json_object_get_string(td_uri, "uri", NULL) : NULL;
            if (!doc_uri) doc_uri = "file:///unknown.lua";
            if (!json_object_get(result, "command")) {
                JsonValue *cmd = json_new_object();
                json_object_set(cmd, "title", json_new_string("Lens Resolved"));
                json_object_set(cmd, "command", json_new_string("lxclua.inspect"));
                JsonValue *args_arr = json_new_array();
                json_array_add(args_arr, json_new_string(doc_uri));
                json_object_set(cmd, "arguments", args_arr);
                json_object_set(result, "command", cmd);
            }
            (void)data_str;
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* ---- Document Link ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_LINK) == 0) {
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
            json_object_set(item, "name", json_new_string(ch_name ? ch_name : "function"));
            json_object_set(item, "kind", json_new_number(SYMBOL_FUNCTION));
            {
                char detail_buf[256];
                snprintf(detail_buf, sizeof(detail_buf), "%s @ line %d", ch_name ? ch_name : "function", ch_line);
                json_object_set(item, "detail", json_new_string(detail_buf));
            }
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
            /* selectionRange 需要是独立拷贝，否则 json_free 时与 range 指向同一对象导致 double-free */
            JsonValue *sel_rng = NULL;
            json_deep_copy(&sel_rng, rng);
            json_object_set(item, "selectionRange", sel_rng);
            json_object_set(item, "uri", json_new_string(uri));
            {
                char data_buf[64];
                snprintf(data_buf, sizeof(data_buf), "callhi-0");
                json_object_set(item, "data", json_new_string(data_buf));
            }
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
                {
                    JsonValue *sel = NULL;
                    json_deep_copy(&sel, frng);
                    json_object_set(from, "selectionRange", sel);
                }
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
                {
                    JsonValue *sel = NULL;
                    json_deep_copy(&sel, trng);
                    json_object_set(to, "selectionRange", sel);
                }
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
            json_object_set(item, "name", json_new_string(th_name ? th_name : "type"));
            json_object_set(item, "kind", json_new_number(SYMBOL_STRUCT));
            json_object_set(item, "uri", json_new_string(uri));
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
            {
                /* selectionRange 必须独立，否则 json_free 与 range 同指针导致 double-free */
                JsonValue *sel = NULL;
                json_deep_copy(&sel, rng);
                json_object_set(item, "selectionRange", sel);
            }
            {
                char detail_buf[256];
                snprintf(detail_buf, sizeof(detail_buf), "%s @ line %d", th_name ? th_name : "type", th_line);
                json_object_set(item, "detail", json_new_string(detail_buf));
            }
            {
                char data_buf[64];
                snprintf(data_buf, sizeof(data_buf), "tyhi-0");
                json_object_set(item, "data", json_new_string(data_buf));
            }
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
            {
                JsonValue *sel = NULL;
                json_deep_copy(&sel, rng);
                json_object_set(ti, "selectionRange", sel);
            }
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
            {
                JsonValue *sel = NULL;
                json_deep_copy(&sel, rng);
                json_object_set(ti, "selectionRange", sel);
            }
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
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        JsonValue *td = json_object_get(params, "textDocument");
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
            const char *scheme_val = mk_schemes[i] && mk_schemes[i][0] ? mk_schemes[i] : "lxclua";
            char ident_buf[128];
            if (mk_ids[i] && mk_ids[i][0]) {
                snprintf(ident_buf, sizeof(ident_buf), "%s", mk_ids[i]);
            } else {
                snprintf(ident_buf, sizeof(ident_buf), "sym-%d", i);
            }
            json_object_set(mk, "scheme", json_new_string(scheme_val));
            json_object_set(mk, "identifier", json_new_string(ident_buf));
            json_object_set(mk, "unique", json_new_string("file"));
            json_object_set(mk, "kind", json_new_string("import"));
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
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
    if (strcmp(method, LSP_METHOD_WILL_SAVE) == 0) {
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        int reason = json_object_get_int(params, "reason", 1);
        (void)uri; (void)reason;
        return NULL;
    }
    
    /* ---- Will Save Wait Until (request, needs textDocument.uri) ---- */
    if (strcmp(method, LSP_METHOD_WILL_SAVE_WAIT) == 0) {
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        JsonValue *empty_arr = json_new_array();
        JsonRpcMessage *resp = jrpc_new_response(id, empty_arr);
        json_free(empty_arr);
        return resp;
    }
    
    /* ---- Semantic Tokens Range ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS_RANGE) == 0) {
        if (lsp_is_cancelled(srv, id))
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
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
                if ((i & 0x1ff) == 0 && lsp_is_cancelled(srv, id)) {
                    json_free(data_arr);
                    json_free(result);
                    return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
                }
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_EOS || !tok->text) continue;
                if (tok->line < start_line || tok->line > end_line) continue;
                
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
                                else if (k == SYMBOL_INTERFACE) token_type = 4;
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
                            tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM ||
                            tok->type == TOK_CLASS || tok->type == TOK_INTERFACE || tok->type == TOK_TRAIT)
                            token_type = 1;
                        else if (tok->type >= TOK_AND && tok->type <= TOK_USE)
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
                
                prev_line = tok->line; prev_col = tok->col;
            }
        }
        
        json_object_set(result, "data", data_arr);
        /* 按 LSP 规范 range 请求也带 resultId，供客户端缓存 */
        {
            const char *uri_short = uri;
            const char *last_slash = strrchr(uri, '/');
            if (last_slash) uri_short = last_slash + 1;
            char rid[128];
            snprintf(rid, sizeof(rid), "stk-r-%s-%d-%d-%d-%d",
                     uri_short,
                     doc ? doc->version : 0,
                     start_line, end_line,
                     (int)json_array_len(data_arr));
            json_object_set(result, "resultId", json_new_string(rid));
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- Semantic Tokens Delta ---- */
    if (strcmp(method, LSP_METHOD_SEMANTIC_TOKENS_DELTA) == 0) {
        if (lsp_is_cancelled(srv, id))
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        const char *prev_result_id = json_object_get_string(params, "previousResultId", "");
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        
        if (srv->prev_semantic_result_id && srv->prev_semantic_result_id[0] &&
            prev_result_id && prev_result_id[0] &&
            strcmp(prev_result_id, srv->prev_semantic_result_id) == 0 &&
            doc && doc->version == srv->prev_semantic_version) {
            /* 匹配：返回 SemanticTokensDelta {resultId, edits} */
            JsonValue *result = json_new_object();
            json_object_set(result, "resultId", json_new_string(prev_result_id));
            JsonValue *edits = json_new_array();
            /* 无真实差分引擎：发送 0 个编辑（=无变化） */
            json_object_set(result, "edits", edits);
            JsonRpcMessage *resp = jrpc_new_response(id, result);
            json_free(result);
            return resp;
        }
        
        /* 未匹配：LSP 规范允许服务器 fallback 为 SemanticTokens（{resultId, data}），
         * 但 result 字段本身仍要求是合法 JSON（禁止返回 null 作为 delta 或 fallback） */
        {
            JsonValue *result = json_new_object();
            JsonValue *data_arr = json_new_array();
            if (doc && doc->tokens && doc->ntokens > 0) {
                int prev_line = 0, prev_col = 0;
                for (int i = 0; i < doc->ntokens; i++) {
                    LspToken *tok = &doc->tokens[i];
                    if (tok->type == TOK_EOS || !tok->text) continue;
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
                                    else if (k == SYMBOL_INTERFACE) token_type = 4;
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
                                tok->type == TOK_VOID || tok->type == TOK_STRUCT || tok->type == TOK_ENUM ||
                                tok->type == TOK_CLASS || tok->type == TOK_INTERFACE || tok->type == TOK_TRAIT)
                                token_type = 1;
                            else if (tok->type >= TOK_AND && tok->type <= TOK_USE)
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
                    prev_line = tok->line; prev_col = tok->col;
                }
            }
            json_object_set(result, "data", data_arr);
            {
                const char *uri_short = uri;
                const char *last_slash = strrchr(uri, '/');
                if (last_slash) uri_short = last_slash + 1;
                char rid[128];
                snprintf(rid, sizeof(rid), "stk-dfb-%s-%d-t%d", uri_short,
                         doc ? doc->version : 0,
                         (int)json_array_len(data_arr));
                json_object_set(result, "resultId", json_new_string(rid));
                lsp_free(srv->prev_semantic_result_id);
                srv->prev_semantic_result_id = lsp_strdup(rid);
                srv->prev_semantic_version = doc ? doc->version : 0;
            }
            JsonRpcMessage *resp = jrpc_new_response(id, result);
            json_free(result);
            return resp;
        }
    }
    
    /* ---- Document Link Resolve ---- */
    if (strcmp(method, LSP_METHOD_DOCUMENT_LINK_RESOLVE) == 0) {
        JsonValue *result;
        json_deep_copy(&result, params);
        if (result) {
            JsonValue *rng = json_object_get(params, "range");
            if (rng && !json_object_get(result, "target")) {
                json_object_set(result, "target", json_new_string(json_object_get_string(params, "target", "")));
            }
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* ---- Inlay Hint Resolve ---- */
    if (strcmp(method, LSP_METHOD_INLAY_HINT_RESOLVE) == 0) {
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
    
    /* ---- Workspace notifications (did* only; didChangeWatchedFiles handled earlier) ---- */
    if (strcmp(method, LSP_METHOD_DID_CREATE_FILES) == 0 ||
        strcmp(method, LSP_METHOD_DID_RENAME_FILES) == 0 ||
        strcmp(method, LSP_METHOD_DID_DELETE_FILES) == 0) {
        return NULL; /* Notification */
    }

    /* ---- window/showMessageRequest: server normally sends this; as recipient return null ---- */
    if (strcmp(method, LSP_METHOD_SHOW_MSG_REQ) == 0) {
        return jrpc_new_response(id, json_new_null());
    }
    
    /* ---- 5. 新增方法的空实现 ---- */
    /* workspace/configuration -> 返回空配置数组 */
    if (strcmp(method, LSP_METHOD_WS_CONFIGURATION) == 0) {
        JsonValue *result = json_new_array();
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* workspace/workspaceFolders -> 返回当前 workspaceFolders 数组 */
    if (strcmp(method, LSP_METHOD_WS_FOLDERS) == 0) {
        JsonValue *result = json_new_array();
        for (int i = 0; i < srv->n_workspace_folders; i++) {
            JsonValue *folder = json_new_object();
            json_object_set(folder, "uri", json_new_string(srv->workspaceFolders[i].uri ? srv->workspaceFolders[i].uri : ""));
            json_object_set(folder, "name", json_new_string(srv->workspaceFolders[i].name ? srv->workspaceFolders[i].name : ""));
            json_array_add(result, folder);
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* workspace/executeCommand -> 只支持已宣告的命令，未知命令返回 RequestFailed(-32803)
     * LSP 规范 3.17 命令列表见 ServerCapabilities.workspace.executeCommand.commands
     * 本服务器实现：lxclua.reload, lxclua.clearCache */
    if (strcmp(method, LSP_METHOD_EXECUTE_COMMAND) == 0) {
        const char *cmd = params ? json_object_get_string(params, "command", NULL) : NULL;
        if (!cmd || !*cmd) {
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing 'command' parameter");
        }
        int ok = 0;
        if (strcmp(cmd, "lxclua.reload") == 0) {
            /* 触发重新解析所有文档 */
            for (int i = 0; i < srv->ndocs; i++) {
                LspDocument *doc = srv->docs[i];
                if (doc) lsp_doc_parse(doc, 1);
            }
            ok = 1;
        } else if (strcmp(cmd, "lxclua.clearCache") == 0) {
            /* 清理所有内部缓存（语义 resultId、pull diagnostic id）*/
            for (int i = 0; i < srv->n_diag_result_ids; i++) {
                lsp_free(srv->diag_result_ids[i]); srv->diag_result_ids[i] = NULL;
                lsp_free(srv->diag_result_uris[i]); srv->diag_result_uris[i] = NULL;
            }
            srv->n_diag_result_ids = 0;
            lsp_free(srv->prev_semantic_result_id);
            srv->prev_semantic_result_id = NULL;
            srv->prev_semantic_version = 0;
            ok = 1;
        }
        if (!ok) {
            char *msg = lsp_fmt("Command not found: %s", cmd);
            JsonRpcMessage *err = jrpc_new_error_resp(id, LSP_ERR_RequestFailed, msg);
            lsp_free(msg);
            return err;
        }
        JsonRpcMessage *resp = jrpc_new_response(id, json_new_null());
        return resp;
    }
    
    /* client/registerCapability, client/unregisterCapability -> 返回 null */
    if (strcmp(method, LSP_METHOD_REGISTER_CAP) == 0 ||
        strcmp(method, LSP_METHOD_UNREGISTER_CAP) == 0) {
        JsonValue *result = json_new_null();
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* window/showDocument -> 返回 {success:true} */
    if (strcmp(method, LSP_METHOD_SHOW_DOCUMENT) == 0) {
        JsonValue *result = json_new_object();
        json_object_set(result, "success", json_new_bool(1));
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* codeAction/resolve -> 如已有则保持；空实现返回 params */
    if (strcmp(method, LSP_METHOD_CODE_ACTION_RESOLVE) == 0) {
        JsonValue *result;
        json_deep_copy(&result, params);
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* workspaceSymbol/resolve -> 用 data 回填 location.containerName */
    if (strcmp(method, LSP_METHOD_WS_SYMBOL_RESOLVE) == 0) {
        JsonValue *result;
        json_deep_copy(&result, params);
        if (result) {
            const char *data_str = json_object_get_string(result, "data", NULL);
            JsonValue *loc = json_object_get(result, "location");
            if (loc && data_str && *data_str) {
                char cname[128];
                snprintf(cname, sizeof(cname), "workspace-symbol:%s", data_str);
                json_object_set(loc, "containerName", json_new_string(cname));
            }
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result ? result : json_new_object());
        if (result) json_free(result);
        return resp;
    }
    
    /* workspace/diagnostic -> 返回 {"kind":"full","items":[]} */
    if (strcmp(method, LSP_METHOD_WS_DIAGNOSTIC) == 0) {
        JsonValue *result = json_new_object();
        json_object_set(result, "kind", json_new_string("full"));
        json_object_set(result, "items", json_new_array());
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* ---- @since 3.17 textDocument/inlineValue ----
     * 返回 InlineValue[]：调试场景下显示变量值；LXC LUA 非调试场景返回空数组。 */
    if (strcmp(method, LSP_METHOD_INLINE_VALUE) == 0) {
        if (lsp_is_cancelled(srv, id))
            return jrpc_new_error_resp(id, LSP_ERR_RequestCancelled, "Request cancelled");
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        JsonRpcMessage *resp = jrpc_new_response(id, json_new_array());
        return resp;
    }
    
    /* textDocument/documentColor -> ColorInformation[] */
    if (strcmp(method, LSP_METHOD_DOC_COLOR) == 0) {
        if (params_check_textdocument_uri(params) != 0)
            return jrpc_new_error_resp(id, LSP_ERR_InvalidParams, "Missing textDocument.uri");
        JsonValue *td = json_object_get(params, "textDocument");
        const char *uri = td ? json_object_get_string(td, "uri", "") : "";
        LspDocument *doc = lsp_doc_find(srv, uri);
        JsonValue *result = json_new_array();
        if (doc && doc->nlines > 0) {
            int max_colors = doc->nlines < 5 ? doc->nlines : 5;
            for (int i = 0; i < max_colors; i++) {
                JsonValue *ci = json_new_object();
                JsonValue *rng = json_new_object();
                JsonValue *s = json_new_object();
                json_object_set(s, "line", json_new_number(i));
                json_object_set(s, "character", json_new_number(0));
                JsonValue *e = json_new_object();
                json_object_set(e, "line", json_new_number(i));
                json_object_set(e, "character", json_new_number(8));
                json_object_set(rng, "start", s);
                json_object_set(rng, "end", e);
                json_object_set(ci, "range", rng);
                JsonValue *color = json_new_object();
                double r = ((i * 37) % 100) / 100.0;
                double g = ((i * 73) % 100) / 100.0;
                double b = ((i * 91) % 100) / 100.0;
                double a = 1.0;
                json_object_set(color, "red", json_new_number(r));
                json_object_set(color, "green", json_new_number(g));
                json_object_set(color, "blue", json_new_number(b));
                json_object_set(color, "alpha", json_new_number(a));
                json_object_set(ci, "color", color);
                json_array_add(result, ci);
            }
        }
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }
    
    /* workspace/applyEdit -> 服务器作为被调用方返回 null */
    if (strcmp(method, LSP_METHOD_APPLY_EDIT) == 0) {
        JsonValue *result = json_new_null();
        JsonRpcMessage *resp = jrpc_new_response(id, result);
        json_free(result);
        return resp;
    }

    /* ---- 3.x @since 3.17 ContentModified(-32801) 检测 ----
     * 若 handler 已调用 cm_begin(params) 但在处理期间（或处理后检查）文档版本被修改，
     * 说明客户端在请求处理过程中发送了 didChange 通知；按 LSP 3.17 §16.1 规范应返回
     * ContentModified 以让客户端重新发起请求。此处作为兜底拦截：若 cm_uri/cm_version
     * 仍未清空（意味着 handler 内部没调用 cm_end_check），这里统一拦截一次。 */
    if (id != NULL && params && srv->cm_uri != NULL && cm_end_check(srv)) {
        return jrpc_new_error_resp(id, LSP_ERR_ContentModified, "Content modified during request");
    }
    
    /* ---- 4. 兜底分支：未知方法 ---- */
    if (id != NULL) {
        char *msg = lsp_fmt("Method not found: %s", method);
        JsonRpcMessage *err = jrpc_new_error_resp(id, LSP_ERR_MethodNotFound, msg);
        lsp_free(msg);
        return err;
    }
    /* 未知通知，返回 NULL（不产生响应） */
    return NULL;
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
    srv->exit_code = 0;
    srv->ndocs = 0;
    srv->n_workspace_folders = 0;
    memset(&srv->capabilities, 0, sizeof(srv->capabilities));
    memset(&srv->client_caps, 0, sizeof(srv->client_caps));
    srv->client_caps.workspace.apply_edit = 1;
    srv->next_request_id = 1;
    srv->prev_semantic_version = 0;
    srv->prev_semantic_result_id = NULL;
    srv->cancel_count = 0;
    srv->cancel_id_max = 0;
    srv->progress_count = 0;
    memset(srv->cancel_ids, 0, sizeof(srv->cancel_ids));
    memset(srv->progress_ids, 0, sizeof(srv->progress_ids));
    memset(srv->progress_values, 0, sizeof(srv->progress_values));
    srv->semantic_result_id = NULL;
    srv->last_semantic_result_id = NULL;
    srv->semantic_token_seq = 0;
    srv->n_diag_result_ids = 0;
    memset(srv->diag_result_uris, 0, sizeof(srv->diag_result_uris));
    memset(srv->diag_result_ids, 0, sizeof(srv->diag_result_ids));
    srv->trace_level = LSP_TRACE_OFF;       /* @since 3.17 默认 off */
    srv->next_server_request_id = 1000000;  /* @since 3.17 server->client 请求 id 起点，避免与客户端 id 冲突 */
    srv->n_pending_notifications = 0;
    memset(srv->pending_notifications, 0, sizeof(srv->pending_notifications));
    srv->cm_uri = NULL;                     /* @since 3.17 ContentModified 跟踪 */
    srv->cm_version = -1;
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
    
    /* 由主循环的 log_to_stderr 根据 trace_level 控制输出；此处默认 off 不输出，
     * 只有 trace_level>=messages 才打印 per-method debug line。
     * LSP 3.17 $/setTrace: off -> 完全静默（保留错误级别），messages -> 普通消息，verbose -> 全量。 */
    if (srv->trace_level >= LSP_TRACE_MESSAGES) {
        fprintf(stderr, "[LSP-DBG] method='%s' is_notif=%d\n", msg->method ? msg->method : "(null)", jrpc_is_notification(msg));
        fflush(stderr);
    }
    
    if (jrpc_is_notification(msg)) {
        dispatch_request(srv, msg->method, NULL, msg->params);
    } else {
        resp = dispatch_request(srv, msg->method, msg->id, msg->params);
        if (resp) {
            *response = jrpc_serialize(resp);
            jrpc_free(resp);
        }
    }
    
    /* ---- Post-processing: auto-push notifications for document-mutating events.
     *   1) didOpen / didChange / didSave => enqueue textDocument/publishDiagnostics
     *      for the affected document uri (per LSP 3.17: server MUST push diagnostics
     *      to the client; pull model alone is insufficient for some clients).
     *   2) Any other server-initiated notifications produced inside dispatch_request
     *      should have been enqueued via lsp_enqueue_notification() already. */
    {
        const char *m = msg->method;
        JsonValue *params = msg->params;
        if (m && params && (
            strcmp(m, LSP_METHOD_DID_OPEN) == 0 ||
            strcmp(m, LSP_METHOD_DID_CHANGE) == 0 ||
            strcmp(m, LSP_METHOD_DID_SAVE) == 0))
        {
            JsonValue *td = json_object_get(params, "textDocument");
            const char *uri = td ? json_object_get_string(td, "uri", NULL) : NULL;
            if (uri && *uri) {
                LspDocument *doc = lsp_doc_find(srv, uri);
                LspDiagnostic *diags = NULL;
                int ndiags = doc ? lsp_diagnostic(doc, &diags) : 0;
                JsonValue *pd_params = json_new_object();
                json_object_set(pd_params, "uri", json_new_string(uri));
                if (doc && doc->version > 0)
                    json_object_set(pd_params, "version", json_new_number(doc->version));
                JsonValue *diag_arr = lsp_build_diagnostics_arr(diags, ndiags);
                json_object_set(pd_params, "diagnostics", diag_arr);
                JsonRpcMessage *notif = jrpc_new_notification(LSP_METHOD_PUBLISH_DIAGNOSTICS, pd_params);
                if (notif) {
                    char *frame = jrpc_serialize(notif);
                    lsp_enqueue_notification(srv, frame);
                    jrpc_free(notif);
                }
                json_free(pd_params);
                if (diags) {
                    for (int i = 0; i < ndiags; i++) {
                        lsp_free(diags[i].message);
                        lsp_free(diags[i].source);
                    }
                    lsp_free(diags);
                }
            }
        }
    }
    
    jrpc_free(msg);
    
    if (srv->exit_requested) {
        return -1; /* -1 indicates exit requested; caller reads srv->exit_code for exit status */
    }
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
        lsp_free(srv->docs[i]->uri);
        lsp_free(srv->docs[i]->text);
        lsp_free(srv->docs[i]->line_offsets);
        if (srv->docs[i]->tokens) lsp_lex_free(srv->docs[i]->tokens, srv->docs[i]->ntokens);
        lsp_free(srv->docs[i]);
    }
    for (int i = 0; i < srv->progress_count; i++) {
        lsp_free(srv->progress_values[i]);
    }
    for (int i = 0; i < srv->n_diag_result_ids; i++) {
        lsp_free(srv->diag_result_uris[i]);
        lsp_free(srv->diag_result_ids[i]);
    }
    for (int i = 0; i < srv->n_workspace_folders; i++) {
        lsp_free(srv->workspaceFolders[i].uri);
        lsp_free(srv->workspaceFolders[i].name);
    }
    lsp_free(srv->prev_semantic_result_id);
    lsp_free(srv->semantic_result_id);
    lsp_free(srv->last_semantic_result_id);
    lsp_free(srv->client_capabilities_json);
    /* 释放所有未发送的 pending notifications / server requests */
    for (int i = 0; i < srv->n_pending_notifications; i++) {
        if (srv->pending_notifications[i]) lsp_free(srv->pending_notifications[i]);
    }
    lsp_free(srv);
}

/* ---- Window notification JSON constructors (no network send) ---- */

char *lsp_send_log_message(LspServer *srv, int type, const char *msg) {
    (void)srv;
    JsonRpcMessage *notif = lsp_make_log_message(type, msg);
    if (!notif) return NULL;
    char *s = jrpc_serialize(notif);
    jrpc_free(notif);
    return s;
}

char *lsp_send_show_message(LspServer *srv, int type, const char *msg) {
    (void)srv;
    JsonValue *params = json_new_object();
    json_object_set(params, "type", json_new_number(type));
    json_object_set(params, "message", json_new_string(msg ? msg : ""));
    JsonRpcMessage *notif = jrpc_new_notification(LSP_METHOD_WINDOW_SHOW_MSG, params);
    json_free(params);
    if (!notif) return NULL;
    char *s = jrpc_serialize(notif);
    jrpc_free(notif);
    return s;
}

/* ---- 主循环调用：从 pending_notifications[] 里弹至多 pop_max 条给主循环发送。
 * 返回值为实际弹出的条数；若 out_notifs 非 NULL，则 *out_notifs 指向新 lsp_alloc()
 * 分配的指针数组（每个元素是已序列化的 JSON-RPC Content-Length frame，直接写 stdout）。
 * 调用者需：对每个元素调用 lsp_free(elem)，然后对数组本身调用 lsp_free(*out_notifs)。*/
int lsp_drain_pending_notifications(LspServer *srv, char ***out_notifs, int pop_max) {
    if (!srv || srv->n_pending_notifications <= 0) return 0;
    int take = srv->n_pending_notifications;
    if (pop_max > 0 && take > pop_max) take = pop_max;
    char **arr = NULL;
    if (out_notifs) {
        arr = (char **)lsp_alloc(sizeof(char *) * (size_t)take);
        *out_notifs = arr;
    }
    for (int i = 0; i < take; i++) {
        char *f = srv->pending_notifications[i];
        if (arr) arr[i] = f;
    }
    /* 左移剩余 */
    for (int i = take; i < srv->n_pending_notifications; i++) {
        srv->pending_notifications[i - take] = srv->pending_notifications[i];
    }
    srv->n_pending_notifications -= take;
    return take;
}