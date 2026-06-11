/*
** LXCLUA Language Server Protocol Implementation
** Main header file - defines all core types and APIs for the LSP server.
*/

#ifndef lspsrv_h
#define lspsrv_h

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* ---- LSP Protocol Constants ---- */
#define LSP_METHOD_INITIALIZE          "initialize"
#define LSP_METHOD_INITIALIZED         "initialized"
#define LSP_METHOD_SHUTDOWN            "shutdown"
#define LSP_METHOD_EXIT                "exit"
#define LSP_METHOD_DID_OPEN            "textDocument/didOpen"
#define LSP_METHOD_DID_CHANGE          "textDocument/didChange"
#define LSP_METHOD_DID_CLOSE           "textDocument/didClose"
#define LSP_METHOD_DID_SAVE            "textDocument/didSave"
#define LSP_METHOD_COMPLETION          "textDocument/completion"
#define LSP_METHOD_HOVER               "textDocument/hover"
#define LSP_METHOD_DEFINITION          "textDocument/definition"
#define LSP_METHOD_REFERENCES          "textDocument/references"
#define LSP_METHOD_DOCUMENT_HIGHLIGHT  "textDocument/documentHighlight"
#define LSP_METHOD_DOCUMENT_SYMBOL     "textDocument/documentSymbol"
#define LSP_METHOD_SIGNATURE_HELP      "textDocument/signatureHelp"
#define LSP_METHOD_RENAME              "textDocument/rename"
#define LSP_METHOD_FORMATTING          "textDocument/formatting"
#define LSP_METHOD_CODE_ACTION         "textDocument/codeAction"
#define LSP_METHOD_DIAGNOSTIC          "textDocument/diagnostic"
#define LSP_METHOD_PUBLISH_DIAGNOSTICS "textDocument/publishDiagnostics"
#define LSP_METHOD_FOLDING_RANGE       "textDocument/foldingRange"
#define LSP_METHOD_SEMANTIC_TOKENS     "textDocument/semanticTokens/full"
#define LSP_METHOD_WORKSPACE_CFG_CHG   "workspace/didChangeConfiguration"
#define LSP_METHOD_PREPARE_RENAME      "textDocument/prepareRename"
#define LSP_METHOD_TYPE_DEFINITION     "textDocument/typeDefinition"
#define LSP_METHOD_IMPLEMENTATION      "textDocument/implementation"
#define LSP_METHOD_WORKSPACE_SYMBOL    "workspace/symbol"
#define LSP_METHOD_SELECTION_RANGE     "textDocument/selectionRange"
#define LSP_METHOD_COMPLETION_RESOLVE  "completionItem/resolve"
#define LSP_METHOD_LINKED_EDITING      "textDocument/linkedEditingRange"
#define LSP_METHOD_DECLARATION         "textDocument/declaration"
#define LSP_METHOD_CODE_LENS           "textDocument/codeLens"
#define LSP_METHOD_CODE_LENS_RESOLVE   "codeLens/resolve"
#define LSP_METHOD_DOCUMENT_LINK       "textDocument/documentLink"
#define LSP_METHOD_DOCUMENT_LINK_RESOLVE "documentLink/resolve"
#define LSP_METHOD_INLAY_HINT          "textDocument/inlayHint"
#define LSP_METHOD_INLAY_HINT_RESOLVE  "inlayHint/resolve"
#define LSP_METHOD_CALL_HIERARCHY_PREPARE  "textDocument/prepareCallHierarchy"
#define LSP_METHOD_CALL_HIERARCHY_INCOMING "callHierarchy/incomingCalls"
#define LSP_METHOD_CALL_HIERARCHY_OUTGOING "callHierarchy/outgoingCalls"
#define LSP_METHOD_TYPE_HIERARCHY_PREPARE  "textDocument/prepareTypeHierarchy"
#define LSP_METHOD_TYPE_HIERARCHY_SUPERTYPES "typeHierarchy/supertypes"
#define LSP_METHOD_TYPE_HIERARCHY_SUBTYPES   "typeHierarchy/subtypes"
#define LSP_METHOD_COLOR_PRESENTATION   "textDocument/colorPresentation"
#define LSP_METHOD_MONIKER             "textDocument/moniker"
#define LSP_METHOD_WILL_SAVE           "textDocument/willSave"
#define LSP_METHOD_WILL_SAVE_WAIT      "textDocument/willSaveWaitUntil"
#define LSP_METHOD_EXECUTE_COMMAND     "workspace/executeCommand"
#define LSP_METHOD_DID_CREATE_FILES    "workspace/didCreateFiles"
#define LSP_METHOD_DID_RENAME_FILES    "workspace/didRenameFiles"
#define LSP_METHOD_DID_DELETE_FILES    "workspace/didDeleteFiles"
#define LSP_METHOD_WILL_CREATE_FILES   "workspace/willCreateFiles"
#define LSP_METHOD_WILL_RENAME_FILES   "workspace/willRenameFiles"
#define LSP_METHOD_WILL_DELETE_FILES   "workspace/willDeleteFiles"
#define LSP_METHOD_WATCHED_FILES_CHG   "workspace/didChangeWatchedFiles"
#define LSP_METHOD_PROGRESS_START      "window/workDoneProgress/create"
#define LSP_METHOD_PROGRESS_REPORT     "$/progress"
#define LSP_METHOD_SHOW_MSG_REQ        "window/showMessageRequest"
#define LSP_METHOD_ON_TYPE_FORMATTING  "textDocument/onTypeFormatting"
#define LSP_METHOD_RANGE_FORMATTING    "textDocument/rangeFormatting"
#define LSP_METHOD_SEMANTIC_TOKENS_RANGE "textDocument/semanticTokens/range"
#define LSP_METHOD_SEMANTIC_TOKENS_DELTA "textDocument/semanticTokens/full/delta"
#define LSP_METHOD_WINDOW_LOG          "window/logMessage"
#define LSP_METHOD_WINDOW_SHOW_MSG     "window/showMessage"
#define LSP_METHOD_CANCEL_REQUEST      "$/cancelRequest"

