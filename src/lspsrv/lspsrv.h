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
/* LSP 3.17 additional methods */
#define LSP_METHOD_WS_DIAGNOSTIC       "workspace/diagnostic"
#define LSP_METHOD_WS_SYMBOL_RESOLVE   "workspaceSymbol/resolve"
#define LSP_METHOD_REGISTER_CAP        "client/registerCapability"
#define LSP_METHOD_UNREGISTER_CAP      "client/unregisterCapability"
#define LSP_METHOD_SET_TRACE           "$/setTrace"
#define LSP_METHOD_LOG_TRACE           "$/logTrace"
#define LSP_METHOD_WS_CONFIGURATION    "workspace/configuration"
#define LSP_METHOD_SHOW_DOCUMENT       "window/showDocument"
#define LSP_METHOD_WS_FOLDERS          "workspace/workspaceFolders"
#define LSP_METHOD_DOC_COLOR           "textDocument/documentColor"
#define LSP_METHOD_CODE_ACTION_RESOLVE "codeAction/resolve"
#define LSP_METHOD_APPLY_EDIT          "workspace/applyEdit"
/* ---- LSP 3.17 新增方法 ---- */
#define LSP_METHOD_INLINE_VALUE        "textDocument/inlineValue"
/* ---- 刷新通知（Server -> Client 请求） @since 3.17 ---- */
#define LSP_METHOD_CODE_LENS_REFRESH   "workspace/codeLens/refresh"
#define LSP_METHOD_INLAY_HINT_REFRESH  "workspace/inlayHint/refresh"
#define LSP_METHOD_INLINE_VALUE_REFRESH "workspace/inlineValue/refresh"
#define LSP_METHOD_SEMANTIC_TOKENS_REFRESH "workspace/semanticTokens/refresh"
#define LSP_METHOD_DIAGNOSTIC_REFRESH  "workspace/diagnostic/refresh"
/* ---- Telemetry / Trace ---- */
#define LSP_METHOD_TELEMETRY_EVENT     "telemetry/event"

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

/* LSP DiagnosticTag */
enum {
    DIAG_TAG_UNNECESSARY = 1,
    DIAG_TAG_DEPRECATED  = 2,
};

/* LSP CodeActionKind */
#define CODE_ACTION_KIND_QUICKFIX             "quickfix"
#define CODE_ACTION_KIND_REFACTOR             "refactor"
#define CODE_ACTION_KIND_REFACTOR_EXTRACT     "refactor.extract"
#define CODE_ACTION_KIND_REFACTOR_INLINE      "refactor.inline"
#define CODE_ACTION_KIND_REFACTOR_REWRITE     "refactor.rewrite"
#define CODE_ACTION_KIND_SOURCE               "source"
#define CODE_ACTION_KIND_SOURCE_ORGANIZE_IMPORTS "source.organizeImports"
#define CODE_ACTION_KIND_SOURCE_FIX_ALL       "source.fixAll"

enum {
    CODE_ACTION_KIND_ID_QUICKFIX = 1,
    CODE_ACTION_KIND_ID_REFACTOR,
    CODE_ACTION_KIND_ID_REFACTOR_EXTRACT,
    CODE_ACTION_KIND_ID_REFACTOR_INLINE,
    CODE_ACTION_KIND_ID_REFACTOR_REWRITE,
    CODE_ACTION_KIND_ID_SOURCE,
    CODE_ACTION_KIND_ID_SOURCE_ORGANIZE_IMPORTS,
    CODE_ACTION_KIND_ID_SOURCE_FIX_ALL,
};

/* LSP FoldingRangeKind */
#define FOLDING_RANGE_KIND_COMMENT  "comment"
#define FOLDING_RANGE_KIND_IMPORTS  "imports"
#define FOLDING_RANGE_KIND_REGION   "region"

enum {
    FOLDING_RANGE_KIND_ID_COMMENT = 1,
    FOLDING_RANGE_KIND_ID_IMPORTS,
    FOLDING_RANGE_KIND_ID_REGION,
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
    JRPC_UNKNOWN_ERROR   = -32001,
    JRPC_REQUEST_FAILED  = -32803,
    JRPC_SERVER_CANCELLED= -32802,
    JRPC_CONTENT_MODIFIED = -32801,
    JRPC_REQUEST_CANCELLED= -32800,
};

/* LSP ErrorCodes named macros */
#define LSP_ERR_ParseError           -32700
#define LSP_ERR_InvalidRequest       -32600
#define LSP_ERR_MethodNotFound       -32601
#define LSP_ERR_InvalidParams        -32602
#define LSP_ERR_InternalError        -32603
#define LSP_ERR_ServerNotInitialized -32002
#define LSP_ERR_UnknownErrorCode     -32001
#define LSP_ERR_RequestFailed        -32803
#define LSP_ERR_ServerCancelled      -32802
#define LSP_ERR_ContentModified      -32801
#define LSP_ERR_RequestCancelled     -32800

/*
** ---- LXCLUA Lexer Tokens (matches compiler/llex.h) ----
*/

