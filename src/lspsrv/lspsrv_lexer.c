/*
** LXCLUA LSP - Standalone Lexer
** Tokenizes LXCLUA source code into LSP tokens.
** Operates independently of the main Lua VM.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "lspsrv.h"

/* ---- Keyword Table ---- */

typedef struct {
    const char *word;
    LspTokenType token;
} LspKeyword;

static LspKeyword lsp_keywords[] = {
    {"and",       TOK_AND},       {"asm",       TOK_ASM},
    {"async",     TOK_ASYNC},     {"await",     TOK_AWAIT},
    {"bool",      TOK_BOOL},      {"break",     TOK_BREAK},
    {"case",      TOK_CASE},      {"catch",     TOK_CATCH},
    {"char",      TOK_CHAR},      {"command",   TOK_COMMAND},
    {"concept",   TOK_CONCEPT},   {"const",     TOK_CONST},
    {"continue",  TOK_CONTINUE},  {"default",   TOK_DEFAULT},
    {"defer",     TOK_DEFER},     {"do",        TOK_DO},
    {"double",    TOK_DOUBLE},    {"else",      TOK_ELSE},
    {"elseif",    TOK_ELSEIF},    {"end",       TOK_END},
    {"enum",      TOK_ENUM},      {"export",    TOK_EXPORT},
    {"false",     TOK_FALSE},     {"finally",   TOK_FINALLY},
    {"float",     TOK_TYPE_FLOAT},     {"for",       TOK_FOR},
    {"function",  TOK_FUNCTION},  {"global",    TOK_GLOBAL},
    {"goto",      TOK_GOTO},      {"if",        TOK_IF},
    {"in",        TOK_IN},        {"int",       TOK_TYPE_INT},
    {"is",        TOK_IS},        {"instanceof",TOK_INSTANCEOF},
    {"keyword",   TOK_KEYWORD},   {"lambda",    TOK_LAMBDA},
    {"local",     TOK_LOCAL},     {"long",      TOK_LONG},
    {"namespace", TOK_NAMESPACE}, {"nil",       TOK_NIL},
    {"not",       TOK_NOT},       {"operator",  TOK_OPERATOR},
    {"or",        TOK_OR},        {"repeat",    TOK_REPEAT},
    {"requires",  TOK_REQUIRES},  {"return",    TOK_RETURN},
    {"struct",    TOK_STRUCT},    {"superstruct",TOK_SUPERSTRUCT},
    {"switch",    TOK_SWITCH},    {"take",      TOK_TAKE},
    {"then",      TOK_THEN},      {"true",      TOK_TRUE},
    {"try",       TOK_TRY},       {"until",     TOK_UNTIL},
    {"using",     TOK_USING},     {"void",      TOK_VOID},
    {"when",      TOK_WHEN},      {"while",     TOK_WHILE},
    {"with",      TOK_WITH},      {"let",       TOK_LET},
    {NULL,        0}
};

/** Lexer state */
typedef struct {
    const char *src;
    int len;
    int pos;
    int line;
    int col;
    int linestart;   /**< offset of current line start */
    LspToken *tokens;
    int ntokens;
    int token_cap;
} LspLexer;

/* Token buffer management */

/*
 * @brief 添加一个token到token列表
 * @param lex 词法分析器
 * @param type token类型
 * @param text token文本
 * @param len 文本长度
 * @param offset token起始偏移量
 * @return 新添加的token指针
 */
static LspToken *lex_add_token(LspLexer *lex, LspTokenType type, const char *text, int len, int offset) {
    if (lex->ntokens >= lex->token_cap) {
        lex->token_cap = lex->token_cap ? lex->token_cap * 2 : 256;
        lex->tokens = lsp_realloc(lex->tokens, lex->token_cap * sizeof(LspToken));
    }
    LspToken *tok = &lex->tokens[lex->ntokens++];
    tok->type = type;
    tok->line = lex->line;
    tok->col = offset - lex->linestart;
    tok->offset = offset;
    tok->len = len;
    tok->text = (char *)lsp_alloc(len + 1);
    memcpy(tok->text, text, len);
    tok->text[len] = 0;
    tok->num.fval = 0;
    tok->num.ival = 0;
    return tok;
}