/* LSP CompletionItemKind */
enum {
    COMPLETION_TEXT        = 1,
    COMPLETION_METHOD      = 2,
    COMPLETION_FUNCTION    = 3,
    COMPLETION_CONSTRUCTOR = 4,
    COMPLETION_FIELD       = 5,
    COMPLETION_VARIABLE    = 6,
    COMPLETION_CLASS       = 7,
    COMPLETION_INTERFACE   = 8,
    COMPLETION_MODULE      = 9,
    COMPLETION_PROPERTY    = 10,
    COMPLETION_UNIT        = 11,
    COMPLETION_VALUE       = 12,
    COMPLETION_ENUM        = 13,
    COMPLETION_KEYWORD     = 14,
    COMPLETION_SNIPPET     = 15,
    COMPLETION_COLOR       = 16,
    COMPLETION_FILE        = 17,
    COMPLETION_REFERENCE   = 18,
    COMPLETION_FOLDER      = 19,
    COMPLETION_ENUM_MEMBER = 20,
    COMPLETION_CONSTANT    = 21,
    COMPLETION_STRUCT      = 22,
    COMPLETION_EVENT       = 23,
    COMPLETION_OPERATOR    = 24,
    COMPLETION_TYPE_PARAM  = 25,
};

/* LSP SymbolKind */
enum {
    SYMBOL_FILE       = 1,
    SYMBOL_MODULE     = 2,
    SYMBOL_NAMESPACE  = 3,
    SYMBOL_PACKAGE    = 4,
    SYMBOL_CLASS      = 5,
    SYMBOL_METHOD     = 6,
    SYMBOL_PROPERTY   = 7,
    SYMBOL_FIELD      = 8,
    SYMBOL_CONSTRUCTOR= 9,
    SYMBOL_ENUM       = 10,
    SYMBOL_INTERFACE  = 11,
    SYMBOL_FUNCTION   = 12,
    SYMBOL_VARIABLE   = 13,
    SYMBOL_CONSTANT   = 14,
    SYMBOL_STRING     = 15,
    SYMBOL_NUMBER     = 16,
    SYMBOL_BOOLEAN    = 17,
    SYMBOL_ARRAY      = 18,
    SYMBOL_OBJECT     = 19,
    SYMBOL_KEY        = 20,
    SYMBOL_NULL       = 21,
    SYMBOL_ENUM_MEMBER= 22,
    SYMBOL_STRUCT     = 23,
    SYMBOL_EVENT      = 24,
    SYMBOL_OPERATOR   = 25,
    SYMBOL_TYPE_PARAM = 26,
};

