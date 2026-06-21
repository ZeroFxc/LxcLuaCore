/*
** LXCLUA LSP - Code Completion Provider
** Provides intelligent code completion suggestions based on context.
*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "lspsrv.h"

#define MAX_COMPLETION_ITEMS 100   /* 补全项上限 */

/* External declarations (from other modules) */
extern int lsp_kwdb_get_keywords(LspKeywordEntry **out);
extern int lsp_kwdb_get_builtins(LspKeywordEntry **out);
extern int lsp_kwdb_get_stdlib(LspKeywordEntry **out);
extern int lsp_kwdb_get_snippets(LspKeywordEntry **out);
extern const char *lsp_kwdb_find_doc(const char *name);

/*
 * @brief 检查字符串是否以某前缀开始
 * @param str 完整字符串
 * @param prefix 前缀
 * @param prefix_len 前缀长度
 * @return 1匹配，0不匹配
 */
static int strn_prefix(const char *str, const char *prefix, int prefix_len) {
    return strncmp(str, prefix, prefix_len) == 0;
}

/*
 * @brief 计算补全项的排序优先级
 * @param item 补全项
 * @param prefix 用户输入的前缀
 * @return 排序优先级（越大越先显示）
 */
static int calc_priority(LspKeywordEntry *entry, const char *prefix) {
    int pri = 0;
    /* Keyword base priorities */
    if (entry->kind == COMPLETION_KEYWORD) pri = 70;
    if (entry->kind == COMPLETION_FUNCTION) pri = 60;
    if (entry->kind == COMPLETION_SNIPPET) pri = 30;
    /* 前缀完全匹配boost */
    if (prefix && entry->name && strncmp(entry->name, prefix, strlen(prefix)) == 0) {
        pri += 100;
        /* 大小写完全匹配的额外加分 */
        if (entry->name[0] >= 'a' && prefix[0] >= 'a') pri += 50;
    }
    return pri;
}

/*
 * @brief 复制关键字条目为补全项
 * @param dst 目标补全项
 * @param src 源关键字条目
 * @param priority 排序优先级
 */
static void copy_kw_to_item(LspCompletionItem *dst, LspKeywordEntry *src, int priority) {
    dst->label = lsp_strdup(src->name);
    dst->kind = src->kind;
    dst->detail = lsp_strdup(src->detail ? src->detail : "");
    dst->documentation = lsp_strdup(src->documentation ? src->documentation : "");
    dst->insert_text = src->snippet ? lsp_strdup(src->snippet) : lsp_strdup(src->name);
    dst->insert_text_format = src->snippet ? INSERT_TEXT_SNIPPET : INSERT_TEXT_PLAIN;
    dst->sort_text_priority = priority;
}

/*
 * @brief 分析token流，确定当前补全上下文
 * @param doc 文档
 * @param target_line 目标行号
 * @param target_col 目标列号
 * @param prefix 输出-前缀字符串
 * @param context 输出-上下文类型字符串（"table_field", "method_call", "global", "local", "keyword", "comment", "string"）
 */
