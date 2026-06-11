/*
** LXCLUA LSP - Document Management & Symbol Table
** Manages open documents, parsing, and symbol extraction.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "lspsrv.h"

/* Forward declarations */
void lsp_lex(const char *src, int len, LspToken **out_tokens, int *out_ntokens);
void lsp_lex_free(LspToken *tokens, int ntokens);

/* ---- Utility functions from lspsrv_util.c ---- */
extern char *lsp_strdup(const char *s);
extern void *lsp_alloc(size_t sz);
extern void *lsp_realloc(void *p, size_t sz);
extern void lsp_free(void *p);
extern char *lsp_str_append(char *dst, const char *src);
extern int lsp_offset_to_linecol(const char *text, int offset, int *line, int *col);
extern int lsp_linecol_to_offset(const char *text, int line, int col);
extern void lsp_build_line_offsets(const char *text, int len, int **out_offsets, int *out_nlines);

/*
 * @brief 创建新的文档条目
 * @param uri 文档URI
 * @param text 文档文本内容
 * @param version 文档版本号
 * @return 新文档指针
 */
static LspDocument *lsp_doc_new(const char *uri, const char *text, int version) {
    LspDocument *doc = (LspDocument *)lsp_alloc(sizeof(LspDocument));
    doc->uri = lsp_strdup(uri);
    doc->text = lsp_strdup(text ? text : "");
    doc->text_len = text ? strlen(text) : 0;
    doc->version = version;
    doc->open = 1;
    lsp_build_line_offsets(doc->text, (int)doc->text_len, &doc->line_offsets, &doc->nlines);
    doc->tokens = NULL;
    doc->ntokens = 0;
    doc->ast = NULL;
    doc->symbols = NULL;
    doc->nsymbols = 0;
    doc->vars = NULL;
    doc->nvars = 0;
    doc->var_cap = 0;
    doc->diagnostics = NULL;
    doc->ndiags = 0;
    doc->diag_cap = 0;
    doc->defined_globals = NULL;
    doc->ndefined_globals = 0;
    doc->imports = NULL;
    doc->nimports = 0;
    return doc;
}

/*
 * @brief 释放文档占用资源
 * @param doc 文档指针
 */
static void lsp_doc_free(LspDocument *doc) {
    if (!doc) return;
    lsp_free(doc->uri);
    lsp_free(doc->text);
    lsp_free(doc->line_offsets);
    if (doc->tokens) lsp_lex_free(doc->tokens, doc->ntokens);
    /* Free symbols */
    for (int i = 0; i < doc->nsymbols; i++) {
        LspSymbol *sym = doc->symbols[i];
        lsp_free(sym->name);
        lsp_free(sym->detail);
        lsp_free(sym->documentation);
        if (sym->children) {
            for (int j = 0; j < sym->nchildren; j++) {
                lsp_free(sym->children[j]->name);
                lsp_free(sym->children[j]->detail);
                lsp_free(sym->children[j]->documentation);
                lsp_free(sym->children[j]);
            }
            lsp_free(sym->children);
        }
        lsp_free(sym);
    }
    lsp_free(doc->symbols);
    /* Free vars */
    if (doc->vars) {
        for (int i = 0; i < doc->nvars; i++) {
            lsp_free(doc->vars[i].name);
            lsp_free(doc->vars[i].type_hint);
        }
        lsp_free(doc->vars);
    }
    /* Free diagnostics */
    if (doc->diagnostics) {
        for (int i = 0; i < doc->ndiags; i++) {
            lsp_free(doc->diagnostics[i].message);
            lsp_free(doc->diagnostics[i].source);
        }
        lsp_free(doc->diagnostics);
    }
    /* Free globals/imports */
    for (int i = 0; i < doc->ndefined_globals; i++) lsp_free(doc->defined_globals[i]);
    lsp_free(doc->defined_globals);
    for (int i = 0; i < doc->nimports; i++) lsp_free(doc->imports[i]);
    lsp_free(doc->imports);
    /* Free AST recursively */
    /* (AST free logic would go here - simplified for now) */
    lsp_free(doc);
}