/* LSP DiagnosticSeverity */
enum {
    SEVERITY_ERROR   = 1,
    SEVERITY_WARNING = 2,
    SEVERITY_INFO    = 3,
    SEVERITY_HINT    = 4,
};

/* LSP InsertTextFormat */
enum {
    INSERT_TEXT_PLAIN   = 1,
    INSERT_TEXT_SNIPPET = 2,
};

/* LSP MessageType */
enum {
    MSG_TYPE_ERROR   = 1,
    MSG_TYPE_WARNING = 2,
    MSG_TYPE_INFO    = 3,
    MSG_TYPE_LOG     = 4,
};

/*
** ---- JSON-RPC 2.0 Data Structures ----
*/

/** @brief JSON value types */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

/** @brief Forward declaration */
typedef struct JsonValue JsonValue;
typedef struct JsonMember JsonMember;

/** @brief JSON key-value member */
struct JsonMember {
    char *key;
    JsonValue *value;
};

/** @brief JSON value (supports all JSON types) */
struct JsonValue {
    JsonType type;
    union {
        int bool_val;
        double num_val;
        char *str_val;
        struct {
            JsonValue **items;
            int count;
        } arr;
        struct {
            JsonMember *members;
            int count;
        } obj;
    } as;
};

/** @brief JSON-RPC message */
typedef struct {
    char *jsonrpc;
    /* For requests/notifications: method, params (optional), id (optional for notifications) */
    char *method;
    JsonValue *params;
    /* For responses: result (optional), error (optional) */
    JsonValue *result;
    JsonValue *error;
    /* id can be string, number, or null */
    JsonValue *id;
} JsonRpcMessage;

/** @brief JSON-RPC error codes */
enum {
    JRPC_PARSE_ERROR     = -32700,
    JRPC_INVALID_REQUEST = -32600,
    JRPC_METHOD_NOT_FOUND= -32601,
    JRPC_INVALID_PARAMS  = -32602,
    JRPC_INTERNAL_ERROR  = -32603,
    JRPC_SERVER_NOT_INIT = -32002,
    JRPC_REQUEST_CANCELLED= -32800,
    JRPC_CONTENT_MODIFIED = -32801,
};

/*
** ---- LXCLUA Lexer Tokens (matches compiler/llex.h) ----
*/

/** @brief LXCLUA token types (subset of compiler tokens) */
typedef enum {
    /* Single-char tokens map to their ASCII values (< 256) */
    /* Multi-char tokens start at 257 */
    TOK_AND         = 257, TOK_ASM, TOK_ASYNC, TOK_AWAIT, TOK_BOOL,
    TOK_BREAK, TOK_CASE, TOK_CATCH, TOK_CHAR, TOK_COMMAND,
    TOK_CONCEPT, TOK_CONST, TOK_CONTINUE, TOK_DEFAULT,
    TOK_DEFER, TOK_DO, TOK_DOUBLE, TOK_ELSE, TOK_ELSEIF,
    TOK_END, TOK_ENUM, TOK_EXPORT, TOK_FALSE, TOK_FINALLY,
    TOK_TYPE_FLOAT, TOK_FOR, TOK_FUNCTION,
    TOK_GLOBAL, TOK_GOTO, TOK_IF, TOK_IN, TOK_TYPE_INT,
    TOK_IS, TOK_INSTANCEOF, TOK_KEYWORD, TOK_LAMBDA,
    TOK_LOCAL, TOK_LONG, TOK_NAMESPACE, TOK_NIL, TOK_NOT,
    TOK_OPERATOR, TOK_OR,
    TOK_REPEAT, TOK_REQUIRES,
    TOK_RETURN, TOK_STRUCT, TOK_SUPERSTRUCT, TOK_SWITCH,
    TOK_TAKE, TOK_THEN, TOK_TRUE, TOK_TRY, TOK_UNTIL,
    TOK_USING, TOK_VOID, TOK_WHEN, TOK_WHILE, TOK_WITH, TOK_LET,

    /* Operators */
    TOK_IDIV, TOK_CONCAT, TOK_DOTS, TOK_EQ, TOK_GE, TOK_LE, TOK_NE,
    TOK_SHL, TOK_SHR, TOK_PIPE, TOK_REVPIPE, TOK_SAFEPIPE,
    TOK_DBCOLON, TOK_EOS,
    TOK_MEAN, TOK_WALRUS, TOK_ARROW,
    TOK_ADDEQ, TOK_SUBEQ, TOK_MULEQ, TOK_DIVEQ, TOK_IDIVEQ,
    TOK_MODEQ, TOK_BANDEQ, TOK_BOREQ, TOK_BXOREQ,
    TOK_SHREQ, TOK_SHLEQ, TOK_CONCATEQ,
    TOK_PLUSPLUS, TOK_OPTCHAIN, TOK_NULLCOAL, TOK_NULLCOALEQ,
    TOK_POWEQ, TOK_SPACESHIP, TOK_DOLLAR, TOK_DOLLDOLL,

    /* Value tokens */
    TOK_FLT, TOK_INT, TOK_NAME, TOK_STRING, TOK_INTERPSTRING, TOK_RAWSTRING,
    TOK_COMMENT, TOK_MCOMMENT, TOK_HASHBANG,
} LspTokenType;