/* Character peeking */

/*
 * @brief 查看当前位置的字符，不前进
 * @param lex 词法分析器
 * @return 字符，EOF返回-1
 */
static int lex_peek(LspLexer *lex) {
    return lex->pos < lex->len ? (unsigned char)lex->src[lex->pos] : -1;
}

/*
 * @brief 消耗当前位置的字符（前进一位）
 * @param lex 词法分析器
 * @return 消耗的字符
 */
static int lex_advance(LspLexer *lex) {
    if (lex->pos >= lex->len) return -1;
    int c = (unsigned char)lex->src[lex->pos++];
    lex->col++;
    if (c == '\n') {
        lex->line++;
        lex->col = 0;
        lex->linestart = lex->pos;
    } else if (c == '\r' && lex_peek(lex) == '\n') {
        lex->pos++; /* consume LF */
        lex->line++;
        lex->col = 0;
        lex->linestart = lex->pos;
    }
    return c;
}

/*
 * @brief 检查字符是否为十六进制数字
 * @param c 字符
 * @return 1是，0否
 */
static int is_hex(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/*
 * @brief 检查字符是否为数字
 * @param c 字符
 * @return 1是，0否
 */
static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

/*
 * @brief 词法分析-读取标识符或关键字
 * @param lex 词法分析器
 * @param start 起始偏移量
 */
static void lex_identifier(LspLexer *lex, int start) {
    int s = start;
    while (lex->pos < lex->len) {
        int c = lex_peek(lex);
        if (lsp_is_ident_char(c)) {
            lex_advance(lex);
        } else {
            break;
        }
    }
    int len = lex->pos - s;
    const char *word = lex->src + s;
    
    /* 查找是否为关键字 */
    for (int i = 0; lsp_keywords[i].word; i++) {
        if (strlen(lsp_keywords[i].word) == (size_t)len && 
            strncmp(word, lsp_keywords[i].word, len) == 0) {
            lex_add_token(lex, lsp_keywords[i].token, word, len, s);
            return;
        }
    }
    /* 否则是普通标识符 */
    lex_add_token(lex, TOK_NAME, word, len, s);
}

/*
 * @brief 词法分析-读取数字（整数或浮点数）
 * @param lex 词法分析器
 * @param start 起始偏移量
 */
static void lex_number(LspLexer *lex, int start) {
    int is_float = 0;
    int is_hex_literal = 0;
    
    /* 处理0x/0X十六进制前缀 */
    if (lex_peek(lex) == '0') {
        lex_advance(lex);
        int n = lex_peek(lex);
        if (n == 'x' || n == 'X') {
            is_hex_literal = 1;
            lex_advance(lex);
            while (lex->pos < lex->len && is_hex(lex_peek(lex)))
                lex_advance(lex);
            /* 十六进制浮点数: 0x1.2p3 */
            if (lex_peek(lex) == '.') {
                is_float = 1;
                lex_advance(lex);
                while (lex->pos < lex->len && is_hex(lex_peek(lex)))
                    lex_advance(lex);
            }
            if (lex_peek(lex) == 'p' || lex_peek(lex) == 'P') {
                is_float = 1;
                lex_advance(lex);
                if (lex_peek(lex) == '+' || lex_peek(lex) == '-')
                    lex_advance(lex);
                while (lex->pos < lex->len && is_digit(lex_peek(lex)))
                    lex_advance(lex);
            }
            goto done_num;
        }
    }
    
    /* 小数部分 */
    while (lex->pos < lex->len && is_digit(lex_peek(lex)))
        lex_advance(lex);
    
    /* 浮点数：.  或 e/E */
    if (lex_peek(lex) == '.') {
        is_float = 1;
        lex_advance(lex);
        while (lex->pos < lex->len && is_digit(lex_peek(lex)))
            lex_advance(lex);
    }
    if (lex_peek(lex) == 'e' || lex_peek(lex) == 'E') {
        is_float = 1;
        lex_advance(lex);
        if (lex_peek(lex) == '+' || lex_peek(lex) == '-')
            lex_advance(lex);
        while (lex->pos < lex->len && is_digit(lex_peek(lex)))
            lex_advance(lex);
    }
    
done_num:
    int len = lex->pos - start;
    const char *num_text = lex->src + start;
    LspToken *tok = lex_add_token(lex, is_float ? TOK_FLT : TOK_INT, num_text, len, start);
    /* 解析数值 */
    char *temp = (char *)lsp_alloc(len + 1);
    memcpy(temp, num_text, len);
    temp[len] = 0;
    if (is_float) {
        tok->num.fval = strtod(temp, NULL);
    } else {
        tok->num.ival = (int64_t)strtoll(temp, NULL, 0);
    }
    lsp_free(temp);
}

/*
 * @brief 词法分析-读取字符串（单引号或双引号）
 * @param lex 词法分析器
 * @param start 起始偏移量（含引号）
 * @param quote 引号字符
 */
static void lex_string(LspLexer *lex, int start, int quote) {
    while (lex->pos < lex->len) {
        int c = lex_advance(lex);
        if (c == quote) break;
        if (c == '\\') {
            /* 转义字符 */
            int n = lex_peek(lex);
            if (n == 'z') {
                /* 跳过空白 */
                lex_advance(lex);
                while (lex->pos < lex->len) {
                    int w = lex_peek(lex);
                    if (w == ' ' || w == '\t' || w == '\n' || w == '\r')
                        lex_advance(lex);
                    else break;
                }
            } else {
                lex_advance(lex); /* 跳过转义字符 */
            }
        }
    }
    int len = lex->pos - start;
    lex_add_token(lex, TOK_STRING, lex->src + start, len, start);
}

/*
 * @brief 词法分析-读取长字符串 [[...]]
 * @param lex 词法分析器
 * @param start 起始偏移量（含[[）
 * @param eq_count =号数量（0=[[，1=[=[，等）
 */
static void lex_long_string(LspLexer *lex, int start, int eq_count) {
    while (lex->pos < lex->len) {
        int c = lex_advance(lex);
        if (c == ']') {
            /* 检查是否有匹配的]=*] */
            int match = 1;
            for (int i = 0; i < eq_count; i++) {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                } else {
                    match = 0;
                    break;
                }
            }
            if (match && lex_peek(lex) == ']') {
                lex_advance(lex);
                break;
            }
        }
    }
    int len = lex->pos - start;
    lex_add_token(lex, TOK_STRING, lex->src + start, len, start);
}

