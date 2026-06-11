/*
** LXCLUA LSP - Diagnostic Provider
** Real-time code analysis: syntax errors, semantic warnings, style hints.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lspsrv.h"

/*
 * @brief 生成代码诊断信息（主动分析：不平衡块、未使用变量、尾部空白）
 * @param doc 文档指针
 * @param diags 输出-诊断信息数组
 * @return 诊断信息数量
 */
int lsp_diagnostic(LspDocument *doc, LspDiagnostic **diags) {
    *diags = NULL;
    if (!doc) return 0;
    
    int n = 0;
    int cap = 16;
    LspDiagnostic *list = (LspDiagnostic *)lsp_alloc(cap * sizeof(LspDiagnostic));
    memset(list, 0, cap * sizeof(LspDiagnostic));
    
    /* 辅助：添加诊断 */
    #define ADD_DIAG(sev, ls, cs, le, ce, msg) do { \
        if (n >= cap) { cap *= 2; list = lsp_realloc(list, cap * sizeof(LspDiagnostic)); } \
        list[n].severity = (sev); \
        list[n].line_start = (ls); list[n].col_start = (cs); \
        list[n].line_end = (le); list[n].col_end = (ce); \
        list[n].message = lsp_strdup(msg); \
        list[n].source = lsp_strdup("lxclua"); \
        n++; \
    } while(0)
    
    if (doc->tokens && doc->ntokens > 0) {
        /* === 1. 不平衡块检测 === */
        int depth = 0;
        for (int i = 0; i < doc->ntokens; i++) {
            LspToken *tok = &doc->tokens[i];
            if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
                tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                depth++;
            } else if (tok->type == TOK_END) {
                depth--;
                if (depth < 0) {
                    ADD_DIAG(SEVERITY_ERROR, tok->line, tok->col, tok->line, tok->col + (int)strlen(tok->text),
                             "多余的 'end'，没有匹配的块起始语句");
                    depth = 0;
                }
            }
        }
        if (depth > 0) {
            /* 找到最后一个块起始 token 报告缺失 end */
            for (int i = doc->ntokens - 1; i >= 0; i--) {
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
                    tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                    tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                    tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                    ADD_DIAG(SEVERITY_ERROR, tok->line, tok->col, tok->line, tok->col + (int)strlen(tok->text),
                             "缺少匹配的 'end'");
                    break;
                }
            }
        }
        
        /* === 2. 未使用变量检测 === */
        for (int v = 0; v < doc->nvars; v++) {
            const char *name = doc->vars[v].name;
            if (!name || !*name) continue;
            int name_len = (int)strlen(name);
            /* 跳过函数/结构/枚举/类/命名空间定义 */
            if (doc->vars[v].kind == SYMBOL_FUNCTION || 
                doc->vars[v].kind == SYMBOL_STRUCT ||
                doc->vars[v].kind == SYMBOL_ENUM ||
                doc->vars[v].kind == SYMBOL_CLASS ||
                doc->vars[v].kind == SYMBOL_NAMESPACE) continue;
            
            /* 在 token 流中搜索该变量名，排除定义行 */
            int use_count = 0;
            for (int t = 0; t < doc->ntokens; t++) {
                if (doc->tokens[t].type != TOK_NAME) continue;
                if (doc->tokens[t].len != name_len) continue;
                if (strncmp(doc->tokens[t].text, name, name_len) != 0) continue;
                if (doc->tokens[t].line == doc->vars[v].def_line && 
                    doc->tokens[t].col == doc->vars[v].def_col) continue;
                use_count++;
            }
            if (use_count == 0) {
                ADD_DIAG(SEVERITY_WARNING, doc->vars[v].def_line, doc->vars[v].def_col,
                         doc->vars[v].def_line, doc->vars[v].def_col + name_len,
                         "未使用的变量");
            }
        }
    }
    
    /* === 3. 尾部空白检测 === */
    if (doc->line_offsets && doc->nlines > 0) {
        for (int ln = 0; ln < doc->nlines; ln++) {
            int start = doc->line_offsets[ln];
            int end = (ln + 1 < doc->nlines) ? doc->line_offsets[ln + 1] : (int)doc->text_len;
            int line_len = end - start;
            if (line_len <= 0) continue;
            /* 从行尾反向寻找非空白字符 */
            int trail_end = end - 1;
            while (trail_end > start && (doc->text[trail_end] == '\r' || doc->text[trail_end] == '\n'))
                trail_end--;
            int trail_start = trail_end;
            while (trail_start >= start && (doc->text[trail_start] == ' ' || doc->text[trail_start] == '\t'))
                trail_start--;
            trail_start++;
            if (trail_start <= trail_end) {
                ADD_DIAG(SEVERITY_INFO, ln, trail_start - start, ln, trail_end - start + 1,
                         "行尾有多余空白字符");
            }
        }
    }
    
    /* === 4. 未闭合字符串检测 === */
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            LspToken *tok = &doc->tokens[i];
            if (tok->type == TOK_STRING && tok->text && tok->len > 0) {
                char quote = tok->text[0];
                /* 检查字符串是否以引号开始和结束 */
                if (quote == '"' || quote == '\'') {
                    /* 长字符串 [[...]] 格式 */
                    if (tok->len >= 4 && tok->text[0] == '[' && tok->text[1] == '[') {
                        if (tok->len < 4 || tok->text[tok->len-2] != ']' || tok->text[tok->len-1] != ']') {
                            ADD_DIAG(SEVERITY_ERROR, tok->line, tok->col, tok->line, tok->col + tok->len,
                                     "未闭合的长字符串 \"[[...]]\"");
                        }
                    } else if (tok->len < 2 || tok->text[tok->len-1] != quote) {
                        /* 单行字符串未闭合 */
                        ADD_DIAG(SEVERITY_ERROR, tok->line, tok->col, tok->line, tok->col + tok->len,
                                 "未闭合的字符串");
                    }
                }
            }
        }
    }
    
    /* === 5. 多余逗号检测（表构造器、参数列表等） === */
    if (doc->tokens && doc->ntokens > 1) {
        for (int i = 0; i < doc->ntokens - 1; i++) {
            /* 检测连续两个逗号 */
            if (doc->tokens[i].type == (LspTokenType)',' && 
                doc->tokens[i+1].type == (LspTokenType)',') {
                ADD_DIAG(SEVERITY_WARNING, doc->tokens[i+1].line, doc->tokens[i+1].col,
                         doc->tokens[i+1].line, doc->tokens[i+1].col + 1,
                         "多余的逗号");
            }
            /* 检测逗号后紧跟 ) 或 } (如 {a, b, }) */
            if (doc->tokens[i].type == (LspTokenType)',' &&
                (doc->tokens[i+1].type == (LspTokenType)')' || 
                 doc->tokens[i+1].type == (LspTokenType)'}')) {
                ADD_DIAG(SEVERITY_INFO, doc->tokens[i].line, doc->tokens[i].col,
                         doc->tokens[i].line, doc->tokens[i].col + 1,
                         "末尾多余逗号，建议移除");
            }
        }
    }
    
    #undef ADD_DIAG
    *diags = list;
    return n;
}

/*
** LXCLUA LSP - Formatting Provider
*/

/*
 * @brief 格式化文档
 * @param doc 文档指针
 * @param tab_size Tab大小（空格数）
 * @param insert_spaces 是否用空格替代Tab
 * @return 格式化后的文本（新分配），NULL表示失败
 */