/** @brief Token structure */
typedef struct {
    LspTokenType type;
    int line;           /**< 0-based line number */
    int col;            /**< 0-based column (byte offset in line) */
    int offset;         /**< byte offset from start of document */
    int len;            /**< length of token text */
    char *text;         /**< owned copy of token text */
    union {
        double fval;
        int64_t ival;
    } num;
} LspToken;

/*
** ---- LXCLUA AST Structures ----
*/

/** @brief AST node types for LXCLUA */
typedef enum {
    /* Statements */
    AST_STMT_LIST,
    AST_STMT_LOCAL,
    AST_STMT_ASSIGN,
    AST_STMT_CALL,
    AST_STMT_IF,
    AST_STMT_WHILE,
    AST_STMT_REPEAT,
    AST_STMT_FOR,
    AST_STMT_FOR_IN,
    AST_STMT_RETURN,
    AST_STMT_FUNCTION,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_GOTO,
    AST_STMT_LABEL,
    AST_STMT_SWITCH,
    AST_STMT_CASE,
    AST_STMT_DEFAULT,
    AST_STMT_TRY,
    AST_STMT_CATCH,
    AST_STMT_FINALLY,
    AST_STMT_DEFER,
    AST_STMT_CLASS,
    AST_STMT_STRUCT,
    AST_STMT_SUPERSTRUCT,
    AST_STMT_ENUM,
    AST_STMT_NAMESPACE,
    AST_STMT_COMPOUND_ASSIGN,
    AST_STMT_ASYNC_FUNCTION,
    AST_STMT_GLOBAL,
    AST_STMT_EXPORT,

    /* Expressions */
    AST_EXPR_BINOP,
    AST_EXPR_UNOP,
    AST_EXPR_NAME,
    AST_EXPR_LITERAL_NIL,
    AST_EXPR_LITERAL_INT,
    AST_EXPR_LITERAL_FLT,
    AST_EXPR_LITERAL_STR,
    AST_EXPR_LITERAL_BOOL,
    AST_EXPR_TABLE,
    AST_EXPR_CALL,
    AST_EXPR_INDEX,
    AST_EXPR_METHOD,
    AST_EXPR_ARROW_FUNC,
    AST_EXPR_LAMBDA,
    AST_EXPR_ASYNC_FUNC,
    AST_EXPR_AWAIT,
    AST_EXPR_TERNARY,
    AST_EXPR_OPTCHAIN,
    AST_EXPR_NULLCOAL,
    AST_EXPR_COMPREHENSION,
    AST_EXPR_SPREAD,
    AST_EXPR_DESTRUCT_ASSIGN,
    AST_EXPR_PIPE,
} AstNodeType;

/** @brief AST node */
typedef struct AstNode {
    AstNodeType type;
    int line;
    int col;
    int end_line;
    int end_col;
    char *str_val;        /**< Name, string value, or keyword */
    int64_t int_val;
    double flt_val;
    struct AstNode *next;   /**< Next sibling (for lists/blocks) */
    struct AstNode *children[4]; /**< Child nodes */
} AstNode;

/*
** ---- Symbol Table ----
*/

