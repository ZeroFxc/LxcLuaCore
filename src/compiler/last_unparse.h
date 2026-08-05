/*
** last_unparse.h
** AST Unparser - 将 C 层 AstChunk* 或 Lua table 格式的序列化 AST
** 反解析为合法的 Lua 源码字符串（人类可读，保证可重新 parse）
*/

#ifndef last_unparse_h
#define last_unparse_h

#include "lua.h"
#include "llimits.h"
#include "lobject.h"  /* TString */

#include "last.h"      /* AstChunk, AstFunc, AstBlock, AstStmt, AstExpr */

LUAI_FUNC int luaY_ast_unparse_chunk(lua_State *L, AstChunk *chunk);

LUAI_FUNC int luaY_ast_unparse_from_table(lua_State *L, int table_idx);

#endif