static void analyze_context(LspDocument *doc, int target_line, int target_col, 
                            char **prefix, const char **context, char **table_name) {
    *prefix = NULL;
    *context = "global";
    *table_name = NULL;
    
    if (!doc->tokens || doc->ntokens == 0) return;
    
    LspToken *tokens = doc->tokens;
    int ntokens = doc->ntokens;
    
    /* 找到光标前的token */
    int best_idx = -1;
    for (int i = 0; i < ntokens; i++) {
        if (tokens[i].line < target_line || 
            (tokens[i].line == target_line && tokens[i].col + tokens[i].len <= target_col)) {
            best_idx = i;
        } else {
            break;
        }
    }
    
    if (best_idx < 0) { *context = "keyword"; return; }
    
    /* 获取当前单词前缀 */
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, 
        lsp_linecol_to_offset(doc->text, target_line, target_col), 
        &word_start, &word_end);
    *prefix = word;
    
    /* 分析上下文 */
    /* 检查是否在table.field或obj:method */
    if (best_idx >= 1 && tokens[best_idx].type == (LspTokenType)'.') {
        *context = "table_field";
        /* 提取表名：'.' 前面的 token */
        if (best_idx >= 2 && tokens[best_idx-1].type == TOK_NAME) {
            *table_name = lsp_strdup(tokens[best_idx-1].text);
        }
        return;
    }
    if (best_idx >= 1 && tokens[best_idx].type == (LspTokenType)':') {
        *context = "method_call";
        /* 提取表名：':' 前面的 token */
        if (best_idx >= 2 && tokens[best_idx-1].type == TOK_NAME) {
            *table_name = lsp_strdup(tokens[best_idx-1].text);
        }
        return;
    }
    if (best_idx >= 1 && tokens[best_idx].type == TOK_OPTCHAIN) {
        *context = "table_field";
        return;
    }
    /* 检测注释中 */
    if (tokens[best_idx].type == TOK_COMMENT || tokens[best_idx].type == TOK_MCOMMENT) {
        *context = "comment";
        return;
    }
    /* 检测字符串中 */
    if (tokens[best_idx].type == TOK_STRING || tokens[best_idx].type == TOK_INTERPSTRING) {
        *context = "string";
        return;
    }
    /* 检测new/import语句后 */
    if (tokens[best_idx].type == TOK_LOCAL) {
        *context = "local";
        return;
    }
    /* 检测"."或":"后的字段名 */
    if (best_idx >= 1 && (tokens[best_idx-1].type == (LspTokenType)'.' || 
                          tokens[best_idx-1].type == (LspTokenType)':')) {
        *context = "table_field";
        /* 提取表名：'.' 或 ':' 前面的 token */
        if (best_idx >= 3 && tokens[best_idx-2].type == TOK_NAME) {
            *table_name = lsp_strdup(tokens[best_idx-2].text);
        }
        return;
    }
    /* 全局/表达式上下文 */
    *context = "global";
}

/*
 * @brief 添加匹配的补全候选项
 * @param entries 条目数组
 * @param count 条目数量
 * @param prefix 过滤前缀
 * @param items 输出补全项数组
 * @param n_items 输出-已添加数量
 */
static void add_matching(LspKeywordEntry *entries, int count, const char *prefix, 
                         LspCompletionItem **items, int *n_items) {
    for (int i = 0; i < count && entries[i].name && *n_items < MAX_COMPLETION_ITEMS; i++) {
        if (!prefix || !*prefix || strn_prefix(entries[i].name, prefix, (int)strlen(prefix))) {
            (*n_items)++;
            *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
            copy_kw_to_item(&(*items)[*n_items - 1], &entries[i], calc_priority(&entries[i], prefix));
        }
    }
}

/*
 * @brief 在文档文本中搜索指定表的字段定义
 * 扫描形如 "t.field = value"、"t:method()" 和 "t = {field = ...}" 的模式
 * @param doc 文档指针
 * @param table_name 表名
 * @param prefix 过滤前缀
 * @param items 输出补全项数组
 * @param n_items 输出-已添加数量
 */