/** @brief LXCLUA token types (subset of compiler tokens) */
typedef enum {
    /* Single-char tokens map to their ASCII values (< 256) */
    /* Multi-char tokens start at 257 */
    TOK_AND         = 257, TOK_ASM, TOK_ASYNC, TOK_AWAIT, TOK_BOOL,
    TOK_BREAK, TOK_CASE, TOK_CLASS, TOK_CATCH, TOK_CHAR, TOK_COMMAND,
    TOK_CONCEPT, TOK_CONST, TOK_CONTINUE, TOK_DEFAULT,
    TOK_DEFER, TOK_DELETE, TOK_DO, TOK_DOUBLE, TOK_ELSE, TOK_ELSEIF,
    TOK_END, TOK_ENUM, TOK_EXPORT, TOK_FALSE, TOK_FINALLY,
    TOK_TYPE_FLOAT, TOK_FOR, TOK_FUNCTION,
    TOK_GLOBAL, TOK_GUARD, TOK_GOTO, TOK_IF, TOK_IN, TOK_TYPE_INT,
    TOK_IS, TOK_INSTANCEOF, TOK_KEYWORD, TOK_LAMBDA,
    TOK_LOCAL, TOK_LONG, TOK_NAMESPACE, TOK_NIL, TOK_NOT,
    TOK_INTERFACE, TOK_NEW, TOK_OPERATOR, TOK_OR,
    TOK_EXTENDS, TOK_IMPLEMENTS, TOK_SUPER,
    TOK_REPEAT, TOK_REQUIRES,
    TOK_RETURN, TOK_STRUCT, TOK_SUPERSTRUCT, TOK_SWITCH,
    TOK_TAKE, TOK_THEN, TOK_TRUE, TOK_TRY, TOK_UNTIL,
    TOK_USING, TOK_VOID, TOK_WHEN, TOK_WHILE, TOK_WITH, TOK_LET, TOK_MATCH,
    TOK_PRIVATE, TOK_PROTECTED, TOK_PUBLIC, TOK_STATIC,
    TOK_ABSTRACT, TOK_FINAL, TOK_SEALED, TOK_ARRAY,
    TOK_GET, TOK_SET, TOK_TRAIT, TOK_USE,

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
    AST_STMT_MATCH,       /**< match 语句 */

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
    int exit_code;
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
        int inline_value;  /**< @since 3.17 textDocument/inlineValue */
        /* LSP 3.17 resolve support */
        int code_action_resolve;
        int completion_resolve;
        int code_lens_resolve;
        int document_link_resolve;
        int inlay_hint_resolve;
        /* Workspace nested capabilities */
        struct {
            int workspace_folders;
            int did_change_configuration;
            int did_change_watched_files;
            int file_operations;
            struct {
                int enabled;
                char **commands;
                int ncommands;
            } execute_command;
        } workspace;
        /* Window nested capabilities */
        struct {
            int work_done_progress;
            int show_message;
            int show_message_request;
            int show_document;
        } window;
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
        struct {
            int apply_edit;
        } workspace;
    } client_caps;
    /* Pending request tracking */
    int next_request_id;
    /* Semantic tokens delta state */
    int prev_semantic_version;   /**< 上一次语义标记请求时的文档版本 */
    char *prev_semantic_result_id; /**< 上一次语义标记请求的 resultId */
    /* ---- LSP 3.17 extended state (appended for ABI safety) ---- */
    /* Cancelled request tracking */
    #define LSP_MAX_CANCEL_IDS 64
    int cancel_count;
    int cancel_ids[LSP_MAX_CANCEL_IDS];
    int cancel_id_max;
    /* Progress tracking */
    #define LSP_MAX_PROGRESS_IDS 64
    int progress_count;
    int progress_ids[LSP_MAX_PROGRESS_IDS];
    char *progress_values[LSP_MAX_PROGRESS_IDS];
    /* Current semantic result id string */
    char *semantic_result_id;
    /* Raw client capabilities JSON for later inspection */
    char *client_capabilities_json;
    /* Workspace folders array */
    #define LSP_MAX_WORKSPACE_FOLDERS 64
    struct {
        char *uri;
        char *name;
    } workspaceFolders[LSP_MAX_WORKSPACE_FOLDERS];
    int n_workspace_folders;
    /* Diagnostic (Pull) resultId cache per document */
    #define LSP_MAX_DIAG_RESULT_IDS 64
    int n_diag_result_ids;
    char *diag_result_uris[LSP_MAX_DIAG_RESULT_IDS];
    char *diag_result_ids[LSP_MAX_DIAG_RESULT_IDS];
    /* Semantic tokens: last resultId (for delta) and sequence counter */
    char *last_semantic_result_id;
    int semantic_token_seq;
    /* ---- Outbound notification queue (for publishDiagnostics, $/progress,
     *      window/logMessage, window/showMessage, workspace/applyEdit, ...).
     *      Main-loop (lspsrv_main.c) drains after each lsp_handle_message()
     *      by inspecting srv->pending_notifications[] / n_pending_notifications
     *      and sending each serialized frame via write_lsp_message().   ---- */
    #define LSP_MAX_PENDING_NOTIFICATIONS 32
    char *pending_notifications[LSP_MAX_PENDING_NOTIFICATIONS];
    int n_pending_notifications;
    /* Trace level 控制 ('off' | 'messages' | 'verbose') @since 3.17 */
    #define LSP_TRACE_OFF      0
    #define LSP_TRACE_MESSAGES 1
    #define LSP_TRACE_VERBOSE  2
    int trace_level;
    /* 下一 server->client 请求 id（用于 workspace/xxx/refresh 等） @since 3.17 */
    int next_server_request_id;
    /* ContentModified(-32801) 检测：请求处理前记录 uri/version，didChange 时 bump
     * 文档版本，处理完毕比较，若版本已被改动则返回 ContentModified。
     * 目前仅记录最近 1 个请求（足够覆盖大多数同步模型：每请求串行处理）。 */
    char *cm_uri;      /**< 当前处理中请求的文档 uri（若涉及文档） */
    int cm_version;    /**< 该请求处理起始时的文档版本（-1 表示无文档） */
} LspServer;

