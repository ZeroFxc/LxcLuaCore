/*
** LXCLUA LSP - Hover Provider
** Provides documentation and type information on hover.
*/

#include <stdlib.h>
#include <string.h>
#include "lspsrv.h"

/* External declarations */
extern const char *lsp_kwdb_find_doc(const char *name);
extern char *lsp_fmt(const char *fmt, ...);
extern char *lsp_strdup(const char *s);
extern void lsp_free(void *p);
extern char *lsp_get_word_at(const char *text, int offset, int *start, int *end);
extern int lsp_linecol_to_offset(const char *text, int line, int col);

/*
 * @brief 生成悬停提示信息（Markdown格式）
 * @param doc 文档指针
 * @param line 光标行号（0为起始）
 * @param col 光标列号（0为起始）
 * @return Markdown格式的悬停文本，NULL表示无可用信息
 */
char *lsp_hover(LspDocument *doc, int line, int col) {
    if (!doc) return NULL;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return NULL; }
    
    char *result = NULL;
    
    /* 检查局部变量 */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0) {
            const char *kind_str = "variable";
            switch (doc->vars[i].kind) {
                case SYMBOL_FUNCTION: kind_str = "function"; break;
                case SYMBOL_CLASS: kind_str = "class"; break;
                case SYMBOL_STRUCT: kind_str = "struct"; break;
                case SYMBOL_ENUM: kind_str = "enum"; break;
                case SYMBOL_NAMESPACE: kind_str = "namespace"; break;
                case SYMBOL_CONSTANT: kind_str = "constant"; break;
                default: kind_str = "variable"; break;
            }
            if (doc->vars[i].type_hint) {
                result = lsp_fmt("```lua\nlocal %s: %s -- %s\n```\n\n%s",
                    doc->vars[i].name, doc->vars[i].type_hint, kind_str,
                    doc->vars[i].def_line == line ? "Defined here" : "");
            } else {
                result = lsp_fmt("```lua\n%s (%s)\n```\n\nDefined at line %d",
                    doc->vars[i].name, kind_str, doc->vars[i].def_line + 1);
            }
            lsp_free(word);
            return result;
        }
    }
    
    /* 检查关键词文档 */
    const char *doc_str = lsp_kwdb_find_doc(word);
    if (doc_str) {
        result = lsp_fmt("```lua\n%s\n```\n\n%s", word, doc_str);
        lsp_free(word);
        return result;
    }
    
    /* 搜索字符串库函数: string.xxx */
    {
        int wlen = (int)strlen(word);
        /* Check if word is a table-like access */
        char *dot = strchr(word, '.');
        char *colon = strchr(word, ':');
        if (dot || colon) {
            const char *search_name = word;
            const char *doc_found = lsp_kwdb_find_doc(search_name);
            if (doc_found) {
                result = lsp_fmt("```lua\n%s\n```\n\n%s", search_name, doc_found);
                lsp_free(word);
                return result;
            }
        }
    }
    
    lsp_free(word);
    return NULL;
}

/*
** LXCLUA LSP - Go-to-Definition Provider
*/

/*
 * @brief 查找光标所在符号的定义位置
 * @param doc 文档指针
 * @param line 光标行号（0为起始）
 * @param col 光标列号（0为起始）
 * @param def_line 输出-定义行号
 * @param def_col 输出-定义列号
 * @param def_uri 输出-定义所在的文件URI
 * @return 符号名称，NULL表示未找到
 */
char *lsp_get_symbol_at(LspDocument *doc, int line, int col, int *def_line, int *def_col, char **def_uri) {
    if (!doc) return NULL;
    *def_line = -1; *def_col = -1; *def_uri = NULL;
    
    int offset = lsp_linecol_to_offset(doc->text, line, col);
    int word_start, word_end;
    char *word = lsp_get_word_at(doc->text, offset, &word_start, &word_end);
    if (!word || !*word) { lsp_free(word); return NULL; }
    
    /* 查找局部变量定义 */
    for (int i = 0; i < doc->nvars; i++) {
        if (strcmp(doc->vars[i].name, word) == 0) {
            *def_line = doc->vars[i].def_line;
            *def_col = doc->vars[i].def_col;
            *def_uri = lsp_strdup(doc->uri);
            /* 但如果当前就在定义处，则返回已找到 */
            if (*def_line == line && *def_col == col) {
                /* Already at definition */
            }
            char *name = lsp_strdup(word);
            lsp_free(word);
            return name;
        }
    }
    
    /* 查找import/require - 如果找到，def_uri指向对应模块 */
    for (int i = 0; i < doc->nimports; i++) {
        if (strcmp(doc->imports[i], word) == 0) {
            /* 尝试找到该模块的require行 */
            if (doc->tokens && doc->ntokens > 0) {
                for (int j = 0; j < doc->ntokens; j++) {
                    /* Find 'require' token */
                    if (doc->tokens[j].type == TOK_NAME && 
                        strcmp(doc->tokens[j].text, "require") == 0) {
                        *def_line = doc->tokens[j].line;
                        *def_col = doc->tokens[j].col;
                        *def_uri = lsp_strdup(doc->uri);
                        char *name = lsp_strdup(word);
                        lsp_free(word);
                        return name;
                    }
                }
            }
            break;
        }
    }
    
    lsp_free(word);
    return NULL;
}