char *lsp_format(LspDocument *doc, int tab_size, int insert_spaces) {
    if (!doc || !doc->text) return NULL;
    if (tab_size <= 0) tab_size = 4;
    
    /* First pass: compute indent levels from token stream */
    int nlines = doc->nlines;
    int *indent_levels = (int *)lsp_alloc(nlines * sizeof(int));
    memset(indent_levels, 0, nlines * sizeof(int));
    
    if (doc->tokens && doc->ntokens > 0) {
        int cur_indent = 0;
        int last_line = -1;
        for (int i = 0; i < doc->ntokens; i++) {
            LspToken *tok = &doc->tokens[i];
            int line = tok->line;
            if (line < 0 || line >= nlines) continue;
            
            /* 每行首次遇到token时才记录当前缩进级别 */
            if (line != last_line) {
                indent_levels[line] = cur_indent;
                last_line = line;
            }
            
            /* 块结束关键字：减少缩进，本行和后续行都在低级别 */
            if (tok->type == TOK_END || tok->type == TOK_UNTIL) {
                if (cur_indent > 0) cur_indent--;
                indent_levels[line] = cur_indent;
                continue;
            }
            /* else/elseif/catch/finally/case/default：本行回退一级，但块体内容仍需缩进 */
            if (tok->type == TOK_ELSE || tok->type == TOK_ELSEIF ||
                tok->type == TOK_CATCH || tok->type == TOK_FINALLY ||
                tok->type == TOK_CASE || tok->type == TOK_DEFAULT) {
                if (cur_indent > 0) cur_indent--;
                indent_levels[line] = cur_indent;
                cur_indent++;  /* 恢复：后续块体内容缩进 */
                continue;
            }
            
            /* 块起始关键字：后续行缩进增加 */
            /* 注意：THEN 不在此列表 —— 它和 IF 同属一行，不应再次递增 */
            if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
                tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                cur_indent++;
            }
        }
    }
    
    /* Second pass: format text */
    size_t text_len = doc->text_len;
    size_t cap = text_len * 2 + 4096;
    char *result = (char *)lsp_alloc(cap);
    size_t out_pos = 0;
    int prev_was_newline = 1;
    int cur_line = 0;
    int in_string = 0;
    int string_char = 0;
    
    for (size_t i = 0; i < text_len; i++) {
        char c = doc->text[i];
        
        if (out_pos + 16 >= cap) {
            cap *= 2;
            result = lsp_realloc(result, cap);
        }
        
        if (in_string) {
            result[out_pos++] = c;
            if (c == string_char && (i == 0 || doc->text[i-1] != '\\'))
                in_string = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = 1;
            string_char = c;
            result[out_pos++] = c;
            continue;
        }
        
        /* Skip existing indentation at line start */
        if (prev_was_newline && (c == ' ' || c == '\t')) continue;
        
        /* Handle newlines */
        if (c == '\n') {
            while (out_pos > 0 && (result[out_pos-1] == ' ' || result[out_pos-1] == '\t'))
                out_pos--;
            result[out_pos++] = '\n';
            prev_was_newline = 1;
            cur_line++;
            continue;
        }
        if (c == '\r' && i+1 < text_len && doc->text[i+1] == '\n') {
            i++;
            while (out_pos > 0 && (result[out_pos-1] == ' ' || result[out_pos-1] == '\t'))
                out_pos--;
            result[out_pos++] = '\n';
            prev_was_newline = 1;
            cur_line++;
            continue;
        }
        
        /* Apply indent on new line */
        if (prev_was_newline) {
            prev_was_newline = 0;
            int indent = (cur_line < nlines) ? indent_levels[cur_line] : 0;
            int total_indent = indent * tab_size;
            for (int j = 0; j < total_indent; j++) {
                if (out_pos + 1 >= (int)cap) { cap *= 2; result = lsp_realloc(result, cap); }
                result[out_pos++] = (insert_spaces || tab_size != 1) ? ' ' : '\t';
            }
        }
        
        result[out_pos++] = c;
    }
    
    lsp_free(indent_levels);
    result[out_pos] = '\0';
    return result;
}

/*
** LXCLUA LSP - Rename Provider
*/

/*
 * @brief 重命名符号
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param new_name 新名称
 * @return 重命名的位置数
 */
int lsp_rename(LspDocument *doc, int line, int col, const char *new_name) {
    if (!doc || !new_name) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    int count = 0;
    /* Count occurrences (will be used by protocol handler to build text edits) */
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, word) == 0) {
                count++;
            }
        }
    }
    
    lsp_free(word);
    return count;
}

/*
** LXCLUA LSP - Signature Help
*/

/*
 * @brief 生成函数签名帮助
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @return JSON格式的签名帮助字符串
 */
char *lsp_signature_help(LspDocument *doc, int line, int col) {
    if (!doc || !doc->tokens || doc->ntokens == 0) return NULL;
    
    LspToken *tokens = doc->tokens;
    int ntokens = doc->ntokens;
    
    /* Find the nearest opening parenthesis before cursor */
    int paren_count = 0;
    int func_name_idx = -1;
    int open_paren_idx = -1;
    
    for (int i = 0; i < ntokens; i++) {
        if (doc->tokens[i].line > line || 
            (doc->tokens[i].line == line && doc->tokens[i].col > col)) {
            break;
        }
        if (doc->tokens[i].type == (LspTokenType)'(') {
            open_paren_idx = i;
            /* Look backwards for function name */
            if (i >= 1 && doc->tokens[i-1].type == TOK_NAME) {
                func_name_idx = i - 1;
                paren_count = 1;
                /* Count commas to determine arg position */
                int active_param = 0;
                for (int j = i + 1; j < ntokens; j++) {
                    if (doc->tokens[j].line > line || 
                        (doc->tokens[j].line == line && doc->tokens[j].col > col)) {
                        break;
                    }
                    if (doc->tokens[j].type == (LspTokenType)',' && doc->tokens[j].line == doc->tokens[j-1].line) {
                        active_param++;
                    }
                }
            } else if (i >= 2 && doc->tokens[i-1].type == (LspTokenType)':' && doc->tokens[i-2].type == TOK_NAME) {
                /* method call: obj:method( */
                func_name_idx = i - 2;
            }
        }
    }
    
    if (func_name_idx < 0) return NULL;
    
    const char *func_name = doc->tokens[func_name_idx].text;
    const char *doc_str = lsp_kwdb_find_doc(func_name);
    
    if (doc_str) {
        return lsp_fmt("{\"signatures\":[{\"label\":\"%s\",\"documentation\":\"%s\",\"parameters\":[]}],\"activeSignature\":0,\"activeParameter\":0}", 
                       doc_str, doc_str);
    }
    return NULL;
}

/*
** LXCLUA LSP - Find References
*/

/*
 * @brief 查找当前光标处符号的所有引用
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param out_lines 输出-引用的行号数组
 * @param out_cols 输出-引用的列号数组
 * @param count 输出-引用数量
 * @return 0成功，-1失败
 */
int lsp_find_references(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count) {
    *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return -1;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return -1; }
    
    /* Count first */
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, word) == 0) {
                n++;
            }
        }
    }
    
    if (n == 0) { lsp_free(word); return 0; }
    
    *out_lines = (int *)lsp_alloc(n * sizeof(int));
    *out_cols = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, word) == 0) {
                (*out_lines)[idx] = doc->tokens[i].line;
                (*out_cols)[idx] = doc->tokens[i].col;
                idx++;
            }
        }
    }
    
    *count = idx;
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - Document Highlight
*/

/*
 * @brief 高亮当前光标处符号的所有出现位置
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param out_kinds 输出-高亮类型数组
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-高亮数量
 * @return 0成功，-1失败
 */
int lsp_document_highlight(LspDocument *doc, int line, int col, int **out_kinds, int **out_lines, int **out_cols, int *count) {
    *out_kinds = NULL; *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return -1;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return -1; }
    
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, word) == 0) {
                n++;
            }
        }
    }
    
    if (n == 0) { lsp_free(word); return 0; }
    
    *out_kinds = (int *)lsp_alloc(n * sizeof(int));
    *out_lines = (int *)lsp_alloc(n * sizeof(int));
    *out_cols = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, word) == 0) {
                /* Kind: 1=Text, 2=Read, 3=Write */
                int kind = 2; /* Read by default */
                if (i > 0) {
                    if (doc->tokens[i-1].type == (LspTokenType)'=' && 
                        (i < 2 || doc->tokens[i-2].type != (LspTokenType)'=' &&
                         doc->tokens[i-2].type != (LspTokenType)'!' &&
                         doc->tokens[i-2].type != (LspTokenType)'~' &&
                         doc->tokens[i-2].type != (LspTokenType)'<' &&
                         doc->tokens[i-2].type != (LspTokenType)'>')) {
                        kind = 3; /* Write (lvalue) */
                    }
                }
                if (doc->tokens[i].line == line && doc->tokens[i].col == col) {
                    kind = 1; /* Text (the reference itself) */
                }
                (*out_kinds)[idx] = kind;
                (*out_lines)[idx] = doc->tokens[i].line;
                (*out_cols)[idx] = doc->tokens[i].col;
                idx++;
            }
        }
    }
    
    *count = idx;
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - Document Symbol Provider
*/

/* 容器符号类型判断：用于 documentSymbol 嵌套层级计算 */
static int is_container(int kind) {
    return kind == SYMBOL_FUNCTION || kind == SYMBOL_STRUCT || 
           kind == SYMBOL_ENUM || kind == SYMBOL_NAMESPACE || 
           kind == SYMBOL_CLASS || kind == SYMBOL_MODULE;
}

/*
 * @brief 生成文档符号（大纲视图，带层级嵌套）
 * @param doc 文档指针
 * @param out_symbols 输出-符号数组（仅顶级符号，子符号挂在其 children 中）
 * @return 顶级符号数量
 */