/*
 * @brief 打开文档
 * @param server LSP服务器
 * @param uri 文档URI
 * @param text 文档内容
 * @param version 版本号
 * @return 0成功，-1失败（达到最大文档数）
 */
int lsp_doc_open(void *server, const char *uri, const char *text, int version) {
    LspServer *srv = (LspServer *)server;
    if (srv->ndocs >= MAX_DOCUMENTS) return -1;
    
    /* Check if already open */
    for (int i = 0; i < srv->ndocs; i++) {
        if (strcmp(srv->docs[i]->uri, uri) == 0) {
            /* Re-open with new content */
            lsp_doc_free(srv->docs[i]);
            srv->docs[i] = lsp_doc_new(uri, text, version);
            lsp_doc_parse(srv->docs[i], 1);
            return 0;
        }
    }
    
    srv->docs[srv->ndocs++] = lsp_doc_new(uri, text, version);
    lsp_doc_parse(srv->docs[srv->ndocs - 1], 1);
    return 0;
}

/*
 * @brief 文档内容变更
 * @param server LSP服务器
 * @param uri 文档URI
 * @param text 新的完整文本内容
 * @param version 新版本号
 * @return 0成功，-1失败
 */
int lsp_doc_change(void *server, const char *uri, const char *text, int version) {
    LspServer *srv = (LspServer *)server;
    LspDocument *doc = lsp_doc_find(srv, uri);
    if (!doc) return -1;
    
    /* Update text */
    lsp_free(doc->text);
    doc->text = lsp_strdup(text ? text : "");
    doc->text_len = text ? strlen(text) : 0;
    doc->version = version;
    
    /* Rebuild line offsets */
    lsp_free(doc->line_offsets);
    lsp_build_line_offsets(doc->text, (int)doc->text_len, &doc->line_offsets, &doc->nlines);
    
    /* Clear old parse data */
    if (doc->tokens) { lsp_lex_free(doc->tokens, doc->ntokens); doc->tokens = NULL; doc->ntokens = 0; }
    if (doc->diagnostics) {
        for (int i = 0; i < doc->ndiags; i++) {
            lsp_free(doc->diagnostics[i].message);
            lsp_free(doc->diagnostics[i].source);
        }
        lsp_free(doc->diagnostics);
        doc->diagnostics = NULL; doc->ndiags = 0; doc->diag_cap = 0;
    }
    
    /* Re-parse */
    lsp_doc_parse(doc, 1);
    return 0;
}

/*
 * @brief 关闭文档
 * @param server LSP服务器
 * @param uri 文档URI
 * @return 0成功，-1未找到
 */
int lsp_doc_close(void *server, const char *uri) {
    LspServer *srv = (LspServer *)server;
    for (int i = 0; i < srv->ndocs; i++) {
        if (strcmp(srv->docs[i]->uri, uri) == 0) {
            lsp_doc_free(srv->docs[i]);
            /* Move remaining docs down */
            for (int j = i; j < srv->ndocs - 1; j++)
                srv->docs[j] = srv->docs[j + 1];
            srv->ndocs--;
            return 0;
        }
    }
    return -1;
}

/*
 * @brief 根据URI查找文档
 * @param server LSP服务器
 * @param uri 文档URI
 * @return 文档指针，NULL表示未找到
 */
LspDocument *lsp_doc_find(void *server, const char *uri) {
    LspServer *srv = (LspServer *)server;
    for (int i = 0; i < srv->ndocs; i++) {
        if (strcmp(srv->docs[i]->uri, uri) == 0)
            return srv->docs[i];
    }
    return NULL;
}

/*
 * @brief 添加变量到文档的变量表
 * @param doc 文档
 * @param name 变量名
 * @param line 定义行号
 * @param col 定义列号
 * @param kind LSP SymbolKind
 * @param type_hint 类型提示
 */