static void search_table_fields(LspDocument *doc, const char *table_name,
                                const char *prefix, LspCompletionItem **items, int *n_items) {
    if (!table_name || !doc->text) return;
    
    int tname_len = (int)strlen(table_name);
    const char *text = doc->text;
    int text_len = (int)doc->text_len;
    
    /* 方法1：扫描 "tablename.field" 模式 */
    for (int pos = 0; pos < text_len - tname_len - 2; pos++) {
        /* 检查表名匹配 */
        if (strncmp(text + pos, table_name, tname_len) != 0) continue;
        /* 后面的字符必须是 '.' 或 ':' */
        if (text[pos + tname_len] != '.' && text[pos + tname_len] != ':') continue;
        
        /* 提取字段名 */
        int field_start = pos + tname_len + 1;
        int field_end = field_start;
        while (field_end < text_len && 
               ((text[field_end] >= 'a' && text[field_end] <= 'z') ||
                (text[field_end] >= 'A' && text[field_end] <= 'Z') ||
                (text[field_end] >= '0' && text[field_end] <= '9') ||
                text[field_end] == '_')) {
            field_end++;
        }
        
        if (field_end <= field_start) continue;
        
        int field_len = field_end - field_start;
        char field_name[128];
        if (field_len >= 128) field_len = 127;
        memcpy(field_name, text + field_start, field_len);
        field_name[field_len] = '\0';
        
        /* 前缀过滤 */
        if (prefix && *prefix) {
            if (strncmp(field_name, prefix, strlen(prefix)) != 0) continue;
        }
        
        /* 去重 */
        int dup = 0;
        for (int i = 0; i < *n_items; i++) {
            if ((*items)[i].label && strcmp((*items)[i].label, field_name) == 0) {
                dup = 1; break;
            }
        }
        if (dup) continue;
        
        if (*n_items >= MAX_COMPLETION_ITEMS) break;
        
        /* 提取字段值（用于detail） */
        int after_field = field_end;
        while (after_field < text_len && 
               (text[after_field] == ' ' || text[after_field] == '\t')) after_field++;
        
        char detail[256] = "field";
        if (after_field < text_len && text[after_field] == '=') {
            after_field++;
            while (after_field < text_len && 
                   (text[after_field] == ' ' || text[after_field] == '\t')) after_field++;
            int val_end = after_field;
            while (val_end < text_len && text[val_end] != ',' && 
                   text[val_end] != '\n' && text[val_end] != '\r') val_end++;
            int vlen = val_end - after_field;
            if (vlen > 0 && vlen < 200) {
                snprintf(detail, sizeof(detail), "field = %.*s", vlen, text + after_field);
            }
        }
        
        (*n_items)++;
        *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
        LspCompletionItem *item = &(*items)[*n_items - 1];
        item->label = lsp_strdup(field_name);
        item->kind = (text[pos + tname_len] == ':') ? COMPLETION_METHOD : COMPLETION_FIELD;
        item->detail = lsp_strdup(detail);
        item->documentation = NULL;
        item->insert_text = lsp_strdup(field_name);
        item->insert_text_format = INSERT_TEXT_PLAIN;
        item->sort_text_priority = 50;  /* 高优先级，排在前列 */
    }
    
    /* 方法2：扫描表初始化器 "tablename = {field = value, ...}" */
    for (int pos = 0; pos < text_len - tname_len - 4; pos++) {
        if (strncmp(text + pos, table_name, tname_len) != 0) continue;
        
        /* 跳过空格 */
        int eq_pos = pos + tname_len;
        while (eq_pos < text_len && (text[eq_pos] == ' ' || text[eq_pos] == '\t')) eq_pos++;
        if (eq_pos >= text_len || text[eq_pos] != '=') continue;
        eq_pos++;
        while (eq_pos < text_len && (text[eq_pos] == ' ' || text[eq_pos] == '\t')) eq_pos++;
        if (eq_pos >= text_len || text[eq_pos] != '{') continue;
        
        /* 在 {...} 中提取字段名 */
        int brace_start = eq_pos + 1;
        int brace_depth = 1;
        int field = brace_start;
        while (field < text_len && brace_depth > 0) {
            /* 跳过空白 */
            while (field < text_len && 
                   (text[field] == ' ' || text[field] == '\t' || 
                    text[field] == '\n' || text[field] == '\r')) field++;
            
            if (field >= text_len) break;
            if (text[field] == '}') { brace_depth--; field++; continue; }
            if (text[field] == '{') { brace_depth++; field++; continue; }
            if (text[field] == ',') { field++; continue; }
            
            /* 提取可能的字段名 */
            int fname_start = field;
            int fname_end = field;
            while (fname_end < text_len &&
                   ((text[fname_end] >= 'a' && text[fname_end] <= 'z') ||
                    (text[fname_end] >= 'A' && text[fname_end] <= 'Z') ||
                    (text[fname_end] >= '0' && text[fname_end] <= '9') ||
                    text[fname_end] == '_')) {
                fname_end++;
            }
            
            if (fname_end > fname_start) {
                int fn_len = fname_end - fname_start;
                char fn[128];
                if (fn_len >= 128) fn_len = 127;
                memcpy(fn, text + fname_start, fn_len);
                fn[fn_len] = '\0';
                
                /* 检查是否真的是字段定义（后面有 =） */
                int check = fname_end;
                while (check < text_len && 
                       (text[check] == ' ' || text[check] == '\t')) check++;
                if (check < text_len && text[check] == '=') {
                    /* 前缀过滤 */
                    if (prefix && *prefix) {
                        if (strncmp(fn, prefix, strlen(prefix)) != 0) {
                            field = fname_end;
                            continue;
                        }
                    }
                    
                    /* 去重 */
                    int dup = 0;
                    for (int i = 0; i < *n_items; i++) {
                        if ((*items)[i].label && strcmp((*items)[i].label, fn) == 0) {
                            dup = 1; break;
                        }
                    }
                    if (!dup && *n_items < MAX_COMPLETION_ITEMS) {
                        /* 提取值 */
                        int vstart = check + 1;
                        while (vstart < text_len && 
                               (text[vstart] == ' ' || text[vstart] == '\t')) vstart++;
                        int vend = vstart;
                        while (vend < text_len && text[vend] != ',' && 
                               text[vend] != '\n' && text[vend] != '\r' && text[vend] != '}') vend++;
                        char detail[256];
                        int vlen = vend - vstart;
                        if (vlen > 0 && vlen < 200) {
                            snprintf(detail, sizeof(detail), "field = %.*s", vlen, text + vstart);
                        } else {
                            snprintf(detail, sizeof(detail), "field");
                        }
                        
                        (*n_items)++;
                        *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
                        LspCompletionItem *item = &(*items)[*n_items - 1];
                        item->label = lsp_strdup(fn);
                        item->kind = COMPLETION_FIELD;
                        item->detail = lsp_strdup(detail);
                        item->documentation = NULL;
                        item->insert_text = lsp_strdup(fn);
                        item->insert_text_format = INSERT_TEXT_PLAIN;
                        item->sort_text_priority = 50;
                    }
                }
                field = fname_end;
            } else {
                field++;
            }
        }
        /* 只处理第一个匹配的表初始化器 */
        break;
    }
}