/* ---- 入站消息后服务器需要主循环额外推送的通知：
 *      lspsrv_main.c 调用 lsp_drain_pending_notifications() 并把返回的每条
 *      JSON-RPC 帧写到 stdout。调用者负责对返回的每个字符串调用 lsp_free()。
 *      返回值为实际弹出的通知数量；最多 pop_max 条。 */
int lsp_drain_pending_notifications(LspServer *srv, char ***out_notifs, int pop_max);

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
int lsp_is_cancelled(LspServer *srv, JsonValue *id);
/* ---- @since 3.17 辅助函数 ---- */
/**
 * @brief 触发一次 workspace/xxx/refresh（server 发起请求到 client，请求其重新拉取对应数据）。
 *        若 client 声明了支持（需客户端 capabilities 支持），把请求帧加入 pending 队列。
 * @param srv   LSP 服务器
 * @param method 刷新方法，如 LSP_METHOD_CODE_LENS_REFRESH 等
 * @return 0 已入队，-1 失败（队列满/参数错）
 */
int lsp_request_refresh(LspServer *srv, const char *method);
/**
 * @brief 发送 telemetry/event 通知（server->client 任意遥测数据）。
 * @param srv    LSP 服务器
 * @param data   已经 stringify 的 JSON LSPAny 数据（调用者保证正确 JSON）
 */
int lsp_send_telemetry(LspServer *srv, const char *data_json);

/**
 * @brief 查询当前 trace_level（供主循环控制日志量；>=LSP_TRACE_MESSAGES 才输出普通调试日志）。
 * @since 3.17
 */
static inline int lsp_get_trace_level(LspServer *srv) { return srv ? srv->trace_level : LSP_TRACE_OFF; }

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
JsonValue *lsp_build_diagnostics_arr(LspDiagnostic *diags, int ndiags);
void lsp_publish_diagnostics(void *server, const char *uri);
void lsp_send_log(void *server, int type, const char *fmt, ...);
void lsp_show_message(void *server, int type, const char *message);

/* ---- Window notifications (construct JSON only, no network send) ---- */
char *lsp_send_log_message(LspServer *srv, int type, const char *msg);
char *lsp_send_show_message(LspServer *srv, int type, const char *msg);

/**
 * @brief Server -> Client: 发起 window/workDoneProgress/create 请求（带 id）
 *        入队到 pending_notifications，由主循环 drain 时发送。
 *        服务端使用 progress_count/progress_ids 表自增 token；
 *        创建成功后调用者可使用 lsp_progress_report() 用同一 token 发送 $/progress。
 * @param srv          LSP 服务器
 * @param out_token    可选输出：返回分配的字符串 token（`lsp_free` 释放），无需可传 NULL
 * @param title        可选标题，NULL 时用空串
 * @param cancellable  是否可取消（客户端显示取消按钮）
 * @param message      可选初始进度消息，NULL 时不显示
 * @param percentage   可选 0-100 初始百分比，<0 时不显示
 * @return 0 入队成功，-1 参数错误/队列满
 * @since 3.15 (LSP 3.17 保留，兼容 Work Done Progress 流程)
 */
int lsp_work_done_progress_create(LspServer *srv, char **out_token, const char *title,
                                  int cancellable, const char *message, int percentage);

/**
 * @brief 向已创建的 workDoneProgress token 发送 $/progress report 通知（begin/report/end 三种）。
 * @param srv   LSP 服务器
 * @param token 与 lsp_work_done_progress_create 返回一致的字符串 token
 * @param kind  "begin"|"report"|"end"
 * @param message  可选消息（report/end 时显示）
 * @param percentage 可选百分比（report 时，<0 不填）
 * @return 0 入队成功，-1 失败
 */
int lsp_progress_report(LspServer *srv, const char *token, const char *kind,
                        const char *message, int percentage);

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