static void lsp_doc_add_var(LspDocument *doc, const char *name, int line, int col, int kind, const char *type_hint) {
    if (!doc || !name) return;
    if (doc->nvars >= doc->var_cap) {
        doc->var_cap = doc->var_cap ? doc->var_cap * 2 : 64;
        doc->vars = lsp_realloc(doc->vars, doc->var_cap * sizeof(LspVarInfo));
    }
    LspVarInfo *v = &doc->vars[doc->nvars++];
    v->name = lsp_strdup(name);
    v->def_line = line;
    v->def_col = col;
    v->kind = kind;
    v->type_hint = type_hint ? lsp_strdup(type_hint) : NULL;
}

/*
 * @brief 向文档添加诊断信息
 * @param doc 文档
 * @param severity 严重级别
 * @param line_start 起始行
 * @param col_start 起始列
 * @param line_end 结束行
 * @param col_end 结束列
 * @param message 诊断消息
 * @param source 来源
 */
void lsp_doc_add_diag(LspDocument *doc, int severity, int line_start, int col_start,
                      int line_end, int col_end, const char *message, const char *source) {
    if (!doc || !message) return;
    if (doc->ndiags >= doc->diag_cap) {
        doc->diag_cap = doc->diag_cap ? doc->diag_cap * 2 : 16;
        doc->diagnostics = lsp_realloc(doc->diagnostics, doc->diag_cap * sizeof(LspDiagnostic));
    }
    LspDiagnostic *d = &doc->diagnostics[doc->ndiags++];
    d->severity = severity;
    d->line_start = line_start;
    d->col_start = col_start;
    d->line_end = line_end;
    d->col_end = col_end;
    d->message = lsp_strdup(message);
    d->source = source ? lsp_strdup(source) : NULL;
}

/*
 * @brief 解析文档 - 使用词法分析器提取符号表和结构信息
 * @param doc 文档指针
 * @param for_diagnostics 是否同时进行诊断分析
 */