/*
 * @brief 搜索类/结构体定义中的成员字段和方法
 * 扫描 class/struct ClassName ... end 块中的字段和方法定义
 * @param doc 文档指针
 * @param class_name 类名
 * @param prefix 过滤前缀
 * @param items 输出补全项数组
 * @param n_items 输出-已添加数量
 * @param is_method 是否搜索方法（:访问）还是字段（.访问）
 */
static void search_class_members(LspDocument *doc, const char *class_name,
                                 const char *prefix, LspCompletionItem **items, int *n_items, int is_method) {
    if (!class_name || !doc->tokens || doc->ntokens == 0) return;
    
    int cn_len = (int)strlen(class_name);
    
    /* 查找 class/struct ClassName 声明 */
    for (int i = 0; i < doc->ntokens - 1; i++) {
        if ((doc->tokens[i].type == TOK_CLASS || doc->tokens[i].type == TOK_STRUCT || doc->tokens[i].type == TOK_INTERFACE || doc->tokens[i].type == TOK_TRAIT) &&
            doc->tokens[i+1].type == TOK_NAME &&
            strcmp(doc->tokens[i+1].text, class_name) == 0) {
            
            /* 找到对应的 end，确定类体范围 */
            int depth = 1;
            int body_start = i + 2;
            int body_end = doc->ntokens;
            for (int j = i + 2; j < doc->ntokens; j++) {
                if (doc->tokens[j].type == TOK_CLASS || doc->tokens[j].type == TOK_STRUCT ||
                    doc->tokens[j].type == TOK_INTERFACE || doc->tokens[j].type == TOK_TRAIT ||
                    doc->tokens[j].type == TOK_FUNCTION || doc->tokens[j].type == TOK_IF ||
                    doc->tokens[j].type == TOK_FOR || doc->tokens[j].type == TOK_WHILE ||
                    doc->tokens[j].type == TOK_DO || doc->tokens[j].type == TOK_SWITCH ||
                    doc->tokens[j].type == TOK_ENUM || doc->tokens[j].type == TOK_NAMESPACE ||
                    doc->tokens[j].type == TOK_MATCH) {
                    depth++;
                } else if (doc->tokens[j].type == TOK_END) {
                    depth--;
                    if (depth == 0) {
                        body_end = j;
                        break;
                    }
                }
            }
            
            /* 在类体中扫描成员 */
            for (int j = body_start; j < body_end && *n_items < MAX_COMPLETION_ITEMS; j++) {
                LspToken *tok = &doc->tokens[j];
                
                if (is_method) {
                    /* 搜索方法: methodName = function(...) 或 function methodName(...) */
                    if (tok->type == TOK_FUNCTION && j > 0 && doc->tokens[j-1].type == TOK_NAME) {
                        const char *mname = doc->tokens[j-1].text;
                        if (!prefix || !*prefix || strncmp(mname, prefix, strlen(prefix)) == 0) {
                            /* 去重 */
                            int dup = 0;
                            for (int k = 0; k < *n_items; k++) {
                                if ((*items)[k].label && strcmp((*items)[k].label, mname) == 0) { dup = 1; break; }
                            }
                            if (!dup) {
                                (*n_items)++;
                                *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
                                LspCompletionItem *item = &(*items)[*n_items - 1];
                                item->label = lsp_strdup(mname);
                                item->kind = COMPLETION_METHOD;
                                item->detail = lsp_fmt("method of %s", class_name);
                                item->documentation = NULL;
                                item->insert_text = lsp_strdup(mname);
                                item->insert_text_format = INSERT_TEXT_PLAIN;
                                item->sort_text_priority = 150;
                            }
                        }
                    }
                } else {
                    /* 搜索字段: fieldName = value 或 fieldName: type */
                    if (tok->type == TOK_NAME && j + 1 < body_end && j > 0) {
                        /* 跳过关键字 */
                        if (doc->tokens[j-1].type == TOK_FUNCTION || doc->tokens[j-1].type == TOK_LOCAL ||
                            doc->tokens[j-1].type == TOK_EXPORT || doc->tokens[j-1].type == TOK_GLOBAL) continue;
                        
                        int next = j + 1;
                        /* 字段定义: name = value 或 name: type */
                        if (doc->tokens[next].type == (LspTokenType)'=' || 
                            doc->tokens[next].type == (LspTokenType)':') {
                            if (!prefix || !*prefix || strncmp(tok->text, prefix, strlen(prefix)) == 0) {
                                int dup = 0;
                                for (int k = 0; k < *n_items; k++) {
                                    if ((*items)[k].label && strcmp((*items)[k].label, tok->text) == 0) { dup = 1; break; }
                                }
                                if (!dup) {
                                    const char *type_hint = NULL;
                                    if (doc->tokens[next].type == (LspTokenType)':' && 
                                        next + 1 < body_end && doc->tokens[next+1].type == TOK_NAME) {
                                        type_hint = doc->tokens[next+1].text;
                                    }
                                    (*n_items)++;
                                    *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
                                    LspCompletionItem *item = &(*items)[*n_items - 1];
                                    item->label = lsp_strdup(tok->text);
                                    item->kind = COMPLETION_FIELD;
                                    item->detail = type_hint ? lsp_fmt("field: %s", type_hint) : lsp_fmt("field of %s", class_name);
                                    item->documentation = NULL;
                                    item->insert_text = lsp_strdup(tok->text);
                                    item->insert_text_format = INSERT_TEXT_PLAIN;
                                    item->sort_text_priority = 140;
                                }
                            }
                        }
                    }
                }
            }
            break; /* 只处理第一个匹配的类定义 */
        }
    }
}

