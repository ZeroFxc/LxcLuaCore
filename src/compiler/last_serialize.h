#ifndef last_serialize_h
#define last_serialize_h

#include "last.h"

/* AST → Lua table 序列化 */
LUAI_FUNC void ast_serialize_to_lua(lua_State *L, AstChunk *chunk);

/* Lua table → AST 反序列化 */
LUAI_FUNC AstChunk *ast_deserialize_from_lua(lua_State *L, int idx);

#endif