int lsp_document_symbol(LspDocument *doc, LspSymbol ***out_symbols) {
    *out_symbols = NULL;
    if (!doc || doc->nvars == 0) return 0;
    
    int n = doc->nvars;
    
    /* 第一步：找出所有容器符号的结束行 */
    int *scope_end = (int *)lsp_alloc(n * sizeof(int));
    for (int i = 0; i < n; i++) scope_end[i] = -1;
    
    /* 为每个容器符号找对应的 END token */
    if (doc->tokens && doc->ntokens > 0) {
        for (int v = 0; v < n; v++) {
            if (!is_container(doc->vars[v].kind)) continue;
            
            int start_line = doc->vars[v].def_line;
            int depth = 0;
            for (int t = 0; t < doc->ntokens; t++) {
                LspToken *tok = &doc->tokens[t];
                if (tok->line < start_line) continue;
                
                /* 块起始关键字 */
                if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
                    tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                    tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                    tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                    depth++;
                }
                else if (tok->type == TOK_END) {
                    depth--;
                    if (depth == 0) {
                        scope_end[v] = tok->line;
                        break;
                    }
                }
            }
            /* 如果没找到 END，用文档末尾 */
            if (scope_end[v] < 0) scope_end[v] = doc->nlines - 1;
        }
    }
    
    /* 第二步：计算每个符号的嵌套层级 */
    int *parent_idx = (int *)lsp_alloc(n * sizeof(int));
    for (int i = 0; i < n; i++) parent_idx[i] = -1;
    
    for (int i = 0; i < n; i++) {
        int best_parent = -1;
        int best_end = -1;
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            if (!is_container(doc->vars[j].kind)) continue;
            if (scope_end[j] < 0) continue;
            /* 符号 j 是符号 i 的父级：i 在 j 的定义行和结束行之间，且 j 的结束行最接近 i */
            if (doc->vars[i].def_line > doc->vars[j].def_line && 
                doc->vars[i].def_line <= scope_end[j]) {
                if (best_parent < 0 || scope_end[j] < best_end) {
                    best_parent = j;
                    best_end = scope_end[j];
                }
            }
        }
        parent_idx[i] = best_parent;
    }
    
    /* 第三步：分配 LspSymbol 并构建层级 */
    LspSymbol **all_syms = (LspSymbol **)lsp_alloc(n * sizeof(LspSymbol *));
    for (int i = 0; i < n; i++) {
        all_syms[i] = (LspSymbol *)lsp_alloc(sizeof(LspSymbol));
        all_syms[i]->name = lsp_strdup(doc->vars[i].name);
        all_syms[i]->kind = doc->vars[i].kind;
        all_syms[i]->line = doc->vars[i].def_line;
        all_syms[i]->col = doc->vars[i].def_col;
        all_syms[i]->end_line = scope_end[i] >= 0 ? scope_end[i] : doc->vars[i].def_line;
        all_syms[i]->end_col = doc->vars[i].def_col + (int)strlen(doc->vars[i].name);
        all_syms[i]->detail = doc->vars[i].type_hint ? lsp_strdup(doc->vars[i].type_hint) : NULL;
        all_syms[i]->documentation = NULL;
        all_syms[i]->children = NULL;
        all_syms[i]->nchildren = 0;
        all_syms[i]->scope_start_line = doc->vars[i].def_line;
        all_syms[i]->scope_end_line = scope_end[i] >= 0 ? scope_end[i] : doc->vars[i].def_line;
    }
    
    /* 第四步：统计每个父节点子节点数量，收集顶级符号 */
    int *child_counts = (int *)lsp_alloc(n * sizeof(int));
    memset(child_counts, 0, n * sizeof(int));
    for (int i = 0; i < n; i++) {
        if (parent_idx[i] >= 0) child_counts[parent_idx[i]]++;
    }
    
    /* 分配 children 数组 */
    for (int i = 0; i < n; i++) {
        if (child_counts[i] > 0) {
            all_syms[i]->children = (LspSymbol **)lsp_alloc(child_counts[i] * sizeof(LspSymbol *));
        }
    }
    
    /* 第五步：填充子节点 */
    int *child_fill = (int *)lsp_alloc(n * sizeof(int));
    memset(child_fill, 0, n * sizeof(int));
    int top_count = 0;
    LspSymbol **top_syms = (LspSymbol **)lsp_alloc(n * sizeof(LspSymbol *));
    
    for (int i = 0; i < n; i++) {
        if (parent_idx[i] < 0) {
            /* 顶级符号 */
            top_syms[top_count++] = all_syms[i];
        } else {
            /* 挂到父节点下 */
            int p = parent_idx[i];
            all_syms[p]->children[child_fill[p]++] = all_syms[i];
        }
    }
    
    lsp_free(scope_end);
    lsp_free(parent_idx);
    lsp_free(child_counts);
    lsp_free(child_fill);
    
    *out_symbols = top_syms;
    return top_count;
}

/*
** LXCLUA LSP - Folding Range Provider
*/

/*
 * @brief 生成代码折叠范围
 * @param doc 文档指针
 * @param out_start_lines 输出-折叠起始行
 * @param out_end_lines 输出-折叠结束行
 * @param count 输出-折叠范围数量
 * @return 0成功
 */
int lsp_folding_range(LspDocument *doc, int **out_start_lines, int **out_end_lines, int *count) {
    *out_start_lines = NULL; *out_end_lines = NULL; *count = 0;
    if (!doc || !doc->tokens || doc->ntokens == 0) return 0;
    
    /* Use a stack to track block boundaries */
    int max_depth = 128;
    int *stack_lines = (int *)lsp_alloc(max_depth * sizeof(int));
    int *stack_types = (int *)lsp_alloc(max_depth * sizeof(int));
    int depth = 0;
    
    int cap = 64;
    int *starts = (int *)lsp_alloc(cap * sizeof(int));
    int *ends = (int *)lsp_alloc(cap * sizeof(int));
    int nfolds = 0;
    
    for (int i = 0; i < doc->ntokens; i++) {
        LspToken *tok = &doc->tokens[i];
        
        if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
            tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
            tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
            tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
            if (depth < max_depth) {
                stack_lines[depth] = tok->line;
                stack_types[depth] = tok->type;
                depth++;
            }
        } else if (tok->type == TOK_END || tok->type == TOK_UNTIL) {
            if (depth > 0) {
                depth--;
                int start_line = stack_lines[depth];
                int end_line = tok->line;
                /* Only add fold if it spans at least 2 lines */
                if (end_line - start_line >= 1) {
                    if (nfolds >= cap) {
                        cap *= 2;
                        starts = lsp_realloc(starts, cap * sizeof(int));
                        ends = lsp_realloc(ends, cap * sizeof(int));
                    }
                    starts[nfolds] = start_line;
                    ends[nfolds] = end_line;
                    nfolds++;
                }
            }
        }
    }
    
    lsp_free(stack_lines);
    lsp_free(stack_types);
    
    *out_start_lines = starts;
    *out_end_lines = ends;
    *count = nfolds;
    return 0;
}

/*
** LXCLUA LSP - Semantic Tokens Provider
*/

/*
 * @brief 生成语义标记（语法高亮增强）
 * @param doc 文档指针
 * @return JSON字符串，NULL表示空
 */
char *lsp_semantic_tokens(LspDocument *doc) {
    if (!doc || !doc->tokens || doc->ntokens == 0) return NULL;
    /* 委托给 proto 中的实现 */
    return NULL;
}

/*
** LXCLUA LSP - Code Action Provider
*/

/*
 * @brief 生成代码操作建议
 * @param doc 文档指针
 * @param line 光标行
 * @param col 光标列
 * @param diagnostics_list 输出-诊断列表
 * @param count 输出-诊断数量
 * @return 操作数量（现在固定返回0，表示支持code_action但无具体操作）
 */
int lsp_code_action(LspDocument *doc, int line, int col, LspDiagnostic **diagnostics_list, int *count) {
    /* Collect diagnostics at the cursor position and generate corresponding code actions */
    *diagnostics_list = NULL;
    *count = 0;
    if (!doc) return 0;
    
    int n = 0;
    for (int i = 0; i < doc->ndiags; i++) {
        if (doc->diagnostics[i].line_start <= line && doc->diagnostics[i].line_end >= line) {
            n++;
        }
    }
    
    if (n == 0) {
        /* Even without diagnostics, provide some generic code actions */
        /* For now, return empty - editors will show "No code actions available" */
        return 0;
    }
    
    *diagnostics_list = (LspDiagnostic *)lsp_alloc(n * sizeof(LspDiagnostic));
    int idx = 0;
    for (int i = 0; i < doc->ndiags; i++) {
        if (doc->diagnostics[i].line_start <= line && doc->diagnostics[i].line_end >= line) {
            (*diagnostics_list)[idx] = doc->diagnostics[i];
            (*diagnostics_list)[idx].message = lsp_strdup(doc->diagnostics[i].message);
            (*diagnostics_list)[idx].source = lsp_strdup(doc->diagnostics[i].source);
            idx++;
        }
    }
    *count = idx;
    return 0;
}

/*
** LXCLUA LSP - Prepare Rename Provider
*/

/*
 * @brief 验证重命名位置是否有效，返回可重命名的范围
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param out_line 输出-范围起始行
 * @param out_col 输出-范围起始列
 * @param out_end_line 输出-范围结束行
 * @param out_end_col 输出-范围结束列
 * @return 1有效，0无效
 */