/*
 * @brief 词法分析-读取注释
 * @param lex 词法分析器
 * @param start 起始偏移量
 * @param is_block 是否块注释（--[[）还是行注释（--）
 */
static void lex_comment(LspLexer *lex, int start, int is_block) {
    if (is_block) {
        /* 已经是 [[，需要找到匹配的 ]] */
        int eq_count = 0;
        while (lex_peek(lex) == '=') {
            eq_count++;
            lex_advance(lex);
        }
        if (lex_peek(lex) == '[') lex_advance(lex);
        while (lex->pos < lex->len) {
            int c = lex_advance(lex);
            if (c == ']') {
                int match = 1;
                for (int i = 0; i < eq_count; i++) {
                    if (lex_peek(lex) == '=') lex_advance(lex);
                    else { match = 0; break; }
                }
                if (match && lex_peek(lex) == ']') {
                    lex_advance(lex);
                    break;
                }
            }
        }
    } else {
        while (lex->pos < lex->len) {
            int c = lex_peek(lex);
            if (c == '\n' || c == '\r') break;
            lex_advance(lex);
        }
    }
    /* We add comment tokens but they're mainly for tracking */
    int len = lex->pos - start;
    lex_add_token(lex, is_block ? TOK_MCOMMENT : TOK_COMMENT, lex->src + start, len, start);
}

/*
 * @brief 完整的词法分析函数
 * @param src 源文本
 * @param len 文本长度
 * @param out_tokens 输出-token数组
 * @param out_ntokens 输出-token数量
 */