/** @brief Symbol for LSP features */
typedef struct LspSymbol {
    char *name;
    int kind;                /**< LSP SymbolKind */
    int line;                /**< 0-based definition line */
    int col;                 /**< 0-based definition column */
    int end_line;
    int end_col;
    char *detail;            /**< Type or kind description */
    char *documentation;     /**< Optional doc string */
    struct LspSymbol **children;
    int nchildren;
    int scope_start_line;    /**< Scope range start */
    int scope_end_line;      /**< Scope range end */
} LspSymbol;

/** @brief Scope entry for variables */
typedef struct {
    char *name;
    int def_line;
    int def_col;
    int kind;                /**< LSP SymbolKind */
    char *type_hint;
} LspVarInfo;

/** @brief Completion item */
typedef struct {
    char *label;
    int kind;
    char *detail;
    char *documentation;
    char *insert_text;
    int insert_text_format;
    int sort_text_priority;  /**< Higher = earlier in list */
} LspCompletionItem;

/** @brief Diagnostic entry */
typedef struct {
    int severity;            /**< SEVERITY_ERROR/WARNING/INFO/HINT */
    int line_start;
    int col_start;
    int line_end;
    int col_end;
    char *message;
    char *source;
} LspDiagnostic;

/*
** ---- Document ----
*/

/** @brief Maximum documents tracked simultaneously */
#define MAX_DOCUMENTS 64

/** @brief Managed document */
typedef struct LspDocument {
    char *uri;
    char *text;
    size_t text_len;
    int version;
    int open;
    /* Line offset cache for fast offset->line/col conversion */
    int *line_offsets;
    int nlines;
    /* Parse results */
    LspToken *tokens;
    int ntokens;
    AstNode *ast;
    LspSymbol **symbols;
    int nsymbols;
    LspVarInfo *vars;
    int nvars;
    int var_cap;
    LspDiagnostic *diagnostics;
    int ndiags;
    int diag_cap;
    /* Preprocessor info */
    char **defined_globals;
    int ndefined_globals;
    char **imports;
    int nimports;
} LspDocument;

/*
** ---- LSP Server State ----
*/

typedef struct LspServer {
    /* Connection state */
    int initialized;
    int shutdown;
    int exit_requested;
    /* Document store */
    LspDocument *docs[MAX_DOCUMENTS];
    int ndocs;
    /* Server capabilities (sent to client) */
    struct {
        int hover;
        int completion;
        int completion_trigger;
        int definition;
        int type_definition;
        int implementation;
        int references;
        int document_highlight; 
        int document_symbol;
        int signature_help;
        int rename;
        int prepare_rename;
        int formatting;
        int code_action;
        int diagnostic;
        int folding_range;
        int semantic_tokens;
        int workspace_symbol;
        int selection_range;
        int linked_editing;
        int declaration;
        int code_lens;
        int document_link;
        int inlay_hint;
        int call_hierarchy;
        int type_hierarchy;
        int color_presentation;
        int moniker;
        int on_type_formatting;
        int range_formatting;
    } capabilities;
    /* Client capabilities (received from client) */
    struct {
        int supports_snippets;
        int supports_deprecated;
        int supports_preselect;
        int supports_tag_support;
        int supports_documentation;
        int supports_resolve;
        int supports_insert_replace;
        int supports_label_details;
    } client_caps;
    /* Pending request tracking */
    int next_request_id;
    /* Semantic tokens delta state */
    int prev_semantic_version;   /**< 上一次语义标记请求时的文档版本 */
    char *prev_semantic_result_id; /**< 上一次语义标记请求的 resultId */
} LspServer;

/*
** ---- Function Declarations ----
*/

/* ---- lspsrv_json.c ---- */
JsonValue *json_parse(const char *src, int len);
void json_free(JsonValue *v);
char *json_stringify(JsonValue *v);
JsonValue *json_new_null(void);
JsonValue *json_new_bool(int val);
JsonValue *json_new_number(double val);
JsonValue *json_new_string(const char *s);
JsonValue *json_new_array(void);
JsonValue *json_new_object(void);
void json_array_add(JsonValue *arr, JsonValue *item);
void json_object_set(JsonValue *obj, const char *key, JsonValue *val);
JsonValue *json_object_get(JsonValue *obj, const char *key);
int json_object_get_int(JsonValue *obj, const char *key, int def);
double json_object_get_number(JsonValue *obj, const char *key, double def);
const char *json_object_get_string(JsonValue *obj, const char *key, const char *def);
int json_object_get_bool(JsonValue *obj, const char *key, int def);
JsonValue *json_array_get(JsonValue *arr, int idx);
int json_array_len(JsonValue *arr);
void json_deep_copy(JsonValue **dst, JsonValue *src);