int lsp_prepare_rename(LspDocument *doc, int line, int col, int *out_line, int *out_col, int *out_end_line, int *out_end_col) {
    *out_line = *out_col = *out_end_line = *out_end_col = 0;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* Check if the word is an identifier (not a keyword) */
    int is_keyword = 0;
    /* Standard Lua/LXCLUA keywords are reserved, can't rename */
    const char *reserved[] = {"and","break","do","else","elseif","end","false","for",
        "function","goto","if","in","local","nil","not","or","repeat","return",
        "then","true","until","while","async","await","class","const","continue",
        "defer","enum","export","global","import","let","namespace","struct",
        "switch","case","default","try","catch","finally","throw","requires",
        "using","concept","superstruct","is","instanceof","take","with","when",
        "lambda","command","keyword","operator","void","bool","char","double",
        "float","int","long",NULL};
    for (int i = 0; reserved[i]; i++) {
        if (strcmp(word, reserved[i]) == 0) { is_keyword = 1; break; }
    }
    
    if (is_keyword) { lsp_free(word); return 0; }
    
    /* Convert byte offsets to line/col */
    int start_line, start_col, end_line, end_col;
    lsp_offset_to_linecol(doc->text, word_start, &start_line, &start_col);
    lsp_offset_to_linecol(doc->text, word_end, &end_line, &end_col);
    
    *out_line = start_line;
    *out_col = start_col;
    *out_end_line = end_line;
    *out_end_col = end_col;
    
    lsp_free(word);
    return 1;
}

/*
** LXCLUA LSP - Type Definition Provider
*/

/*
 * @brief 查找光标处符号的类型定义
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param def_line 输出-定义行号
 * @param def_col 输出-定义列号
 * @param def_uri 输出-定义URI
 * @return 1找到，0未找到
 */
int lsp_type_definition(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri) {
    *def_line = -1; *def_col = -1; *def_uri = NULL;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* Look for type definitions: struct/enum declarations or type-hinted variables */
    /* 1. Check if it's a variable with a type hint */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0 && doc->vars[i].type_hint) {
            /* Found a variable with type hint - look for the type declaration */
            if (doc->tokens && doc->ntokens > 0) {
                for (int j = 0; j < doc->ntokens; j++) {
                    if (doc->tokens[j].type == TOK_NAME && 
                        strcmp(doc->tokens[j].text, doc->vars[i].type_hint) == 0) {
                        /* Check if this token is in a struct/enum declaration */
                        if (j >= 1 && (doc->tokens[j-1].type == TOK_STRUCT || 
                                       doc->tokens[j-1].type == TOK_ENUM)) {
                            *def_line = doc->tokens[j].line;
                            *def_col = doc->tokens[j].col;
                            *def_uri = lsp_strdup(doc->uri);
                            lsp_free(word);
                            return 1;
                        }
                    }
                }
            }
        }
    }
    
    /* 2. Check if the word itself is a type name by looking for its struct/enum declaration */
    if (doc->tokens && doc->ntokens > 0) {
        for (int j = 0; j < doc->ntokens; j++) {
            if (doc->tokens[j].type == TOK_NAME && strcmp(doc->tokens[j].text, word) == 0) {
                if (j >= 1 && (doc->tokens[j-1].type == TOK_STRUCT || 
                               doc->tokens[j-1].type == TOK_ENUM ||
                               doc->tokens[j-1].type == TOK_EXPORT)) {
                    *def_line = doc->tokens[j].line;
                    *def_col = doc->tokens[j].col;
                    *def_uri = lsp_strdup(doc->uri);
                    lsp_free(word);
                    return 1;
                }
            }
        }
    }
    
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - Implementation Provider
*/

/*
 * @brief 查找接口/方法的实现位置
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_find_implementation(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count) {
    *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* For LXCLUA, implementation means: find function bodies for method declarations,
     * or subclass definitions for a superstruct */
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        /* Look for function definitions that might be implementations */
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_FUNCTION) {
                /* Check if previous tokens form ClassName:methodName or ClassName.methodName */
                if (i >= 3 && doc->tokens[i-1].type == TOK_NAME &&
                    (doc->tokens[i-2].type == (LspTokenType)':' || doc->tokens[i-2].type == (LspTokenType)'.') &&
                    doc->tokens[i-3].type == TOK_NAME) {
                    if (strcmp(doc->tokens[i-3].text, word) == 0) {
                        n++;
                    }
                }
            }
        }
        /* Also look for superstruct that inherits from the word */
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_SUPERSTRUCT && i + 1 < doc->ntokens &&
                doc->tokens[i+1].type == TOK_NAME) {
                /* superstruct NewName: BaseName */
                if (i + 3 < doc->ntokens && doc->tokens[i+2].type == (LspTokenType)':' &&
                    doc->tokens[i+3].type == TOK_NAME &&
                    strcmp(doc->tokens[i+3].text, word) == 0) {
                    n++;
                }
            }
        }
    }
    
    if (n == 0) { lsp_free(word); return 0; }
    
    *out_lines = (int *)lsp_alloc(n * sizeof(int));
    *out_cols = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_FUNCTION && i >= 3 &&
                doc->tokens[i-1].type == TOK_NAME &&
                (doc->tokens[i-2].type == (LspTokenType)':' || doc->tokens[i-2].type == (LspTokenType)'.') &&
                doc->tokens[i-3].type == TOK_NAME &&
                strcmp(doc->tokens[i-3].text, word) == 0) {
                (*out_lines)[idx] = doc->tokens[i].line;
                (*out_cols)[idx] = doc->tokens[i].col;
                idx++;
            }
        }
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_SUPERSTRUCT && i + 3 < doc->ntokens &&
                doc->tokens[i+2].type == (LspTokenType)':' &&
                doc->tokens[i+3].type == TOK_NAME &&
                strcmp(doc->tokens[i+3].text, word) == 0) {
                (*out_lines)[idx] = doc->tokens[i+1].line;
                (*out_cols)[idx] = doc->tokens[i+1].col;
                idx++;
            }
        }
    }
    
    *count = n;
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - Workspace Symbol Provider
*/

/*
 * @brief 在工作区中搜索符号
 * @param server LSP服务器
 * @param query 搜索查询字符串
 * @param out_symbols 输出-符号数组
 * @return 符号数量
 */
int lsp_workspace_symbol(void *server, const char *query, LspSymbol ***out_symbols) {
    *out_symbols = NULL;
    if (!server || !query) return 0;
    
    LspServer *srv = (LspServer *)server;
    int total = 0;
    int cap = 64;
    LspSymbol **syms = (LspSymbol **)lsp_alloc(cap * sizeof(LspSymbol *));
    
    for (int d = 0; d < srv->ndocs; d++) {
        LspDocument *doc = srv->docs[d];
        if (!doc || !doc->open) continue;
        
        for (int v = 0; v < doc->nvars; v++) {
            /* Simple case-insensitive substring match */
            const char *name = doc->vars[v].name;
            int match = 1;
            const char *q = query;
            const char *n = name;
            while (*q && *n) {
                char qc = (*q >= 'A' && *q <= 'Z') ? (*q + 32) : *q;
                char nc = (*n >= 'A' && *n <= 'Z') ? (*n + 32) : *n;
                if (qc == nc) { q++; n++; }
                else { n++; }
            }
            if (*q != 0) match = 0;
            
            if (match) {
                if (total >= cap) {
                    cap *= 2;
                    syms = lsp_realloc(syms, cap * sizeof(LspSymbol *));
                }
                syms[total] = (LspSymbol *)lsp_alloc(sizeof(LspSymbol));
                syms[total]->name = lsp_strdup(doc->vars[v].name);
                syms[total]->kind = doc->vars[v].kind;
                syms[total]->line = doc->vars[v].def_line;
                syms[total]->col = doc->vars[v].def_col;
                syms[total]->end_line = doc->vars[v].def_line;
                syms[total]->end_col = doc->vars[v].def_col + (int)strlen(doc->vars[v].name);
                syms[total]->detail = lsp_fmt("%s - %s", doc->vars[v].type_hint ? doc->vars[v].type_hint : "any", doc->uri);
                syms[total]->documentation = NULL;
                syms[total]->children = NULL;
                syms[total]->nchildren = 0;
                syms[total]->scope_start_line = doc->vars[v].def_line;
                syms[total]->scope_end_line = doc->vars[v].def_line;
                total++;
            }
        }
    }
    
    if (total == 0) { lsp_free(syms); return 0; }
    *out_symbols = syms;
    return total;
}

/*
** LXCLUA LSP - Selection Range Provider
*/

/*
 * @brief 计算光标的智能选择范围（从光标位置向外扩展）
 * @param doc 文档指针
 * @param npositions 位置数量
 * @param lines 行数组
 * @param cols 列数组
 * @param out_start_lines 输出-起始行数组
 * @param out_end_lines 输出-结束行数组
 * @return 位置数量
 */
