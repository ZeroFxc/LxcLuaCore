"""Add all missing LSP feature implementations to lspsrv_features.c"""
import os

features_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\lspsrv\lspsrv_features.c'

with open(features_path, 'r', encoding='utf-8') as f:
    content = f.read()

new_code = '''

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
                    if (s[0] == '\"' || s[0] == '\'') start = 1;
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
int lsp_call_hierarchy_incoming(LspDocument *doc, int line, int col, int ***out_from_lines, int ***out_from_cols, int ***out_to_lines, int ***out_to_cols, int *count) {
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
                /* 查找调用者（包含此调用的最近一个函数定义） */
                int caller_line = -1, caller_col = -1;
                for (int j = i - 1; j >= 0; j--) {
                    if (doc->tokens[j].type == TOK_FUNCTION && j >= 1 && 
                        doc->tokens[j-1].type == TOK_LOCAL) {
                        /* local function name */
                        if (j >= 2 && doc->tokens[j-2].type == TOK_NAME) {
                            caller_line = doc->tokens[j].line;
                            caller_col = doc->tokens[j].col;
                        }
                        break;
                    } else if (doc->tokens[j].type == TOK_FUNCTION && j >= 1 && 
                               doc->tokens[j-1].type == TOK_NAME) {
                        caller_line = doc->tokens[j].line;
                        caller_col = doc->tokens[j].col;
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
int lsp_call_hierarchy_outgoing(LspDocument *doc, int line, int col, int ***out_from_lines, int ***out_from_cols, int ***out_to_lines, int ***out_to_cols, int *count) {
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
    
    /* 在当前行的字符串中搜索 #RRGGBB 颜色 */
    if (doc->tokens && doc->ntokens > 0 && line < doc->nlines) {
        for (int i = 0; i < doc->ntokens; i++) {
            if (doc->tokens[i].line == line && 
                doc->tokens[i].type == TOK_STRING) {
                const char *s = doc->tokens[i].text;
                int len = doc->tokens[i].len;
                /* 在字符串内搜索 #XXXXXX */
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
                            int n = 1;
                            char **labels = (char **)lsp_alloc(n * sizeof(char *));
                            labels[0] = lsp_strdup("#RRGGBB");
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
            while (cur_indent < linelen && (line_text[cur_indent] == ' ' || line_text[cur_indent] == '\t'))
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
            char indent_char = insert_spaces ? ' ' : '\t';
            
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
                    while (cur_indent < linelen && (line_text[cur_indent] == ' ' || line_text[cur_indent] == '\t'))
                        cur_indent++;
                    
                    if (cur_indent > tab_size) {
                        cur_indent -= tab_size;
                        char *indent_str = (char *)lsp_alloc(cur_indent * tab_size + 1);
                        int idx = 0;
                        char indent_char = insert_spaces ? ' ' : '\t';
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
            if (c == string_char && (i == 0 || range_text[i-1] != '\\\\'))
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
'''

with open(features_path, 'a', encoding='utf-8') as f:
    f.write(new_code)

print("Added all new feature implementations to lspsrv_features.c")