void lsp_doc_parse(LspDocument *doc, int for_diagnostics) {
    if (!doc || !doc->text) return;
    
    /* 词法分析 */
    if (doc->tokens) { lsp_lex_free(doc->tokens, doc->ntokens); doc->tokens = NULL; doc->ntokens = 0; }
    lsp_lex(doc->text, (int)doc->text_len, &doc->tokens, &doc->ntokens);
    
    /* 清理旧分析结果 */
    if (doc->vars) {
        for (int i = 0; i < doc->nvars; i++) {
            lsp_free(doc->vars[i].name);
            lsp_free(doc->vars[i].type_hint);
        }
        doc->nvars = 0;
    }
    if (doc->diagnostics) {
        for (int i = 0; i < doc->ndiags; i++) {
            lsp_free(doc->diagnostics[i].message);
            lsp_free(doc->diagnostics[i].source);
        }
        lsp_free(doc->diagnostics);
        doc->diagnostics = NULL; doc->ndiags = 0; doc->diag_cap = 0;
    }
    
    /* 第一遍：提取基本符号 */
    LspToken *tokens = doc->tokens;
    int ntokens = doc->ntokens;
    
    for (int i = 0; i < ntokens; i++) {
        LspToken *tok = &tokens[i];
        
        /* 检测local声明 */
        if (tok->type == TOK_LOCAL) {
            if (i + 1 < ntokens && tokens[i+1].type == TOK_FUNCTION) {
                /* local function name */
                if (i + 2 < ntokens && tokens[i+2].type == TOK_NAME) {
                    LspToken *name = &tokens[i+2];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_FUNCTION, NULL);
                }
            } else if (i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
                /* local name [= expr] */
                int j = i + 1;
                while (j < ntokens && tokens[j].type == TOK_NAME) {
                    /* peek ahead for type hint: local x: int */
                    int has_type = 0;
                    if (j + 1 < ntokens && tokens[j+1].type == ':') {
                        has_type = 1;
                    }
                    LspToken *name = &tokens[j];
                    /* Check for function assignment */
                    int is_func = 0;
                    if (j + 2 < ntokens && tokens[j+1].type == (LspTokenType)'=' && tokens[j+2].type == TOK_FUNCTION) {
                        is_func = 1;
                    }
                    const char *type_str = NULL;
                    if (has_type && j + 2 < ntokens && tokens[j+2].type == TOK_NAME) {
                        type_str = tokens[j+2].text;
                    }
                    lsp_doc_add_var(doc, name->text, name->line, name->col,
                                   is_func ? SYMBOL_FUNCTION : SYMBOL_VARIABLE, type_str);
                    j++;
                    if (has_type) {
                        j += 2; /* skip : type */
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    } else {
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    }
                }
            }
        }
        /* 检测let声明 (LXCLUA: let x = expr 或 let x: type) */
        else if (tok->type == TOK_LET) {
            if (i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
                int j = i + 1;
                while (j < ntokens && tokens[j].type == TOK_NAME) {
                    const char *type_str = NULL;
                    int has_type = 0;
                    if (j + 1 < ntokens && tokens[j+1].type == ':') {
                        has_type = 1;
                        /* Extract type hint */
                        if (j + 2 < ntokens && tokens[j+2].type == TOK_NAME) {
                            type_str = tokens[j+2].text;
                        }
                    }
                    LspToken *name = &tokens[j];
                    int is_func = 0;
                    if (j + 2 < ntokens && tokens[j+1].type == (LspTokenType)'=' && tokens[j+2].type == TOK_FUNCTION) {
                        is_func = 1;
                    }
                    lsp_doc_add_var(doc, name->text, name->line, name->col,
                                   is_func ? SYMBOL_FUNCTION : SYMBOL_VARIABLE, type_str);
                    j++;
                    if (has_type) {
                        j += 2; /* skip : type */
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    } else {
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    }
                }
            }
        }
        /* 检测const声明 */
        else if (tok->type == TOK_CONST) {
            if (i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
                int j = i + 1;
                while (j < ntokens && tokens[j].type == TOK_NAME) {
                    const char *type_str = NULL;
                    int has_type = 0;
                    if (j + 1 < ntokens && tokens[j+1].type == ':') {
                        has_type = 1;
                        if (j + 2 < ntokens && tokens[j+2].type == TOK_NAME) {
                            type_str = tokens[j+2].text;
                        }
                    }
                    LspToken *name = &tokens[j];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_CONSTANT, type_str);
                    j++;
                    if (has_type) {
                        j += 2;
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    } else {
                        if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                        else break;
                    }
                }
            }
        }
        /* 检测export声明 */
        else if (tok->type == TOK_EXPORT) {
            if (i + 1 < ntokens) {
                if (tokens[i+1].type == TOK_FUNCTION && i + 2 < ntokens && tokens[i+2].type == TOK_NAME) {
                    LspToken *name = &tokens[i+2];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_FUNCTION, NULL);
                } else if (tokens[i+1].type == TOK_STRUCT && i + 2 < ntokens && tokens[i+2].type == TOK_NAME) {
                    LspToken *name = &tokens[i+2];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_STRUCT, NULL);
                } else if (tokens[i+1].type == TOK_ENUM && i + 2 < ntokens && tokens[i+2].type == TOK_NAME) {
                    LspToken *name = &tokens[i+2];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_ENUM, NULL);
                } else if (tokens[i+1].type == TOK_NAME) {
                    LspToken *name = &tokens[i+1];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_VARIABLE, NULL);
                }
            }
        }
        /* 检测global声明 */
        else if (tok->type == TOK_GLOBAL) {
            if (i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
                int j = i + 1;
                while (j < ntokens && tokens[j].type == TOK_NAME) {
                    LspToken *name = &tokens[j];
                    lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_VARIABLE, NULL);
                    /* Add to global list */
                    int found = 0;
                    for (int g = 0; g < doc->ndefined_globals; g++) {
                        if (strcmp(doc->defined_globals[g], name->text) == 0) { found = 1; break; }
                    }
                    if (!found) {
                        doc->ndefined_globals++;
                        doc->defined_globals = lsp_realloc(doc->defined_globals, doc->ndefined_globals * sizeof(char *));
                        doc->defined_globals[doc->ndefined_globals - 1] = lsp_strdup(name->text);
                    }
                    j++;
                    if (j < ntokens && tokens[j].type == (LspTokenType)',') j++;
                    else break;
                }
            }
        }
        /* 检测function声明（全局） */
        else if (tok->type == TOK_FUNCTION && i > 0) {
            /* function name(...) */
            if (tokens[i-1].type == TOK_NAME && (i < 2 || tokens[i-2].type != TOK_LOCAL)) {
                LspToken *name = &tokens[i-1];
                lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_FUNCTION, NULL);
            }
            /* 检测 if...end classmethod style assignment: ClassName:method = function(...) */
            if (i >= 3 && tokens[i-3].type == TOK_NAME && tokens[i-1].type == TOK_NAME) {
                int sep = i - 2;
                if (tokens[sep].type == (LspTokenType)':') {
                    /* ClassName:method = function */
                    char *full_name = lsp_fmt("%s:%s", tokens[i-3].text, tokens[i-1].text);
                    lsp_doc_add_var(doc, full_name, tokens[i-1].line, tokens[i-1].col, SYMBOL_METHOD, NULL);
                    lsp_free(full_name);
                } else if (tokens[sep].type == (LspTokenType)'.') {
                    /* table.field = function */
                    char *full_name = lsp_fmt("%s.%s", tokens[i-3].text, tokens[i-1].text);
                    lsp_doc_add_var(doc, full_name, tokens[i-1].line, tokens[i-1].col, SYMBOL_FUNCTION, NULL);
                    lsp_free(full_name);
                }
            }
        }
        /* 检测class声明 */
        else if (tok->type == TOK_STRUCT && i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
            LspToken *name = &tokens[i+1];
            lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_STRUCT, NULL);
        }
        /* 检测enum声明 */
        else if (tok->type == TOK_ENUM && i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
            LspToken *name = &tokens[i+1];
            lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_ENUM, NULL);
        }
        /* 检测namespace声明 */
        else if (tok->type == TOK_NAMESPACE && i + 1 < ntokens && tokens[i+1].type == TOK_NAME) {
            LspToken *name = &tokens[i+1];
            lsp_doc_add_var(doc, name->text, name->line, name->col, SYMBOL_NAMESPACE, NULL);
        }
        /* 检测requires声明（导入） */
        else if (tok->type == TOK_REQUIRES && i + 1 < ntokens && tokens[i+1].type == TOK_STRING) {
            doc->nimports++;
            doc->imports = lsp_realloc(doc->imports, doc->nimports * sizeof(char *));
            char *mod = tokens[i+1].text;
            int mlen = tokens[i+1].len;
            /* Strip quotes */
            if (mlen > 2 && (mod[0] == '"' || mod[0] == '\'')) {
                char *clean = (char *)lsp_alloc(mlen - 1);
                memcpy(clean, mod + 1, mlen - 2);
                clean[mlen - 2] = 0;
                doc->imports[doc->nimports - 1] = clean;
            } else {
                doc->imports[doc->nimports - 1] = lsp_strdup(mod);
            }
        }
        /* 检测require("...") 调用 */
        else if (tok->type == TOK_NAME && strcmp(tok->text, "require") == 0) {
            if (i + 2 < ntokens && tokens[i+1].type == (LspTokenType)'(' && tokens[i+2].type == TOK_STRING) {
                doc->nimports++;
                doc->imports = lsp_realloc(doc->imports, doc->nimports * sizeof(char *));
                char *mod = tokens[i+2].text;
                int mlen = tokens[i+2].len;
                if (mlen > 2 && (mod[0] == '"' || mod[0] == '\'')) {
                    char *clean = (char *)lsp_alloc(mlen - 1);
                    memcpy(clean, mod + 1, mlen - 2);
                    clean[mlen - 2] = 0;
                    doc->imports[doc->nimports - 1] = clean;
                } else {
                    doc->imports[doc->nimports - 1] = lsp_strdup(mod);
                }
            }
        }
    }
    
    /* ============ 基础诊断分析 ============ */
    if (for_diagnostics) {
        /* 检查未闭合的块 */
        int block_depth = 0;
        for (int i = 0; i < ntokens; i++) {
            LspToken *tok = &tokens[i];
            if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR || 
                tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                block_depth++;
            } else if (tok->type == TOK_END) {
                block_depth--;
                if (block_depth < 0) {
                    /* Unexpected end */
                    if (doc->ndiags >= doc->diag_cap) {
                        doc->diag_cap = doc->diag_cap ? doc->diag_cap * 2 : 32;
                        doc->diagnostics = lsp_realloc(doc->diagnostics, doc->diag_cap * sizeof(LspDiagnostic));
                    }
                    LspDiagnostic *d = &doc->diagnostics[doc->ndiags++];
                    d->severity = SEVERITY_ERROR;
                    d->line_start = tok->line; d->col_start = tok->col;
                    d->line_end = tok->line; d->col_end = tok->col + tok->len;
                    d->message = lsp_strdup("Unexpected 'end' (mismatched block)");
                    d->source = lsp_strdup("lxclua-lsp");
                    block_depth = 0;
                }
            }
        }
        if (block_depth > 0) {
            if (doc->ndiags >= doc->diag_cap) {
                doc->diag_cap = doc->diag_cap ? doc->diag_cap * 2 : 32;
                doc->diagnostics = lsp_realloc(doc->diagnostics, doc->diag_cap * sizeof(LspDiagnostic));
            }
            LspDiagnostic *d = &doc->diagnostics[doc->ndiags++];
            d->severity = SEVERITY_ERROR;
            d->line_start = doc->nlines > 0 ? doc->nlines - 1 : 0;
            d->col_start = 0;
            d->line_end = d->line_start;
            d->col_end = 100;
            d->message = lsp_strdup("Unclosed block(s) detected");
            d->source = lsp_strdup("lxclua-lsp");
        }
        
        /* 检查未使用的变量 */
        for (int i = 0; i < doc->nvars; i++) {
            const char *vname = doc->vars[i].name;
            int used = 0;
            /* 扫描该作用域内是否有使用该变量 */
            for (int j = 0; j < ntokens; j++) {
                if (tokens[j].type == TOK_NAME && strcmp(tokens[j].text, vname) == 0) {
                    /* 如果token不在定义行，则视为使用 */
                    if (tokens[j].line != doc->vars[i].def_line || tokens[j].col != doc->vars[i].def_col) {
                        used = 1;
                        break;
                    }
                }
            }
            if (!used) {
                if (doc->ndiags >= doc->diag_cap) {
                    doc->diag_cap = doc->diag_cap ? doc->diag_cap * 2 : 32;
                    doc->diagnostics = lsp_realloc(doc->diagnostics, doc->diag_cap * sizeof(LspDiagnostic));
                }
                LspDiagnostic *d = &doc->diagnostics[doc->ndiags++];
                d->severity = SEVERITY_HINT;
                d->line_start = doc->vars[i].def_line; d->col_start = doc->vars[i].def_col;
                d->line_end = doc->vars[i].def_line; d->col_end = doc->vars[i].def_col + (int)strlen(vname);
                d->message = lsp_fmt("Variable '%s' is declared but never used", vname);
                d->source = lsp_strdup("lxclua-lsp");
            }
        }
    }
}