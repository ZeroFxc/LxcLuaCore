/*
** LXCLUA LSP - Code Completion Provider
** Provides intelligent code completion suggestions based on context.
*/

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
                            char **prefix, const char **context) {
    *prefix = NULL;
    *context = "global";
    
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
        /* 如果前面是字符串或名称，这是table字段补全 */
        return;
    }
    if (best_idx >= 1 && tokens[best_idx].type == (LspTokenType)':') {
        *context = "method_call";
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
 * @brief 生成代码补全结果
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
    analyze_context(doc, line, col, &prefix, &context);
    
    /* 注释中不补全 */
    if (strcmp(context, "comment") == 0) { lsp_free(prefix); return 0; }
    
    /* 字符串中不补全（除非是做模块路径补全） */
    if (strcmp(context, "string") == 0) { lsp_free(prefix); return 0; }
    
    /* 根据上下文添加不同的补全项 */
    if (strcmp(context, "table_field") == 0 || strcmp(context, "method_call") == 0) {
        /* 在table.field上下文中，提供当前作用域中的table成员 */
        /* 简化实现：提供标准库函数 */
        LspKeywordEntry *stdlib;
        int nstdlib = lsp_kwdb_get_stdlib(&stdlib);
        add_matching(stdlib, nstdlib, prefix, items, &n_items);
    } else if (strcmp(context, "local") == 0) {
        /* 在local声明后，提供类型提示和变量名 */
        LspKeywordEntry *keywords;
        int nkw = lsp_kwdb_get_keywords(&keywords);
        /* Filter: only type-related keywords */
        const char *type_names[] = {"bool","char","double","float","int","long","void","struct","enum","class",NULL};
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
            const char *top_snippets[] = {"function","if","fori","forp","class",NULL};
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
    return n_items;
}