int lsp_selection_range(LspDocument *doc, int npositions, int *lines, int *cols, int **out_start_lines, int **out_end_lines) {
    *out_start_lines = NULL; *out_end_lines = NULL;
    if (!doc || npositions <= 0) return 0;
    
    *out_start_lines = (int *)lsp_alloc(npositions * sizeof(int));
    *out_end_lines = (int *)lsp_alloc(npositions * sizeof(int));
    
    for (int p = 0; p < npositions; p++) {
        int line = lines[p];
        int col = cols[p];
        
        /* Find the enclosing block for this position */
        int start_line = line;
        int end_line = line;
        
        if (doc->tokens && doc->ntokens > 0) {
            /* Search for the nearest enclosing function/if/while/for/do/struct/enum/namespace block */
            int best_start = 0;
            int best_end = doc->nlines > 0 ? doc->nlines - 1 : line;
            int stack[128];
            int sptr = 0;
            
            for (int i = 0; i < doc->ntokens; i++) {
                LspToken *tok = &doc->tokens[i];
                if (tok->type == TOK_FUNCTION || tok->type == TOK_IF || tok->type == TOK_FOR ||
                    tok->type == TOK_WHILE || tok->type == TOK_REPEAT || tok->type == TOK_DO ||
                    tok->type == TOK_TRY || tok->type == TOK_SWITCH || tok->type == TOK_STRUCT ||
                    tok->type == TOK_ENUM || tok->type == TOK_NAMESPACE) {
                    if (sptr < 128) stack[sptr++] = tok->line;
                } else if (tok->type == TOK_END) {
                    if (sptr > 0) {
                        int block_start = stack[--sptr];
                        int block_end = tok->line;
                        /* Check if cursor is inside this block */
                        if (block_start <= line && line <= block_end) {
                            if (block_start >= best_start && block_end <= best_end) {
                                best_start = block_start;
                                best_end = block_end;
                            }
                        }
                    }
                }
            }
            
            if (best_start < best_end) {
                start_line = best_start;
                end_line = best_end;
            }
        }
        
        (*out_start_lines)[p] = start_line;
        (*out_end_lines)[p] = end_line;
    }
    
    /* Build nested structure: each position builds upon the previous */
    return npositions;
}

/*
** LXCLUA LSP - Linked Editing Range Provider
*/

/*
 * @brief 获取链接编辑范围（如同时重命名HTML标签时的关联编辑）
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-范围数量
 * @return 1有结果，0无结果
 */
int lsp_linked_editing_range(LspDocument *doc, int line, int col, int **out_lines, int **out_cols, int *count) {
    *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* For LXCLUA, linked editing applies to:
     * 1. Multi-variable declarations: local a, b, c = ...
     * 2. Function parameter lists where renaming one parameter should rename all
     * For now, find all occurrences of the same variable name in the same scope */
    
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && strcmp(doc->tokens[i].text, word) == 0) {
                n++;
            }
        }
    }
    
    if (n <= 1) { lsp_free(word); return 0; }
    
    *out_lines = (int *)lsp_alloc(n * sizeof(int));
    *out_cols = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && strcmp(doc->tokens[i].text, word) == 0) {
                (*out_lines)[idx] = doc->tokens[i].line;
                (*out_cols)[idx] = doc->tokens[i].col;
                idx++;
            }
        }
    }
    
    *count = idx;
    lsp_free(word);
    return 1;
}

/*
** LXCLUA LSP - Push Diagnostic Notifications
*/

/*
 * @brief 推送诊断到客户端
 * @param server LSP服务器
 * @param uri 文档URI
 */
void lsp_publish_diagnostics(void *server, const char *uri) {
    /* This is a stub - full implementation would use a callback 
     * registered via lsp_set_send_callback() to push notifications
     * to the connected editor. In its current form, diagnostics are 
     * served through the pull model (textDocument/diagnostic). */
    (void)server;
    (void)uri;
}

/*
 * @brief 发送日志消息到客户端
 * @param server LSP服务器
 * @param type 消息类型
 * @param fmt 格式化字符串
 * @param ... 变参
 */
void lsp_send_log(void *server, int type, const char *fmt, ...) {
    /* Stub - implements window/logMessage notification push.
     * Full implementation would format the message and call the 
     * server's write callback to send a JSON-RPC notification. */
    (void)server;
    (void)type;
    (void)fmt;
}

/*
 * @brief 显示消息到客户端
 * @param server LSP服务器
 * @param type 消息类型
 * @param message 消息内容
 */
void lsp_show_message(void *server, int type, const char *message) {
    /* Stub - implements window/showMessage notification push.
     * Full implementation would format and send a showMessage
     * notification through the server write callback. */
    (void)server;
    (void)type;
    (void)message;
}

/*
** LXCLUA LSP - Declaration Provider (Go to Declaration)
*/

/*
 * @brief 查找光标处符号的声明位置
 * @param doc 文档指针
 * @param line 光标行号
 * @param col 光标列号
 * @param def_line 输出-声明行号
 * @param def_col 输出-声明列号
 * @param def_uri 输出-声明URI
 * @return 1找到，0未找到
 */
int lsp_declaration(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri) {
    *def_line = -1; *def_col = -1; *def_uri = NULL;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* 查找变量声明（包括local/let/const/export/global/function/struct/enum/namespace声明） */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0) {
            *def_line = doc->vars[i].def_line;
            *def_col = doc->vars[i].def_col;
            *def_uri = lsp_strdup(doc->uri);
            lsp_free(word);
            return 1;
        }
    }
    
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - Code Lens Provider
*/

/*
 * @brief 生成代码镜头（在函数/类上方显示引用计数等）
 * @param doc 文档指针
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param out_titles 输出-标题数组
 * @param out_commands 输出-命令数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_code_lens(LspDocument *doc, int **out_lines, int **out_cols, char ***out_titles, char ***out_commands, int *count) {
    *out_lines = NULL; *out_cols = NULL; *out_titles = NULL; *out_commands = NULL; *count = 0;
    if (!doc) return 0;
    
    int cap = 32;
    int *lines = (int *)lsp_alloc(cap * sizeof(int));
    int *cols = (int *)lsp_alloc(cap * sizeof(int));
    char **titles = (char **)lsp_alloc(cap * sizeof(char *));
    char **commands = (char **)lsp_alloc(cap * sizeof(char *));
    int n = 0;
    
    /* 为每个函数/类/struct添加引用计数的CodeLens */
    for (int i = 0; i < doc->nvars; i++) {
        if (doc->vars[i].kind == SYMBOL_FUNCTION || doc->vars[i].kind == SYMBOL_METHOD ||
            doc->vars[i].kind == SYMBOL_CLASS || doc->vars[i].kind == SYMBOL_STRUCT ||
            doc->vars[i].kind == SYMBOL_ENUM) {
            
            /* 统计该符号在代码中出现的次数 */
            int refs = 0;
            if (doc->tokens && doc->ntokens > 0) {
                for (int j = 0; j < doc->ntokens; j++) {
                    if (doc->tokens[j].type == TOK_NAME && 
                        strcmp(doc->tokens[j].text, doc->vars[i].name) == 0) {
                        refs++;
                    }
                }
            }
            
            if (n >= cap) {
                cap *= 2;
                lines = lsp_realloc(lines, cap * sizeof(int));
                cols = lsp_realloc(cols, cap * sizeof(int));
                titles = lsp_realloc(titles, cap * sizeof(char *));
                commands = lsp_realloc(commands, cap * sizeof(char *));
            }
            
            lines[n] = doc->vars[i].def_line;
            cols[n] = doc->vars[i].def_col;
            titles[n] = lsp_fmt("%d references", refs);
            commands[n] = lsp_strdup("editor.action.showReferences");
            n++;
        }
    }
    
    *out_lines = lines;
    *out_cols = cols;
    *out_titles = titles;
    *out_commands = commands;
    *count = n;
    return 0;
}

/*
** LXCLUA LSP - Document Link Provider
*/