/*
 * @brief 为标准库模块提供字段补全（如 string.xxx, table.xxx, os.xxx 等）
 * @param mod_name 模块名
 * @param prefix 过滤前缀
 * @param items 输出补全项数组
 * @param n_items 输出-已添加数量
 */
static void add_library_fields(const char *mod_name, const char *prefix,
                               LspCompletionItem **items, int *n_items) {
    if (!mod_name) return;
    
    LspKeywordEntry *stdlib;
    int nstdlib = lsp_kwdb_get_stdlib(&stdlib);
    
    int mod_len = (int)strlen(mod_name);
    for (int i = 0; i < nstdlib && stdlib[i].name && *n_items < MAX_COMPLETION_ITEMS; i++) {
        /* 检查是否是 "mod_name.something" 格式 */
        if (strncmp(stdlib[i].name, mod_name, mod_len) != 0) continue;
        if (stdlib[i].name[mod_len] != '.') continue;
        
        const char *field = stdlib[i].name + mod_len + 1;
        if (!*field) continue;
        
        /* 前缀过滤 */
        if (prefix && *prefix) {
            if (!strn_prefix(field, prefix, (int)strlen(prefix))) continue;
        }
        
        /* 去重 */
        int dup = 0;
        for (int j = 0; j < *n_items; j++) {
            if ((*items)[j].label && strcmp((*items)[j].label, field) == 0) {
                dup = 1; break;
            }
        }
        if (dup) continue;
        
        (*n_items)++;
        *items = lsp_realloc(*items, (*n_items) * sizeof(LspCompletionItem));
        LspCompletionItem *item = &(*items)[*n_items - 1];
        item->label = lsp_strdup(field);
        item->kind = COMPLETION_FUNCTION;
        item->detail = lsp_strdup(stdlib[i].detail ? stdlib[i].detail : mod_name);
        item->documentation = lsp_strdup(stdlib[i].documentation ? stdlib[i].documentation : "");
        item->insert_text = lsp_strdup(field);
        item->insert_text_format = INSERT_TEXT_PLAIN;
        item->sort_text_priority = 100;
    }
}