/* ---- lspsrv_main.c ---- */
JsonRpcMessage *jrpc_parse(const char *data, int len);
char *jrpc_serialize(JsonRpcMessage *msg);
void jrpc_free(JsonRpcMessage *msg);
JsonRpcMessage *jrpc_new_response(JsonValue *id, JsonValue *result);
JsonRpcMessage *jrpc_new_error_resp(JsonValue *id, int code, const char *message);
JsonRpcMessage *jrpc_new_notification(const char *method, JsonValue *params);
int jrpc_is_notification(JsonRpcMessage *msg);
int jrpc_is_response(JsonRpcMessage *msg);

/* ---- lspsrv_proto.c ---- */
void *lsp_init(void);  /* returns LspServer* */
int lsp_handle_message(void *server, const char *data, int len, char **response);
void lsp_shutdown(void *server);
void lsp_srv_free(void *server);

/* ---- lspsrv_doc.c ---- */
int lsp_doc_open(void *server, const char *uri, const char *text, int version);
int lsp_doc_change(void *server, const char *uri, const char *text, int version);
int lsp_doc_close(void *server, const char *uri);
LspDocument *lsp_doc_find(void *server, const char *uri);
void lsp_doc_parse(LspDocument *doc, int for_diagnostics);
/** @brief 向文档添加诊断信息 */
void lsp_doc_add_diag(LspDocument *doc, int severity, int line_start, int col_start,
                      int line_end, int col_end, const char *message, const char *source);

/* ---- lspsrv_kwdb.c ---- */
typedef struct {
    char *name;
    int kind;
    char *detail;
    char *documentation;
    char *snippet;
} LspKeywordEntry;

int lsp_kwdb_get_keywords(LspKeywordEntry **out);
int lsp_kwdb_get_builtins(LspKeywordEntry **out);
int lsp_kwdb_get_stdlib(LspKeywordEntry **out);
const char *lsp_kwdb_find_doc(const char *name);

/* ---- lspsrv_complete.c ---- */
int lsp_completion(LspDocument *doc, int line, int col, LspCompletionItem **items);

/* ---- lspsrv_hover.c ---- */
char *lsp_hover(LspDocument *doc, int line, int col);

/* ---- lspsrv_definition.c ---- */
char *lsp_get_symbol_at(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);

/* ---- lspsrv_diagnostic.c ---- */
int lsp_diagnostic(LspDocument *doc, LspDiagnostic **diags);

/* ---- lspsrv_format.c ---- */
char *lsp_format(LspDocument *doc, int tab_size, int insert_spaces);

/* ---- lspsrv_rename.c ---- */
int lsp_rename(LspDocument *doc, int line, int col, const char *new_name);

/* ---- lspsrv_signature.c ---- */
char *lsp_signature_help(LspDocument *doc, int line, int col);

/* ---- lspsrv_reference.c ---- */
int lsp_find_references(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);

/* ---- lspsrv_highlight.c ---- */
int lsp_document_highlight(LspDocument *doc, int line, int col, int **out_kinds, int **out_lines, int **out_cols, int *count);

/* ---- lspsrv_symbol.c (documentSymbol) ---- */
int lsp_document_symbol(LspDocument *doc, LspSymbol ***out_symbols);

/* ---- lspsrv_folding.c (foldingRange) ---- */
int lsp_folding_range(LspDocument *doc, int **out_start_lines, int **out_end_lines, int *count);

/* ---- lspsrv_semantic.c (semanticTokens) ---- */
char *lsp_semantic_tokens(LspDocument *doc);

/* ---- lspsrv_codeaction.c (codeAction) ---- */
int lsp_code_action(LspDocument *doc, int line, int col, LspDiagnostic **diagnostics_list, int *count);

/* ---- lspsrv_preparename.c (prepareRename) ---- */
int lsp_prepare_rename(LspDocument *doc, int line, int col, int *out_line, int *out_col, int *out_end_line, int *out_end_col);