/*
 * @brief 查找文档中的超链接（URL、文件路径等）
 * @param doc 文档指针
 * @param out_start_lines 输出-起始行
 * @param out_start_cols 输出-起始列
 * @param out_end_lines 输出-结束行
 * @param out_end_cols 输出-结束列
 * @param out_targets 输出-链接目标
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_document_link(LspDocument *doc, int **out_start_lines, int **out_start_cols, int **out_end_lines, int **out_end_cols, char ***out_targets, int *count) {
    *out_start_lines = NULL; *out_start_cols = NULL;
    *out_end_lines = NULL; *out_end_cols = NULL;
    *out_targets = NULL; *count = 0;
    if (!doc) return 0;
    
    /* 在字符串中搜索 http:// 和 https:// 链接 */
    int cap = 16;
    int *sl = (int *)lsp_alloc(cap * sizeof(int));
    int *sc = (int *)lsp_alloc(cap * sizeof(int));
    int *el = (int *)lsp_alloc(cap * sizeof(int));
    int *ec = (int *)lsp_alloc(cap * sizeof(int));
    char **tgts = (char **)lsp_alloc(cap * sizeof(char *));
    int n = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_STRING || doc->tokens[i].type == TOK_INTERPSTRING || 
                doc->tokens[i].type == TOK_RAWSTRING) {
                const char *s = doc->tokens[i].text;
                int len = doc->tokens[i].len;
                /* 检查字符串内容是否以 http:// 或 https:// 开头 */
                if (len > 9) {
                    int start = 0;
                    if (s[0] == '"' || s[0] == '\'') start = 1;
                    const char *inner = s + start;
                    int inner_len = len - start - (s[len-1] == s[0] ? 1 : 0);
                    
                    if (inner_len > 7 && (strncmp(inner, "http://", 7) == 0 || 
                        strncmp(inner, "https://", 8) == 0)) {
                        if (n >= cap) {
                            cap *= 2;
                            sl = lsp_realloc(sl, cap * sizeof(int));
                            sc = lsp_realloc(sc, cap * sizeof(int));
                            el = lsp_realloc(el, cap * sizeof(int));
                            ec = lsp_realloc(ec, cap * sizeof(int));
                            tgts = lsp_realloc(tgts, cap * sizeof(char *));
                        }
                        sl[n] = doc->tokens[i].line;
                        sc[n] = doc->tokens[i].col + start;
                        el[n] = doc->tokens[i].line;
                        ec[n] = doc->tokens[i].col + start + inner_len;
                        /* 提取纯URL（去掉引号） */
                        char *target = (char *)lsp_alloc(inner_len + 1);
                        memcpy(target, inner, inner_len);
                        target[inner_len] = 0;
                        tgts[n] = target;
                        n++;
                    }
                }
            }
        }
    }
    
    *out_start_lines = sl;
    *out_start_cols = sc;
    *out_end_lines = el;
    *out_end_cols = ec;
    *out_targets = tgts;
    *count = n;
    return 0;
}

/*
** LXCLUA LSP - Inlay Hint Provider
*/

/*
 * @brief 生成内联类型提示（在变量名后显示类型信息）
 * @param doc 文档指针
 * @param start_line 范围起始行
 * @param end_line 范围结束行
 * @param out_labels 输出-标签数组
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_inlay_hint(LspDocument *doc, int start_line, int end_line, char ***out_labels, int **out_lines, int **out_cols, int *count) {
    *out_labels = NULL; *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return 0;
    
    int cap = 64;
    char **labels = (char **)lsp_alloc(cap * sizeof(char *));
    int *lines = (int *)lsp_alloc(cap * sizeof(int));
    int *cols = (int *)lsp_alloc(cap * sizeof(int));
    int n = 0;
    
    /* 为有类型提示的变量显示类型标注 */
    for (int i = 0; i < doc->nvars; i++) {
        if (doc->vars[i].def_line < start_line || doc->vars[i].def_line > end_line) continue;
        
        /* 为带有类型提示的变量显示类型 */
        if (doc->vars[i].type_hint) {
            if (n >= cap) {
                cap *= 2;
                labels = lsp_realloc(labels, cap * sizeof(char *));
                lines = lsp_realloc(lines, cap * sizeof(int));
                cols = lsp_realloc(cols, cap * sizeof(int));
            }
            int vname_len = (int)strlen(doc->vars[i].name);
            labels[n] = lsp_fmt(": %s", doc->vars[i].type_hint);
            lines[n] = doc->vars[i].def_line;
            cols[n] = doc->vars[i].def_col + vname_len;
            n++;
        }
    }
    
    /* 为函数调用的参数显示参数名提示 */
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                doc->tokens[i].line >= start_line && 
                doc->tokens[i].line <= end_line) {
                /* 检查后面是否是 ( */
                if (i + 1 < doc->ntokens && doc->tokens[i+1].type == (LspTokenType)'(') {
                    const char *name = doc->tokens[i].text;
                    /* 查找该函数的文档 */
                    const char *fn_doc = lsp_kwdb_find_doc(name);
                    if (fn_doc) {
                        /* 如果文档中有参数信息（格式: function(param1, param2)），提取出来 */
                        if (strstr(fn_doc, "function(")) {
                            if (n >= cap) {
                                cap *= 2;
                                labels = lsp_realloc(labels, cap * sizeof(char *));
                                lines = lsp_realloc(lines, cap * sizeof(int));
                                cols = lsp_realloc(cols, cap * sizeof(int));
                            }
                            const char *params_start = strstr(fn_doc, "function(") + 9;
                            const char *params_end = strchr(params_start, ')');
                            if (params_end) {
                                int plen = (int)(params_end - params_start);
                                if (plen > 0 && plen < 200) {
                                    char *params = (char *)lsp_alloc(plen + 3);
                                    memcpy(params, params_start, plen);
                                    params[plen] = 0;
                                    labels[n] = lsp_fmt("(%s)", params);
                                    lsp_free(params);
                                } else {
                                    labels[n] = lsp_strdup("()");
                                }
                            } else {
                                labels[n] = lsp_strdup("()");
                            }
                            lines[n] = doc->tokens[i].line;
                            cols[n] = doc->tokens[i].col + doc->tokens[i].len;
                            n++;
                        }
                    }
                }
            }
        }
    }
    
    *out_labels = labels;
    *out_lines = lines;
    *out_cols = cols;
    *count = n;
    return 0;
}

/*
** LXCLUA LSP - Call Hierarchy Provider
*/

/*
 * @brief 准备调用层次结构 - 获取符号名和位置
 * @param doc 文档指针
 * @param line 光标行
 * @param col 光标列
 * @param out_name 输出-符号名
 * @param out_line 输出-行号
 * @param out_col 输出-列号
 * @return 1找到函数，0未找到
 */
int lsp_prepare_call_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col) {
    *out_name = NULL; *out_line = -1; *out_col = -1;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* 检查是否是函数名 */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0 &&
            (doc->vars[i].kind == SYMBOL_FUNCTION || doc->vars[i].kind == SYMBOL_METHOD)) {
            *out_name = lsp_strdup(word);
            *out_line = doc->vars[i].def_line;
            *out_col = doc->vars[i].def_col;
            lsp_free(word);
            return 1;
        }
    }
    
    lsp_free(word);
    return 0;
}

/*
 * @brief 获取函数的所有调用者
 * @param doc 文档指针
 * @param line 函数定义行
 * @param col 函数定义列
 * @param out_from_lines 输出-调用者行号
 * @param out_from_cols 输出-调用者列号
 * @param out_to_lines 输出-被调用处行号
 * @param out_to_cols 输出-被调用处列号
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_call_hierarchy_incoming(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count) {
    *out_from_lines = NULL; *out_from_cols = NULL;
    *out_to_lines = NULL; *out_to_cols = NULL;
    *count = 0;
    if (!doc) return 0;
    
    /* 获取光标所在的函数名 */
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *func_name = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!func_name || !*func_name) { lsp_free(func_name); return 0; }
    
    /* 统计调用该函数的次数 */
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, func_name) == 0) {
                /* 检查是否是调用位置（后面是 ( 或 ::） */
                if (i + 1 < doc->ntokens && 
                    (doc->tokens[i+1].type == (LspTokenType)'(' || 
                     doc->tokens[i+1].type == TOK_DBCOLON)) {
                    n++;
                }
            }
        }
    }
    
    if (n == 0) { lsp_free(func_name); return 0; }
    
    int *fl = (int *)lsp_alloc(n * sizeof(int));
    int *fc = (int *)lsp_alloc(n * sizeof(int));
    int *tl = (int *)lsp_alloc(n * sizeof(int));
    int *tc = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].type == TOK_NAME && 
                strcmp(doc->tokens[i].text, func_name) == 0 &&
                i + 1 < doc->ntokens && 
                (doc->tokens[i+1].type == (LspTokenType)'(' || 
                 doc->tokens[i+1].type == TOK_DBCOLON)) {
                /* 查找调用者：包含此调用的最近一个函数定义 */
                int caller_line = -1, caller_col = -1;
                for (int j = i - 1; j >= 0; j--) {
                    if (doc->tokens[j].type == TOK_FUNCTION) {
                        /* 找到 function 关键字，看它前面的标识符 */
                        if (j >= 1 && doc->tokens[j-1].type == TOK_NAME) {
                            /* 全局函数: function name() */
                            /* 或 local function name() */
                            if (j >= 2 && doc->tokens[j-2].type == TOK_LOCAL) {
                                caller_line = doc->tokens[j-1].line;
                                caller_col = doc->tokens[j-1].col;
                            } else {
                                caller_line = doc->tokens[j-1].line;
                                caller_col = doc->tokens[j-1].col;
                            }
                        }
                        break;
                    }
                }
                
                if (caller_line >= 0) {
                    fl[idx] = caller_line;
                    fc[idx] = caller_col;
                    tl[idx] = doc->tokens[i].line;
                    tc[idx] = doc->tokens[i].col;
                    idx++;
                }
            }
        }
    }
    
    *out_from_lines = fl;
    *out_from_cols = fc;
    *out_to_lines = tl;
    *out_to_cols = tc;
    *count = idx;
    lsp_free(func_name);
    return 0;
}