/*
 * @brief 生成代码补全结果
 * 根据光标所在的上下文（表字段/方法调用/局部变量声明/全局表达式）提供不同的补全建议
 * @param doc 文档指针
 * @param line 光标行号（0为起始）
 * @param col 光标列号（0为起始）
 * @param items 输出-补全项数组
 * @return 补全项数量
 */
int lsp_completion(LspDocument *doc, int line, int col, LspCompletionItem **items) {
    *items = NULL;
    int n_items = 0;
    
    if (!doc) return 0;
    
    char *prefix = NULL;
    const char *context = "global";
    char *table_name = NULL;
    analyze_context(doc, line, col, &prefix, &context, &table_name);
    
    /* 注释中不补全 */
    if (strcmp(context, "comment") == 0) { lsp_free(prefix); lsp_free(table_name); return 0; }
    
    /* 字符串中不补全（除非是做模块路径补全） */
    if (strcmp(context, "string") == 0) { lsp_free(prefix); lsp_free(table_name); return 0; }
    
    /* 根据上下文添加不同的补全项 */
    if (strcmp(context, "table_field") == 0 || strcmp(context, "method_call") == 0) {
        int has_prefix = (prefix && *prefix);
        int is_method = (strcmp(context, "method_call") == 0);
        
        /* 1. 如果知道表名，扫描文档找该表的字段定义 */
        if (table_name && doc->text && doc->text_len > 0) {
            search_table_fields(doc, table_name, prefix, items, &n_items);
            /* 同时搜索类/结构体成员 */
            search_class_members(doc, table_name, prefix, items, &n_items, is_method);
        }
        
        /* 2. 标准库函数（始终提供） */
        LspKeywordEntry *stdlib;
        int nstdlib = lsp_kwdb_get_stdlib(&stdlib);
        add_matching(stdlib, nstdlib, prefix, items, &n_items);
        
        /* 3. 如果表名匹配内置库，提供该库的成员 */
        if (table_name) {
            add_library_fields(table_name, prefix, items, &n_items);
        }
    } else if (strcmp(context, "local") == 0) {
        /* 在local声明后，提供类型提示和变量名 */
        LspKeywordEntry *keywords;
        int nkw = lsp_kwdb_get_keywords(&keywords);
        /* 提供类型关键字 */
        const char *type_names[] = {"bool","char","double","float","int","long","void","struct","enum","class","namespace",NULL};
        for (int i = 0; i < nkw && keywords[i].name; i++) {
            for (int t = 0; type_names[t]; t++) {
                if (strcmp(keywords[i].name, type_names[t]) == 0) {
                    if (!prefix || !*prefix || strncmp(keywords[i].name, prefix, strlen(prefix)) == 0) {
                        n_items++;
                        *items = lsp_realloc(*items, n_items * sizeof(LspCompletionItem));
                        copy_kw_to_item(&(*items)[n_items - 1], &keywords[i], 100);
                    }
                }
            }
        }
        /* 也提供当前作用域中的变量名（用于 local x = existing_var） */
        for (int i = 0; i < doc->nvars && n_items < MAX_COMPLETION_ITEMS; i++) {
            if (!prefix || !*prefix || strn_prefix(doc->vars[i].name, prefix, (int)strlen(prefix))) {
                n_items++;
                *items = lsp_realloc(*items, n_items * sizeof(LspCompletionItem));
                LspCompletionItem *item = &(*items)[n_items - 1];
                item->label = lsp_strdup(doc->vars[i].name);
                item->kind = doc->vars[i].kind == SYMBOL_FUNCTION ? COMPLETION_FUNCTION : COMPLETION_VARIABLE;
                item->detail = lsp_strdup("local variable");
                item->documentation = lsp_strdup(doc->vars[i].type_hint ? doc->vars[i].type_hint : "");
                item->insert_text = lsp_strdup(doc->vars[i].name);
                item->insert_text_format = INSERT_TEXT_PLAIN;
                item->sort_text_priority = 200;
            }
        }
    } else {
        /* 全局/表达式上下文中提供所有补全 */
        int has_prefix = (prefix && *prefix);
        /* 1. 局部变量 */
        for (int i = 0; i < doc->nvars && n_items < MAX_COMPLETION_ITEMS; i++) {
            if (!has_prefix || strn_prefix(doc->vars[i].name, prefix, strlen(prefix))) {
                n_items++;
                *items = lsp_realloc(*items, n_items * sizeof(LspCompletionItem));
                LspCompletionItem *item = &(*items)[n_items - 1];
                item->label = lsp_strdup(doc->vars[i].name);
                item->kind = doc->vars[i].kind == SYMBOL_FUNCTION ? COMPLETION_FUNCTION : COMPLETION_VARIABLE;
                item->detail = lsp_strdup("local variable");
                item->documentation = lsp_strdup(doc->vars[i].type_hint ? doc->vars[i].type_hint : "");
                item->insert_text = lsp_strdup(doc->vars[i].name);
                item->insert_text_format = INSERT_TEXT_PLAIN;
                item->sort_text_priority = 200;
            }
        }
        /* 2. 关键字（始终包含） */
        {
            LspKeywordEntry *keywords;
            int nkw = lsp_kwdb_get_keywords(&keywords);
            add_matching(keywords, nkw, prefix, items, &n_items);
        }
        /* 3. 内置函数（始终包含） */
        {
            LspKeywordEntry *builtins;
            int nbuiltins = lsp_kwdb_get_builtins(&builtins);
            add_matching(builtins, nbuiltins, prefix, items, &n_items);
        }
        /* 4. 标准库（仅当前缀非空时加入，避免空前缀下返回过多条目） */
        if (has_prefix) {
            LspKeywordEntry *stdlib;
            int nstdlib = lsp_kwdb_get_stdlib(&stdlib);
            add_matching(stdlib, nstdlib, prefix, items, &n_items);
        }
        /* 5. 代码片段（仅当有前缀匹配时） */
        if (has_prefix) {
            LspKeywordEntry *snippets;
            int nsnippets = lsp_kwdb_get_snippets(&snippets);
            add_matching(snippets, nsnippets, prefix, items, &n_items);
        } else {
            /* 无前缀时只展示精选的5个最常用片段 */
            LspKeywordEntry *snippets;
            int nsnippets = lsp_kwdb_get_snippets(&snippets);
            const char *top_snippets[] = {"function","if","fori","forp","class","match",NULL};
            for (int i = 0; i < nsnippets && snippets[i].name && n_items < MAX_COMPLETION_ITEMS; i++) {
                for (int t = 0; top_snippets[t]; t++) {
                    if (strcmp(snippets[i].name, top_snippets[t]) == 0) {
                        n_items++;
                        *items = lsp_realloc(*items, n_items * sizeof(LspCompletionItem));
                        copy_kw_to_item(&(*items)[n_items - 1], &snippets[i], 100);
                    }
                }
            }
        }
    }
    
    lsp_free(prefix);
    lsp_free(table_name);
    return n_items;
}