void lsp_lex(const char *src, int len, LspToken **out_tokens, int *out_ntokens) {
    LspLexer lex_state;
    LspLexer *lex = &lex_state;
    memset(lex, 0, sizeof(LspLexer));
    lex->src = src;
    lex->len = len;
    lex->pos = 0;
    lex->line = 0;
    lex->col = 0;
    lex->linestart = 0;
    lex->tokens = NULL;
    lex->ntokens = 0;
    lex->token_cap = 0;
    
    while (lex->pos < lex->len) {
        int start = lex->pos;
        int c = lex_peek(lex);
        if (is_digit(c)) {
            lex_number(lex, start);
            continue;
        }
        lex_advance(lex);
        
        switch (c) {
            /* 空白 */
            case ' ': case '\t': case '\n': case '\r': case '\f': case '\v':
                break;
                
            /* 注释 */
            case '-': {
                if (lex_peek(lex) == '-') {
                    int s2 = lex->pos - 1; /* start of -- */
                    lex_advance(lex);
                    if (lex_peek(lex) == '[') {
                        lex_advance(lex);
                        if (lex_peek(lex) == '[') {
                            lex_advance(lex);
                            lex_comment(lex, s2, 1);
                        } else {
                            /* --[ 不是块注释，回退为普通注释 */
                            lex_comment(lex, s2, 0);
                        }
                    } else {
                        lex_comment(lex, s2, 0);
                    }
                } else {
                    lex_add_token(lex, (LspTokenType)'-', "-", 1, start);
                }
                break;
            }
            
            /* 字符串 */
            case 'r': case 'R': {
                int n = lex_peek(lex);
                if (n == '"' || n == '\'') {
                    int q = lex_advance(lex);
                    lex_string(lex, lex->pos - 2, q);
                    if (lex->ntokens > 0) {
                        lex->tokens[lex->ntokens - 1].type = TOK_RAWSTRING;
                    }
                    break;
                }
                lex_identifier(lex, start);
                break;
            }
            case '"':  lex_string(lex, start, '"'); break;
            case '\'': lex_string(lex, start, '\''); break;
            case '`': {
                lex_string(lex, start, '`');
                if (lex->ntokens > 0) {
                    lex->tokens[lex->ntokens - 1].type = TOK_INTERPSTRING;
                }
                break;
            }
            
            /* 长字符串或表访问 */
            case '[': {
                if (lex_peek(lex) == '[' || lex_peek(lex) == '=') {
                    int eq = 0;
                    if (lex_peek(lex) == '=') {
                        while (lex_peek(lex) == '=') { eq++; lex_advance(lex); }
                    }
                    if (lex_peek(lex) == '[') {
                        lex_advance(lex);
                        lex_long_string(lex, start, eq);
                    } else {
                        lex_add_token(lex, (LspTokenType)'[', "[", 1, start);
                    }
                } else {
                    lex_add_token(lex, (LspTokenType)'[', "[", 1, start);
                }
                break;
            }
            
            /* 操作符 */
            case '.': {
                if (lex_peek(lex) == '.') {
                    lex_advance(lex);
                    if (lex_peek(lex) == '.') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_DOTS, "...", 3, start);
                    } else if (lex_peek(lex) == '=') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_CONCATEQ, "..=", 3, start);
                    } else {
                        lex_add_token(lex, TOK_CONCAT, "..", 2, start);
                    }
                } else {
                    lex_add_token(lex, (LspTokenType)'.', ".", 1, start);
                }
                break;
            }
            case '=': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_EQ, "==", 2, start);
                } else if (lex_peek(lex) == '>') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_ARROW, "=>", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'=', "=", 1, start);
                }
                break;
            }
            case '<': {
                int n = lex_peek(lex);
                if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_LE, "<=", 2, start);
                } else if (n == '\\') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_REVPIPE, "<\\", 2, start);
                } else if (n == '<') {
                    lex_advance(lex);
                    if (lex_peek(lex) == '=') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_SHLEQ, "<<=", 3, start);
                    } else {
                        lex_add_token(lex, TOK_SHL, "<<", 2, start);
                    }
                } else if (n == '=' && lex->pos+1 < lex->len && lex->src[lex->pos+1] == '>') {
                    lex_advance(lex); lex_advance(lex);
                    lex_add_token(lex, TOK_SPACESHIP, "<=>", 3, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'<', "<", 1, start);
                }
                break;
            }
            case '>': {
                int n = lex_peek(lex);
                if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_GE, ">=", 2, start);
                } else if (n == '>') {
                    lex_advance(lex);
                    if (lex_peek(lex) == '=') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_SHREQ, ">>=", 3, start);
                    } else {
                        lex_add_token(lex, TOK_SHR, ">>", 2, start);
                    }
                } else {
                    lex_add_token(lex, (LspTokenType)'>', ">", 1, start);
                }
                break;
            }
            case '~': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_NE, "~=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'~', "~", 1, start);
                }
                break;
            }
            case '/': {
                int n = lex_peek(lex);
                if (n == '/') {
                    lex_advance(lex);
                    if (lex_peek(lex) == '=') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_IDIVEQ, "//=", 3, start);
                    } else {
                        lex_add_token(lex, TOK_IDIV, "//", 2, start);
                    }
                } else if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_DIVEQ, "/=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'/', "/", 1, start);
                }
                break;
            }
            case ':': {
                int n = lex_peek(lex);
                if (n == ':') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_DBCOLON, "::", 2, start);
                } else if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_MEAN, ":=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)':', ":", 1, start);
                }
                break;
            }
            case '|': {
                int n = lex_peek(lex);
                if (n == '>') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_PIPE, "|>", 2, start);
                } else if (n == '?') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_SAFEPIPE, "|?", 2, start);
                } else if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_BOREQ, "|=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'|', "|", 1, start);
                }
                break;
            }
            case '&': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_BANDEQ, "&=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'&', "&", 1, start);
                }
                break;
            }
            case '+': {
                int n = lex_peek(lex);
                if (n == '+') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_PLUSPLUS, "++", 2, start);
                } else if (n == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_ADDEQ, "+=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'+', "+", 1, start);
                }
                break;
            }
            case '*': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_MULEQ, "*=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'*', "*", 1, start);
                }
                break;
            }
            case '%': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_MODEQ, "%=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'%', "%", 1, start);
                }
                break;
            }
            case '^': {
                if (lex_peek(lex) == '=') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_POWEQ, "^=", 2, start);
                } else {
                    lex_add_token(lex, (LspTokenType)'^', "^", 1, start);
                }
                break;
            }
            case '?': {
                int n = lex_peek(lex);
                if (n == '.') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_OPTCHAIN, "?.", 2, start);
                } else if (n == '?') {
                    lex_advance(lex);
                    if (lex_peek(lex) == '=') {
                        lex_advance(lex);
                        lex_add_token(lex, TOK_NULLCOALEQ, "?" "?" "=", 3, start);
                    } else {
                        lex_add_token(lex, TOK_NULLCOAL, "?" "?", 2, start);
                    }
                } else {
                    lex_add_token(lex, (LspTokenType)'?', "?", 1, start);
                }
                break;
            }
            case '$': {
                if (lex_peek(lex) == '$') {
                    lex_advance(lex);
                    lex_add_token(lex, TOK_DOLLDOLL, "$$", 2, start);
                } else {
                    lex_add_token(lex, TOK_DOLLAR, "$", 1, start);
                }
                break;
            }
            
            /* 其他单字符操作符 */
            case '(': case ')': case '{': case '}':
            case ',': case ';': case '#':
                lex_add_token(lex, (LspTokenType)c, lex->src + start, 1, start);
                break;
            
            default:
                /* 标识符 */
                if (lsp_is_ident_start(c)) {
                    lex_identifier(lex, start);
                }
                break;
        }
    }
    
    /* Add EOF token */
    lex_add_token(lex, TOK_EOS, "", 0, lex->pos);
    
    *out_tokens = lex->tokens;
    *out_ntokens = lex->ntokens;
}

/*
 * @brief 释放token数组
 * @param tokens token数组
 * @param ntokens token数量
 */
void lsp_lex_free(LspToken *tokens, int ntokens) {
    if (!tokens) return;
    for (int i = 0; i < ntokens; i++) {
        lsp_free(tokens[i].text);
    }
    lsp_free(tokens);
}