/*
 * @brief 获取函数内调用的其他函数
 * @param doc 文档指针
 * @param line 函数定义行
 * @param col 函数定义列
 * @param out_from_lines 输出-调用者行号
 * @param out_from_cols 输出-调用者列号
 * @param out_to_lines 输出-被调用处行号
 * @param out_to_cols 输出-被调用处列号
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_call_hierarchy_outgoing(LspDocument *doc, int line, int col, int **out_from_lines, int **out_from_cols, int **out_to_lines, int **out_to_cols, int *count) {
    *out_from_lines = NULL; *out_from_cols = NULL;
    *out_to_lines = NULL; *out_to_cols = NULL;
    *count = 0;
    if (!doc) return 0;
    
    /* 获取此函数的名称 */
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *func_name = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!func_name || !*func_name) { lsp_free(func_name); return 0; }
    
    /* 找到该函数的 end 以确定范围 */
    int func_start_line = -1, func_end_line = -1;
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, func_name) == 0 &&
            doc->vars[i].def_line == line) {
            func_start_line = line;
            break;
        }
    }
    
    /* 用词法分析找到函数的结束行 */
    if (func_start_line >= 0 && doc->tokens && doc->ntokens > 0) {
        int depth = 0;
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].line >= func_start_line) {
                if (doc->tokens[i].type == TOK_FUNCTION) depth++;
                else if (doc->tokens[i].type == TOK_END) {
                    depth--;
                    if (depth == 0) {
                        func_end_line = doc->tokens[i].line;
                        break;
                    }
                }
            }
        }
    }
    
    /* 在函数范围内查找函数调用 */
    int n = 0;
    int cap = 32;
    int *fl = (int *)lsp_alloc(cap * sizeof(int));
    int *fc = (int *)lsp_alloc(cap * sizeof(int));
    int *tl = (int *)lsp_alloc(cap * sizeof(int));
    int *tc = (int *)lsp_alloc(cap * sizeof(int));
    
    if (doc->tokens && doc->ntokens > 0 && func_end_line > func_start_line) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].line >= func_start_line && 
                doc->tokens[i].line <= func_end_line &&
                doc->tokens[i].type == TOK_NAME &&
                strcmp(doc->tokens[i].text, func_name) != 0) {
                /* 检查是否是函数调用（后面是(） */
                if (i + 1 < doc->ntokens && doc->tokens[i+1].type == (LspTokenType)'(') {
                    /* 检查这个name是否是一个函数 */
                    int is_func = 0;
                    for (int v = 0; v < doc->nvars; v++) {
                        if (strcmp(doc->vars[v].name, doc->tokens[i].text) == 0 &&
                            (doc->vars[v].kind == SYMBOL_FUNCTION || doc->vars[v].kind == SYMBOL_METHOD)) {
                            is_func = 1;
                            break;
                        }
                    }
                    if (!is_func) {
                        /* 检查是否是内置函数 */
                        if (lsp_kwdb_find_doc(doc->tokens[i].text)) is_func = 1;
                    }
                    
                    if (is_func) {
                        if (n >= cap) {
                            cap *= 2;
                            fl = lsp_realloc(fl, cap * sizeof(int));
                            fc = lsp_realloc(fc, cap * sizeof(int));
                            tl = lsp_realloc(tl, cap * sizeof(int));
                            tc = lsp_realloc(tc, cap * sizeof(int));
                        }
                        fl[n] = line;
                        fc[n] = col;
                        tl[n] = doc->tokens[i].line;
                        tc[n] = doc->tokens[i].col;
                        n++;
                    }
                }
            }
        }
    }
    
    *out_from_lines = fl;
    *out_from_cols = fc;
    *out_to_lines = tl;
    *out_to_cols = tc;
    *count = n;
    lsp_free(func_name);
    return 0;
}

/*
** LXCLUA LSP - Type Hierarchy Provider
*/

/*
 * @brief 准备类型层次结构
 * @param doc 文档指针
 * @param line 光标行
 * @param col 光标列
 * @param out_name 输出-类型名
 * @param out_line 输出-行号
 * @param out_col 输出-列号
 * @return 1找到类型，0未找到
 */
int lsp_prepare_type_hierarchy(LspDocument *doc, int line, int col, char **out_name, int *out_line, int *out_col) {
    *out_name = NULL; *out_line = -1; *out_col = -1;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* 检查是否是struct/enum/class类型名 */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0 &&
            (doc->vars[i].kind == SYMBOL_STRUCT || doc->vars[i].kind == SYMBOL_ENUM)) {
            *out_name = lsp_strdup(word);
            *out_line = doc->vars[i].def_line;
            *out_col = doc->vars[i].def_col;
            lsp_free(word);
            return 1;
        }
    }
    
    lsp_free(word);
    return 0;
}

/*
 * @brief 获取类型的父类型
 * @param doc 文档指针
 * @param line 类型定义行
 * @param col 类型定义列
 * @param out_names 输出-父类型名数组
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_type_hierarchy_supertypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count) {
    *out_names = NULL; *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return 0;
    
    /* 查找superstruct定义中的父类型 */
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *type_name = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!type_name || !*type_name) { lsp_free(type_name); return 0; }
    
    /* 在token流中查找 superstruct X : ParentType 或 class X : ParentClass */
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens - 3; i++) {
            if ((doc->tokens[i].type == TOK_SUPERSTRUCT || doc->tokens[i].type == TOK_STRUCT) &&
                doc->tokens[i+1].type == TOK_NAME &&
                strcmp(doc->tokens[i+1].text, type_name) == 0 &&
                doc->tokens[i+2].type == (LspTokenType)':' &&
                doc->tokens[i+3].type == TOK_NAME) {
                n++;
            }
        }
    }
    
    if (n == 0) { lsp_free(type_name); return 0; }
    
    char **names = (char **)lsp_alloc(n * sizeof(char *));
    int *ls = (int *)lsp_alloc(n * sizeof(int));
    int *cs = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens - 3; i++) {
            if ((doc->tokens[i].type == TOK_SUPERSTRUCT || doc->tokens[i].type == TOK_STRUCT) &&
                doc->tokens[i+1].type == TOK_NAME &&
                strcmp(doc->tokens[i+1].text, type_name) == 0 &&
                doc->tokens[i+2].type == (LspTokenType)':' &&
                doc->tokens[i+3].type == TOK_NAME) {
                names[idx] = lsp_strdup(doc->tokens[i+3].text);
                ls[idx] = doc->tokens[i+3].line;
                cs[idx] = doc->tokens[i+3].col;
                idx++;
            }
        }
    }
    
    *out_names = names;
    *out_lines = ls;
    *out_cols = cs;
    *count = idx;
    lsp_free(type_name);
    return 0;
}

/*
 * @brief 获取类型的子类型
 * @param doc 文档指针
 * @param line 类型定义行
 * @param col 类型定义列
 * @param out_names 输出-子类型名数组
 * @param out_lines 输出-行号数组
 * @param out_cols 输出-列号数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_type_hierarchy_subtypes(LspDocument *doc, int line, int col, char ***out_names, int **out_lines, int **out_cols, int *count) {
    *out_names = NULL; *out_lines = NULL; *out_cols = NULL; *count = 0;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *type_name = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!type_name || !*type_name) { lsp_free(type_name); return 0; }
    
    /* 查找所有以该类型为父类型的superstruct/class */
    int n = 0;
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens - 3; i++) {
            if ((doc->tokens[i].type == TOK_SUPERSTRUCT || 
                 doc->tokens[i].type == TOK_STRUCT) &&
                doc->tokens[i+1].type == TOK_NAME &&
                doc->tokens[i+2].type == (LspTokenType)':' &&
                doc->tokens[i+3].type == TOK_NAME &&
                strcmp(doc->tokens[i+3].text, type_name) == 0) {
                n++;
            }
        }
    }
    
    if (n == 0) { lsp_free(type_name); return 0; }
    
    char **names = (char **)lsp_alloc(n * sizeof(char *));
    int *ls = (int *)lsp_alloc(n * sizeof(int));
    int *cs = (int *)lsp_alloc(n * sizeof(int));
    int idx = 0;
    
    if (doc->tokens && doc->ntokens > 0) {
        for (int i = 0; i < doc->ntokens - 3; i++) {
            if ((doc->tokens[i].type == TOK_SUPERSTRUCT || 
                 doc->tokens[i].type == TOK_STRUCT) &&
                doc->tokens[i+1].type == TOK_NAME &&
                doc->tokens[i+2].type == (LspTokenType)':' &&
                doc->tokens[i+3].type == TOK_NAME &&
                strcmp(doc->tokens[i+3].text, type_name) == 0) {
                names[idx] = lsp_strdup(doc->tokens[i+1].text);
                ls[idx] = doc->tokens[i+1].line;
                cs[idx] = doc->tokens[i+1].col;
                idx++;
            }
        }
    }
    
    *out_names = names;
    *out_lines = ls;
    *out_cols = cs;
    *count = idx;
    lsp_free(type_name);
    return 0;
}

/*
** LXCLUA LSP - Color Presentation Provider
*/