/* ---- lspsrv_typedef.c (typeDefinition) ---- */
int lsp_type_definition(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);

/* ---- lspsrv_impl.c (implementation) ---- */
int lsp_find_implementation(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);

/* ---- lspsrv_wssymbol.c (workspace/symbol) ---- */
int lsp_workspace_symbol(void *server, const char *query, LspSymbol ***out_symbols);

/* ---- lspsrv_selrange.c (selectionRange) ---- */
int lsp_selection_range(LspDocument *doc, int npositions, int *lines, int *cols, int **out_start_lines, int **out_end_lines);

/* ---- lspsrv_linked.c (linkedEditingRange) ---- */
int lsp_linked_editing_range(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count);

/* ---- Diagnostic push ---- */
void lsp_publish_diagnostics(void *server, const char *uri);
void lsp_send_log(void *server, int type, const char *fmt, ...);
void lsp_show_message(void *server, int type, const char *message);

/* ---- lspsrv_util.c ---- */
char *lsp_strdup(const char *s);
void *lsp_alloc(size_t sz);
void *lsp_realloc(void *p, size_t sz);
void lsp_free(void *p);
char *lsp_str_append(char *dst, const char *src);
char *lsp_vfmt(const char *fmt, va_list ap);
char *lsp_fmt(const char *fmt, ...);
int lsp_offset_to_linecol(const char *text, int offset, int *line, int *col);
int lsp_linecol_to_offset(const char *text, int line, int col);
int lsp_is_ident_char(int c);
int lsp_is_ident_start(int c);
char *lsp_get_word_at(const char *text, int offset, int *start, int *end);
char *lsp_get_line_text(const char *text, int offset);
void lsp_build_line_offsets(const char *text, int len, int **out_offsets, int *out_nlines);

/* ---- lspsrv_lexer.c ---- */
void lsp_lex(const char *src, int len, LspToken **out_tokens, int *out_ntokens);
void lsp_lex_free(LspToken *tokens, int ntokens);

/* ---- lspsrv_kwdb.c ---- */
int lsp_kwdb_get_snippets(LspKeywordEntry **out);

/* ---- lspsrv_declaration.c (declaration) ---- */
int lsp_declaration(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri);

/* ---- lspsrv_codelens.c (codeLens) ---- */
int lsp_code_lens(LspDocument *doc, int **out_lines, int **out_cols, char ***out_titles, char ***out_commands, int *count);

/* ---- lspsrv_doclink.c (documentLink) ---- */
int lsp_document_link(LspDocument *doc, int **out_start_lines, int **out_start_cols, int **out_end_lines, int **out_end_cols, char ***out_targets, int *count);

/* ---- lspsrv_inlayhint.c (inlayHint) ---- */
int lsp_inlay_hint(LspDocument *doc, int start_line, int end_line, char ***out_labels, int **out_lines, int **out_cols, int *count);

/* ---- lspsrv_callhierarchy.c (callHierarchy) ---- */
int lsp_prepare_call_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col);
int lsp_call_hierarchy_incoming(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count);
int lsp_call_hierarchy_outgoing(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count);

/* ---- lspsrv_typehierarchy.c (typeHierarchy) ---- */
int lsp_prepare_type_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col);
int lsp_type_hierarchy_supertypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count);
int lsp_type_hierarchy_subtypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count);

/* ---- lspsrv_color.c (colorPresentation) ---- */
int lsp_color_presentation(LspDocument *doc, int line, int col, char ***out_labels, int *count);

/* ---- lspsrv_moniker.c (moniker) ---- */
int lsp_moniker(LspDocument *doc, int line, int col, char ***out_schemes, char ***out_identifiers, int *count);

/* ---- lspsrv_onformat.c (onTypeFormatting + rangeFormatting) ---- */
char *lsp_on_type_formatting(LspDocument *doc, int line, int col, const char *ch, int tab_size, int insert_spaces);
char *lsp_range_formatting(LspDocument *doc, int start_line, int start_col, int end_line, int end_col, int tab_size, int insert_spaces);

/* ---- Extended completion provider ---- */
int lsp_completion_resolve(LspCompletionItem *item, int *has_tags, int *deprecated);

#endif