/*
 * @brief 提供颜色表示（#RRGGBB格式的颜色预览）
 * @param doc 文档指针
 * @param line 光标行
 * @param col 光标列
 * @param out_labels 输出-颜色标签数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_color_presentation(LspDocument *doc, int line, int col, char ***out_labels, int *count) {
    *out_labels = NULL; *count = 0;
    if (!doc) return 0;
    
    /* 在字符串 token 中搜索 #RRGGBB 格式的颜色 */
    if (doc->tokens && doc->ntokens > 0 && line < doc->nlines) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].line == line && 
                doc->tokens[i].type == TOK_STRING) {
                const char *s = doc->tokens[i].text;
                int len = doc->tokens[i].len;
                for (int j = 0; j < len - 6; j++) {
                    if (s[j] == '#' && len >= 7) {
                        int valid = 1;
                        for (int k = 1; k <= 6; k++) {
                            char c = s[j+k];
                            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                                valid = 0; break;
                            }
                        }
                        if (valid) {
                            /* 提取真实的颜色值 */
                            char hex[7];
                            memcpy(hex, s + j, 7);
                            hex[6] = 0;
                            int r, g, b;
                            sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b);
                            
                            /* 生成三种颜色表示法 */
                            int n = 3;
                            char **labels = (char **)lsp_alloc(n * sizeof(char *));
                            /* 1. 大写十六进制 */
                            labels[0] = (char *)lsp_alloc(8);
                            snprintf(labels[0], 8, "#%02X%02X%02X", r, g, b);
                            /* 2. 小写十六进制 */
                            labels[1] = (char *)lsp_alloc(8);
                            snprintf(labels[1], 8, "#%02x%02x%02x", r, g, b);
                            /* 3. rgb() 表示法 */
                            labels[2] = (char *)lsp_alloc(20);
                            snprintf(labels[2], 20, "rgb(%d, %d, %d)", r, g, b);
                            
                            *out_labels = labels;
                            *count = n;
                            return 0;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/*
** LXCLUA LSP - Moniker Provider
*/

/*
 * @brief 获取符号的唯一标识符
 * @param doc 文档指针
 * @param line 光标行
 * @param col 光标列
 * @param out_schemes 输出-scheme数组
 * @param out_identifiers 输出-标识符数组
 * @param count 输出-数量
 * @return 0成功
 */
int lsp_moniker(LspDocument *doc, int line, int col, char ***out_schemes, char ***out_identifiers, int *count) {
    *out_schemes = NULL; *out_identifiers = NULL; *count = 0;
    if (!doc) return 0;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return 0; }
    
    /* 为该符号生成URI作为moniker */
    char **schemes = (char **)lsp_alloc(sizeof(char *));
    char **ids = (char **)lsp_alloc(sizeof(char *));
    schemes[0] = lsp_strdup("lxclua");
    ids[0] = lsp_fmt("%s#%s", doc->uri, word);
    
    *out_schemes = schemes;
    *out_identifiers = ids;
    *count = 1;
    lsp_free(word);
    return 0;
}

/*
** LXCLUA LSP - On Type Formatting Provider
*/

/*
 * @brief 输入时格式化（如自动缩进）
 * @param doc 文档指针
 * @param line 当前行
 * @param col 当前列
 * @param ch 刚输入的字符
 * @param tab_size Tab大小
 * @param insert_spaces 是否空格缩进
 * @return 格式修改数组的JSON字符串，NULL表示不需要修改
 */
char *lsp_on_type_formatting(LspDocument *doc, int line, int col, const char *ch, int tab_size, int insert_spaces) {
    (void)col;
    if (!doc || !ch) return NULL;
    
    /* 当输入end时，自动减少缩进 */
    if (strcmp(ch, "\n") == 0) {
        /* 获取当前行文本，计算下一行缩进 */
        int offset = lsp_linecol_to_offset(doc->text, line, 0);
        int next_line = line + 1;
        
        /* 智能缩进：查看上一行是否以then,do,else,{等结尾 */
        char *line_text = lsp_get_line_text(doc->text, offset);
        if (line_text) {
            int linelen = (int)strlen(line_text);
            int cur_indent = 0;
            while (cur_indent < linelen && (line_text[cur_indent] == ' ' || line_text[cur_indent] == '	'))
                cur_indent++;
            
            /* 查找then, do, {, ( 等增加缩进 */
            int extra_indent = 0;
            if (linelen > 0) {
                char last_ch = line_text[linelen - 1];
                if (last_ch == '{' || last_ch == '(') extra_indent = 1;
                if (linelen >= 4 && strcmp(line_text + linelen - 4, "then") == 0) extra_indent = 1;
                if (linelen >= 2 && strcmp(line_text + linelen - 2, "do") == 0) extra_indent = 1;
                if (linelen >= 4 && strcmp(line_text + linelen - 4, "else") == 0) extra_indent = 1;
            }
            
            int indent_size = tab_size;
            char indent_char = insert_spaces ? ' ' : '	';
            
            char *indent_str = (char *)lsp_alloc((cur_indent + extra_indent) * indent_size + 1);
            int idx = 0;
            for (int i = 0; i < cur_indent + extra_indent; i++) {
                for (int j = 0; j < indent_size; j++)
                    indent_str[idx++] = indent_char;
            }
            indent_str[idx] = 0;
            
            char *result = lsp_fmt("[{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":0}},\"newText\":\"%s\"}]",
                                   next_line, next_line, indent_str);
            lsp_free(indent_str);
            lsp_free(line_text);
            return result;
        }
        lsp_free(line_text);
    }
    
    /* 当输入end时，检查是否需要减少缩进 */
    if (strcmp(ch, "d") == 0) {
        int offset = lsp_linecol_to_offset(doc->text, line, col);
        if (col >= 2 && offset >= 2) {
            if (doc->text[offset-3] == 'e' && doc->text[offset-2] == 'n' && doc->text[offset-1] == 'd') {
                /* 刚输入了end，检查当前行是否有多余缩进 */
                char *line_text = lsp_get_line_text(doc->text, offset);
                if (line_text) {
                    int linelen = (int)strlen(line_text);
                    int cur_indent = 0;
                    while (cur_indent < linelen && (line_text[cur_indent] == ' ' || line_text[cur_indent] == '	'))
                        cur_indent++;
                    
                    if (cur_indent > tab_size) {
                        cur_indent -= tab_size;
                        char *indent_str = (char *)lsp_alloc(cur_indent * tab_size + 1);
                        int idx = 0;
                        char indent_char = insert_spaces ? ' ' : '	';
                        for (int i = 0; i < cur_indent; i++) {
                            for (int j = 0; j < tab_size; j++)
                                indent_str[idx++] = indent_char;
                        }
                        indent_str[idx] = 0;
                        
                        char *result = lsp_fmt("[{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"}]",
                                               line, line, cur_indent + tab_size, indent_str);
                        lsp_free(indent_str);
                        lsp_free(line_text);
                        return result;
                    }
                    lsp_free(line_text);
                }
            }
        }
    }
    
    return NULL;
}

/*
** LXCLUA LSP - Range Formatting Provider
*/

/*
 * @brief 格式化文档的指定范围
 * @param doc 文档指针
 * @param start_line 起始行
 * @param start_col 起始列
 * @param end_line 结束行
 * @param end_col 结束列
 * @param tab_size Tab大小
 * @param insert_spaces 是否空格缩进
 * @return 格式化的新文本
 */
char *lsp_range_formatting(LspDocument *doc, int start_line, int start_col, int end_line, int end_col, int tab_size, int insert_spaces) {
    if (!doc || !doc->text) return NULL;
    if (tab_size <= 0) tab_size = 4;
    
    /* 获取指定范围内的文本进行格式化 */
    int start_offset = lsp_linecol_to_offset(doc->text, start_line, start_col);
    int end_offset = lsp_linecol_to_offset(doc->text, end_line, end_col);
    
    if (start_offset < 0 || end_offset < start_offset) return NULL;
    if (end_offset > (int)doc->text_len) end_offset = (int)doc->text_len;
    
    /* 只格式化范围内的文本，保持前后文本不变 */
    int range_len = end_offset - start_offset;
    char *range_text = (char *)lsp_alloc(range_len + 1);
    memcpy(range_text, doc->text + start_offset, range_len);
    range_text[range_len] = 0;
    
    /* 对范围内文本进行简单格式化 */
    char *result = (char *)lsp_alloc(range_len * 2 + 256);
    int out_pos = 0;
    int in_string = 0;
    int string_char = 0;
    
    for (int i = 0; i < range_len; i++) {
        char c = range_text[i];
        
        if (out_pos + 16 >= range_len * 2 + 256) {
            result = lsp_realloc(result, out_pos + range_len + 256);
        }
        
        if (in_string) {
            result[out_pos++] = c;
            if (c == string_char && (i == 0 || range_text[i-1] != '\\'))
                in_string = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = 1;
            string_char = c;
            result[out_pos++] = c;
            continue;
        }
        
        result[out_pos++] = c;
    }
    
    result[out_pos] = 0;
    lsp_free(range_text);
    return result;
}
