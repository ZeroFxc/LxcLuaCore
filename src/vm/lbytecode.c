/*
** ByteCode Library
** Implements low-level bytecode manipulation
*/

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lapi.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lgc.h"
#include "ldebug.h"
#include "lopnames.h"
#include <string.h>

/*
** Helper to get Proto from stack argument.
** Accepts a Lua function (LClosure) or a lightuserdata (Proto*).
*/
static Proto *get_proto_from_arg (lua_State *L, int arg) {
  if (lua_isfunction(L, arg) && !lua_iscfunction(L, arg)) {
    const LClosure *cl = (const LClosure *)lua_topointer(L, arg);
    return cl->p;
  }
  if (lua_islightuserdata(L, arg)) {
    return (Proto *)lua_touserdata(L, arg);
  }
  luaL_argerror(L, arg, "expected Lua function or Proto lightuserdata");
  return NULL;
}

/*
** Helper to push TValue to stack
*/
static void push_tvalue (lua_State *L, const TValue *v) {
  if (ttisnil(v)) {
    lua_pushnil(L);
  } else if (ttisboolean(v)) {
    lua_pushboolean(L, !ttisfalse(v));
  } else if (ttisnumber(v)) {
    if (ttisinteger(v)) {
      lua_pushinteger(L, ivalue(v));
    } else {
      lua_pushnumber(L, fltvalue(v));
    }
  } else if (ttisstring(v)) {
    lua_pushstring(L, getstr(tsvalue(v)));
  } else {
    lua_pushfstring(L, "<%s>", luaT_objtypename(L, v));
  }
}

/*
** 1. ByteCode.CheckFunction(val)
** Checks if the value is a Lua function (not C function).
*/
static int bytecode_checkfunction (lua_State *L) {
  if (lua_isfunction(L, 1) && !lua_iscfunction(L, 1)) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

/*
** 2. ByteCode.GetProto(func)
** Returns the internal Proto structure as a lightuserdata.
** WARNING: The Proto is not anchored by this lightuserdata. Ensure the function
** stays alive or use IsGC to pin the Proto.
*/
static int bytecode_getproto (lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  if (lua_iscfunction(L, 1)) {
    return luaL_error(L, "expected Lua function");
  }
  const LClosure *cl = (const LClosure *)lua_topointer(L, 1);
  lua_pushlightuserdata(L, cl->p);
  return 1;
}

/*
** 3. ByteCode.GetCodeCount(proto)
** Returns the number of instructions in the function.
*/
static int bytecode_getcodecount (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_pushinteger(L, p->sizecode);
  return 1;
}

/*
** 4. ByteCode.GetCode(proto, index)
** Returns the instruction at the given 1-based index as an integer.
*/
static int bytecode_getcode (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_Integer idx = luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizecode) {
    return luaL_error(L, "index out of range");
  }
  Instruction i = p->code[idx - 1];
  lua_pushinteger(L, (lua_Integer)i);
  return 1;
}

/*
** 5. ByteCode.SetCode(proto, index, instruction)
** Sets the instruction at the given 1-based index.
** This modifies the bytecode in place (Self-Modifying Code).
*/
static int bytecode_setcode (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_Integer idx = luaL_checkinteger(L, 2);
  lua_Integer inst = luaL_checkinteger(L, 3);
  if (idx < 1 || idx > p->sizecode) {
    return luaL_error(L, "index out of range");
  }
  p->code[idx - 1] = (Instruction)inst;
  return 0;
}

/*
** 6. ByteCode.GetLine(proto, index)
** Returns the source line number for the instruction at the given 1-based index.
*/
static int bytecode_getline (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_Integer idx = luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizecode) {
    return luaL_error(L, "index out of range");
  }
  int line = luaG_getfuncline(p, (int)(idx - 1));
  lua_pushinteger(L, line);
  return 1;
}

/*
** 7. ByteCode.GetParamCount(proto)
** Returns the number of fixed parameters.
*/
static int bytecode_getparamcount (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_pushinteger(L, p->numparams);
  return 1;
}

/*
** 8. ByteCode.IsGC(proto)
** Marks the proto as fixed (prevents garbage collection).
** Effectively pins the Proto in memory.
*/
static int bytecode_isgc (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  luaC_fix(L, obj2gco(p));
  return 0;
}

/*
** 9. ByteCode.GetOpCode(inst)
** Returns the OpCode (integer) and its name (string).
*/
static int bytecode_getopcode (lua_State *L) {
  Instruction i = (Instruction)luaL_checkinteger(L, 1);
  OpCode op = GET_OPCODE(i);
  lua_pushinteger(L, op);
  if (op < NUM_OPCODES && opnames[op]) {
    lua_pushstring(L, opnames[op]);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

/*
** 10. ByteCode.GetArgs(inst)
** Returns a table with decoded arguments based on opcode format.
*/
static int bytecode_getargs (lua_State *L) {
  Instruction i = (Instruction)luaL_checkinteger(L, 1);
  OpCode op = GET_OPCODE(i);

  lua_createtable(L, 0, 4); /* result table */

  enum OpMode mode = getOpMode(op);

  /* Always push OpCode */
  lua_pushinteger(L, op);
  lua_setfield(L, -2, "OP");

  switch (mode) {
    case iABC:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_B(i));
      lua_setfield(L, -2, "B");
      lua_pushinteger(L, GETARG_C(i));
      lua_setfield(L, -2, "C");
      lua_pushinteger(L, GETARG_k(i));
      lua_setfield(L, -2, "k");
      break;
    case ivABC:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_vB(i));
      lua_setfield(L, -2, "B");
      lua_pushinteger(L, GETARG_vC(i));
      lua_setfield(L, -2, "C");
      lua_pushinteger(L, GETARG_k(i));
      lua_setfield(L, -2, "k");
      break;
    case iABx:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_Bx(i));
      lua_setfield(L, -2, "Bx");
      break;
    case iAsBx:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_sBx(i));
      lua_setfield(L, -2, "sBx");
      break;
    case iAx:
      lua_pushinteger(L, GETARG_Ax(i));
      lua_setfield(L, -2, "Ax");
      break;
    case isJ:
      lua_pushinteger(L, GETARG_sJ(i));
      lua_setfield(L, -2, "sJ");
      break;
  }
  return 1;
}

/*
** 11. ByteCode.Make(op, ...)
** Creates an instruction.
*/
static int bytecode_make (lua_State *L) {
  int op;
  if (lua_type(L, 1) == LUA_TSTRING) {
    const char *name = lua_tostring(L, 1);
    /* linear search for opcode name */
    for (op = 0; op < NUM_OPCODES; op++) {
      if (opnames[op] && strcmp(name, opnames[op]) == 0)
        break;
    }
    if (op == NUM_OPCODES)
      return luaL_error(L, "unknown opcode: %s", name);
  } else {
    op = (int)luaL_checkinteger(L, 1);
    if (op < 0 || op >= NUM_OPCODES)
      return luaL_error(L, "opcode out of range");
  }

  enum OpMode mode = getOpMode(op);
  Instruction i = 0;

  SET_OPCODE(i, op);

  switch (mode) {
    case iABC: {
      int a = (int)luaL_optinteger(L, 2, 0);
      int b = (int)luaL_optinteger(L, 3, 0);
      int c = (int)luaL_optinteger(L, 4, 0);
      int k = (int)luaL_optinteger(L, 5, 0);
      SETARG_A(i, a);
      SETARG_B(i, b);
      SETARG_C(i, c);
      SETARG_k(i, k);
      break;
    }
    case ivABC: {
      int a = (int)luaL_optinteger(L, 2, 0);
      int b = (int)luaL_optinteger(L, 3, 0);
      int c = (int)luaL_optinteger(L, 4, 0);
      int k = (int)luaL_optinteger(L, 5, 0);
      SETARG_A(i, a);
      SETARG_vB(i, b);
      SETARG_vC(i, c);
      SETARG_k(i, k);
      break;
    }
    case iABx: {
      int a = (int)luaL_optinteger(L, 2, 0);
      int bx = (int)luaL_optinteger(L, 3, 0);
      SETARG_A(i, a);
      SETARG_Bx(i, bx);
      break;
    }
    case iAsBx: {
      int a = (int)luaL_optinteger(L, 2, 0);
      int sbx = (int)luaL_optinteger(L, 3, 0);
      SETARG_A(i, a);
      SETARG_sBx(i, sbx);
      break;
    }
    case iAx: {
      int ax = (int)luaL_optinteger(L, 2, 0);
      SETARG_Ax(i, ax);
      break;
    }
    case isJ: {
      int sj = (int)luaL_optinteger(L, 2, 0);
      SETARG_sJ(i, sj);
      break;
    }
  }

  lua_pushinteger(L, (lua_Integer)i);
  return 1;
}

/*
** 12. ByteCode.Dump(proto)
*/
static int bytecode_dump (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }

  luaL_Buffer b;
  char buf[256];
  int i;

  luaL_buffinit(L, &b);

  snprintf(buf, sizeof(buf), "\nBytecode Dump: %d instructions\n", p->sizecode);
  luaL_addstring(&b, buf);

  for (i = 0; i < p->sizecode; i++) {
    Instruction inst = p->code[i];
    OpCode op = GET_OPCODE(inst);
    enum OpMode mode = getOpMode(op);
    const char *name = (op < NUM_OPCODES && opnames[op]) ? opnames[op] : "UNKNOWN";

    snprintf(buf, sizeof(buf), "%4d\t%-12s", i + 1, name);
    luaL_addstring(&b, buf);

    switch (mode) {
      case iABC:
        snprintf(buf, sizeof(buf), "\t%d %d %d", GETARG_A(inst), GETARG_B(inst), GETARG_C(inst));
        luaL_addstring(&b, buf);
        if (GETARG_k(inst)) luaL_addstring(&b, " k");
        break;
      case ivABC:
        snprintf(buf, sizeof(buf), "\t%d %d %d", GETARG_A(inst), GETARG_vB(inst), GETARG_vC(inst));
        luaL_addstring(&b, buf);
        if (GETARG_k(inst)) luaL_addstring(&b, " k");
        break;
      case iABx:
        snprintf(buf, sizeof(buf), "\t%d %d", GETARG_A(inst), GETARG_Bx(inst));
        luaL_addstring(&b, buf);
        break;
      case iAsBx:
        snprintf(buf, sizeof(buf), "\t%d %lld", GETARG_A(inst), (long long)GETARG_sBx(inst));
        luaL_addstring(&b, buf);
        break;
      case iAx:
        snprintf(buf, sizeof(buf), "\t%d", GETARG_Ax(inst));
        luaL_addstring(&b, buf);
        break;
      case isJ:
        snprintf(buf, sizeof(buf), "\t%d", GETARG_sJ(inst));
        luaL_addstring(&b, buf);
        break;
    }
    luaL_addstring(&b, "\n");
  }

  luaL_pushresult(&b);
  return 1;
}

/* NEW FUNCTIONS */

/* ByteCode.GetConstant(proto, index) */
static int bytecode_getconstant (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizek) {
    return luaL_error(L, "constant index out of range");
  }
  push_tvalue(L, &p->k[idx - 1]);
  return 1;
}

/* ByteCode.GetConstants(proto) */
static int bytecode_getconstants (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_createtable(L, p->sizek, 0);
  for (int i = 0; i < p->sizek; i++) {
    push_tvalue(L, &p->k[i]);
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

/* ByteCode.GetUpvalue(proto, index) */
static int bytecode_getupvalue (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizeupvalues) {
    return luaL_error(L, "upvalue index out of range");
  }
  Upvaldesc *up = &p->upvalues[idx - 1];
  lua_createtable(L, 0, 3);
  if (up->name) {
    lua_pushstring(L, getstr(up->name));
    lua_setfield(L, -2, "name");
  }
  lua_pushinteger(L, up->instack);
  lua_setfield(L, -2, "instack");
  lua_pushinteger(L, up->idx);
  lua_setfield(L, -2, "idx");
  return 1;
}

/* ByteCode.GetUpvalues(proto) */
static int bytecode_getupvalues (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_createtable(L, p->sizeupvalues, 0);
  for (int i = 0; i < p->sizeupvalues; i++) {
    Upvaldesc *up = &p->upvalues[i];
    lua_createtable(L, 0, 3);
    if (up->name) {
      lua_pushstring(L, getstr(up->name));
      lua_setfield(L, -2, "name");
    }
    lua_pushinteger(L, up->instack);
    lua_setfield(L, -2, "instack");
    lua_pushinteger(L, up->idx);
    lua_setfield(L, -2, "idx");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

/* ByteCode.GetLocal(proto, index) */
static int bytecode_getlocal (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizelocvars) {
    return luaL_error(L, "local index out of range");
  }
  LocVar *loc = &p->locvars[idx - 1];
  lua_createtable(L, 0, 3);
  if (loc->varname) {
    lua_pushstring(L, getstr(loc->varname));
    lua_setfield(L, -2, "name");
  }
  lua_pushinteger(L, loc->startpc);
  lua_setfield(L, -2, "startpc");
  lua_pushinteger(L, loc->endpc);
  lua_setfield(L, -2, "endpc");
  return 1;
}

/* ByteCode.GetLocals(proto) */
static int bytecode_getlocals (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_createtable(L, p->sizelocvars, 0);
  for (int i = 0; i < p->sizelocvars; i++) {
    LocVar *loc = &p->locvars[i];
    lua_createtable(L, 0, 3);
    if (loc->varname) {
      lua_pushstring(L, getstr(loc->varname));
      lua_setfield(L, -2, "name");
    }
    lua_pushinteger(L, loc->startpc);
    lua_setfield(L, -2, "startpc");
    lua_pushinteger(L, loc->endpc);
    lua_setfield(L, -2, "endpc");
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

/* ByteCode.GetNestedProto(proto, index) */
static int bytecode_getnestedproto (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizep) {
    return luaL_error(L, "nested proto index out of range");
  }
  lua_pushlightuserdata(L, p->p[idx - 1]);
  return 1;
}

/* ByteCode.GetNestedProtos(proto) */
static int bytecode_getnestedprotos (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  lua_createtable(L, p->sizep, 0);
  for (int i = 0; i < p->sizep; i++) {
    lua_pushlightuserdata(L, p->p[i]);
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

/*
** {===========================================================
** Proto 替换 API
** ============================================================
** 提供比 debug.hotfix 更完整的 proto 替换能力：
**   - 任意函数（不要求 upvalue）都可被替换
**   - 支持 upvalue 重映射
**   - 提供 upvalue 值的读写接口
**   - 标记 ishotfixed 标志
*/

/*
** 内部辅助：把栈上的参数转换为 LClosure 指针。
** 要求是 Lua 函数且不是 C 函数。返回 NULL 表示参数无效。
*/
static LClosure *get_lclosure_from_arg (lua_State *L, int arg) {
  if (!lua_isfunction(L, arg) || lua_iscfunction(L, arg)) {
    return NULL;
  }
  return (LClosure *)lua_topointer(L, arg);
}

/*
** 内部辅助：把栈上的参数转换为 LClosure 并检查是否合法。
** 不合法则抛错。
*/
static LClosure *check_lclosure_from_arg (lua_State *L, int arg) {
  LClosure *cl = get_lclosure_from_arg(L, arg);
  if (cl == NULL) {
    luaL_argerror(L, arg, "expected Lua function (not C function)");
    return NULL;
  }
  return cl;
}

/*
** 内部辅助：解析 upvalueMap（可选第三参数）并按映射重写目标闭包的 upvalues。
** upvalueMap 可以是：
**   - 0/nil：保持目标闭包原 upvalues 不变
**   - 数组形式的表：第 i 个 upvalue 来自 newcl 的 upvalueMap[i]
**   - 键值对形式的表：old_idx -> new_idx
** 注意：替换 upvalue 数量不能超过目标闭包原有的 nupvalues 槽位。
*/
static void remap_upvalues (lua_State *L, LClosure *oldcl, LClosure *newcl, int map_arg) {
  if (map_arg == 0) {
    /* 无映射：保留原 upvalues */
    return;
  }
  luaL_checktype(L, map_arg, LUA_TTABLE);
  int n = oldcl->nupvalues;
  for (int i = 1; i <= n; i++) {
    /* 优先按数组形式查找 */
    int src = 0;
    lua_rawgeti(L, map_arg, i);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      /* 再按键值对形式查找 */
      lua_pushinteger(L, i);
      lua_rawget(L, map_arg);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        continue;  /* 未指定则保持原 upvalue */
      }
      src = (int)lua_tointeger(L, -1);
      lua_pop(L, 1);
    } else {
      src = (int)lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    if (src < 1 || src > newcl->nupvalues) {
      luaL_error(L, "upvalue map index %d -> %d out of range (new closure has %d upvalues)",
                 i, src, newcl->nupvalues);
    }
    UpVal *newuv = newcl->upvals[src - 1];
    oldcl->upvals[i - 1] = newuv;
    luaC_objbarrier(L, oldcl, newuv);
  }
}

/*
** ByteCode.GetClosure(func)
** 返回 LClosure 的指针（lightuserdata）。
** 注意：返回的 lightuserdata 不会锚定闭包，调用方必须自行保证闭包存活。
*/
static int bytecode_getclosure (lua_State *L) {
  LClosure *cl = check_lclosure_from_arg(L, 1);
  lua_pushlightuserdata(L, cl);
  return 1;
}

/*
** ByteCode.SetProto(oldFunc, newFunc, [upvalueMap])
** 完整 proto 替换：把 oldFunc 的 Proto 替换为 newFunc 的 Proto。
** 相比 debug.hotfix：
**   - 不要求 oldFunc 必须有 upvalue
**   - 不要求 oldFunc/newFunc 的 upvalue 数量一致
**   - upvalueMap 可选；提供时按映射重写 upvalue 引用
** 返回 newFunc。
*/
static int bytecode_setproto (lua_State *L) {
  LClosure *oldcl = check_lclosure_from_arg(L, 1);
  LClosure *newcl = check_lclosure_from_arg(L, 2);
  if (oldcl->p->flag & PF_LOCKED) {
    return luaL_error(L, "cannot SetProto on locked function");
  }
  /* 第三参数可选：upvalueMap */
  int map_arg = (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) ? 3 : 0;
  /* 替换 proto：先做 GC 写屏障 */
  luaC_objbarrier(L, oldcl, newcl->p);
  oldcl->p = newcl->p;
  oldcl->ishotfixed = 1;
  cast(Closure *, oldcl)->c.ishotfixed = 1;
  /* 重新映射 upvalue */
  remap_upvalues(L, oldcl, newcl, map_arg);
  /* 返回 newFunc 便于链式调用 */
  lua_pushvalue(L, 2);
  return 1;
}

/*
** ByteCode.GetUpvalueValues(func)
** 把闭包的 upvalue 实际值打包成数组返回。
** （区别于 GetUpvalues(proto) 返回的是描述符）
*/
static int bytecode_getupvaluevalues (lua_State *L) {
  LClosure *cl = check_lclosure_from_arg(L, 1);
  lua_createtable(L, cl->nupvalues, 0);
  for (int i = 0; i < cl->nupvalues; i++) {
    UpVal *uv = cl->upvals[i];
    if (uv == NULL || uv->v.p == NULL) {
      lua_pushnil(L);
    } else {
      /* open upvalue: v.p 指向栈；closed upvalue: v.p 指向 u.value */
      setobj2s(L, L->top.p, uv->v.p);
      api_incr_top(L);
    }
    lua_rawseti(L, -2, i + 1);
  }
  return 1;
}

/*
** ByteCode.SetUpvalueValue(func, idx, value)
** 把 func 的第 idx 个 upvalue 的实际值改为 value。
** idx 从 1 开始。
*/
static int bytecode_setupvaluevalue (lua_State *L) {
  LClosure *cl = check_lclosure_from_arg(L, 1);
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > cl->nupvalues) {
    return luaL_error(L, "upvalue index %d out of range (1..%d)", idx, cl->nupvalues);
  }
  UpVal *uv = cl->upvals[idx - 1];
  if (uv == NULL) {
    return luaL_error(L, "upvalue %d is NULL", idx);
  }
  /* 写 upvalue：open upvalue 时直接改栈；closed upvalue 改 u.value 字段 */
  setobj(L, uv->v.p, s2v(L->top.p - 1));
  luaC_barrier(L, uv, s2v(L->top.p - 1));
  return 0;
}

/*
** ByteCode.IsHotFixed(func)
** 检查函数是否被热修过。
*/
static int bytecode_ishotfixed (lua_State *L) {
  LClosure *cl = check_lclosure_from_arg(L, 1);
  lua_pushboolean(L, cl->ishotfixed != 0);
  return 1;
}

/*
** ByteCode.GetMaxStackSize(proto)
** 返回 proto 的 maxstacksize。
*/
static int bytecode_getmaxstacksize (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  lua_pushinteger(L, p->maxstacksize);
  return 1;
}

/*
** ByteCode.GetNumParams(proto)
** 返回 proto 的 numparams。
*/
static int bytecode_getnumparams (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  lua_pushinteger(L, p->numparams);
  return 1;
}

/*
** ByteCode.GetUpvalueCount(proto)
** 返回 proto 的 upvalue 数量。
*/
static int bytecode_getupvaluecount (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  lua_pushinteger(L, p->sizeupvalues);
  return 1;
}

/*
** ByteCode.GetSource(proto)
** 返回 proto 的源文件名。
*/
static int bytecode_getsource (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->source == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, getstr(p->source));
  }
  return 1;
}

/* }======================================================= */

/* Helper to decode instruction to table (reused logic) */
static void decode_instruction(lua_State *L, Instruction i) {
  OpCode op = GET_OPCODE(i);
  enum OpMode mode = getOpMode(op);
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, op);
  lua_setfield(L, -2, "OP");

  switch (mode) {
    case iABC:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_B(i));
      lua_setfield(L, -2, "B");
      lua_pushinteger(L, GETARG_C(i));
      lua_setfield(L, -2, "C");
      lua_pushinteger(L, GETARG_k(i));
      lua_setfield(L, -2, "k");
      break;
    case ivABC:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_vB(i));
      lua_setfield(L, -2, "B");
      lua_pushinteger(L, GETARG_vC(i));
      lua_setfield(L, -2, "C");
      lua_pushinteger(L, GETARG_k(i));
      lua_setfield(L, -2, "k");
      break;
    case iABx:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_Bx(i));
      lua_setfield(L, -2, "Bx");
      break;
    case iAsBx:
      lua_pushinteger(L, GETARG_A(i));
      lua_setfield(L, -2, "A");
      lua_pushinteger(L, GETARG_sBx(i));
      lua_setfield(L, -2, "sBx");
      break;
    case iAx:
      lua_pushinteger(L, GETARG_Ax(i));
      lua_setfield(L, -2, "Ax");
      break;
    case isJ:
      lua_pushinteger(L, GETARG_sJ(i));
      lua_setfield(L, -2, "sJ");
      break;
  }
}

/* ByteCode.GetInstruction(proto, index) */
static int bytecode_getinstruction (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizecode) {
    return luaL_error(L, "instruction index out of range");
  }
  Instruction i = p->code[idx - 1];
  decode_instruction(L, i);
  return 1;
}

/* Helper to get integer field from table */
static int get_int_field(lua_State *L, int table_idx, const char *key, int def) {
  lua_getfield(L, table_idx, key);
  int res = def;
  if (lua_isinteger(L, -1)) {
    res = (int)lua_tointeger(L, -1);
  } else if (lua_isnumber(L, -1)) {
     res = (int)lua_tonumber(L, -1);
  }
  lua_pop(L, 1);
  return res;
}

/* ByteCode.SetInstruction(proto, index, table) */
static int bytecode_setinstruction (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  int idx = (int)luaL_checkinteger(L, 2);
  if (idx < 1 || idx > p->sizecode) {
    return luaL_error(L, "instruction index out of range");
  }
  luaL_checktype(L, 3, LUA_TTABLE);

  lua_getfield(L, 3, "OP");
  if (!lua_isinteger(L, -1)) {
    return luaL_error(L, "table must have 'OP' field");
  }
  int op = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);

  if (op < 0 || op >= NUM_OPCODES) {
     return luaL_error(L, "invalid opcode");
  }

  enum OpMode mode = getOpMode(op);
  Instruction i = 0;
  SET_OPCODE(i, op);

  switch (mode) {
    case iABC: {
      int a = get_int_field(L, 3, "A", 0);
      int b = get_int_field(L, 3, "B", 0);
      int c = get_int_field(L, 3, "C", 0);
      int k = get_int_field(L, 3, "k", 0);
      SETARG_A(i, a);
      SETARG_B(i, b);
      SETARG_C(i, c);
      SETARG_k(i, k);
      break;
    }
    case ivABC: {
      int a = get_int_field(L, 3, "A", 0);
      int b = get_int_field(L, 3, "B", 0);
      int c = get_int_field(L, 3, "C", 0);
      int k = get_int_field(L, 3, "k", 0);
      SETARG_A(i, a);
      SETARG_vB(i, b);
      SETARG_vC(i, c);
      SETARG_k(i, k);
      break;
    }
    case iABx: {
      int a = get_int_field(L, 3, "A", 0);
      int bx = get_int_field(L, 3, "Bx", 0);
      SETARG_A(i, a);
      SETARG_Bx(i, bx);
      break;
    }
    case iAsBx: {
      int a = get_int_field(L, 3, "A", 0);
      int sbx = get_int_field(L, 3, "sBx", 0);
      SETARG_A(i, a);
      SETARG_sBx(i, sbx);
      break;
    }
    case iAx: {
      int ax = get_int_field(L, 3, "Ax", 0);
      SETARG_Ax(i, ax);
      break;
    }
    case isJ: {
      int sj = get_int_field(L, 3, "sJ", 0);
      SETARG_sJ(i, sj);
      break;
    }
  }

  p->code[idx - 1] = i;
  return 0;
}

/*
** ByteCode.Lock(proto)
** Locks the function, preventing further modification.
*/
static int bytecode_lock (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  p->flag |= PF_LOCKED;
  return 0;
}

/*
** ByteCode.IsLocked(proto)
** Checks if the function is locked.
*/
static int bytecode_islocked (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  lua_pushboolean(L, (p->flag & PF_LOCKED) != 0);
  return 1;
}

/*
** ByteCode.MarkOriginal(proto)
** Records the current bytecode hash as "original".
*/
static int bytecode_markoriginal (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->flag & PF_LOCKED) {
    return luaL_error(L, "function is locked");
  }
  p->bytecode_hash = luaF_hashcode(p);
  return 0;
}

/*
** ByteCode.IsTampered(proto)
** Checks if the bytecode has been tampered with since MarkOriginal was called.
*/
static int bytecode_istampered (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
  if (p->bytecode_hash == 0) {
    lua_pushboolean(L, 0); /* Not marked, so not tampered (relative to unknown baseline) */
  } else {
    uint64_t h = luaF_hashcode(p);
    lua_pushboolean(L, h != p->bytecode_hash);
  }
  return 1;
}


static const luaL_Reg bytecode_funcs[] = {
  /* ===== PascalCase 原名（向后兼容） ===== */
  {"CheckFunction", bytecode_checkfunction},
  {"GetProto", bytecode_getproto},
  {"GetCodeCount", bytecode_getcodecount},
  {"GetCode", bytecode_getcode},
  {"SetCode", bytecode_setcode},
  {"GetLine", bytecode_getline},
  {"GetParamCount", bytecode_getparamcount},
  {"IsGC", bytecode_isgc},
  {"GetOpCode", bytecode_getopcode},
  {"GetArgs", bytecode_getargs},
  {"Make", bytecode_make},
  {"Dump", bytecode_dump},

  /* 早期新增的 PascalCase 函数 */
  {"GetConstant", bytecode_getconstant},
  {"GetConstants", bytecode_getconstants},
  {"GetUpvalue", bytecode_getupvalue},
  {"GetUpvalues", bytecode_getupvalues},
  {"GetLocal", bytecode_getlocal},
  {"GetLocals", bytecode_getlocals},
  {"GetNestedProto", bytecode_getnestedproto},
  {"GetNestedProtos", bytecode_getnestedprotos},
  {"GetInstruction", bytecode_getinstruction},
  {"SetInstruction", bytecode_setinstruction},
  {"Lock", bytecode_lock},
  {"IsLocked", bytecode_islocked},
  {"MarkOriginal", bytecode_markoriginal},
  {"IsTampered", bytecode_istampered},

  /* ===== 本次新增：proto 替换 API（PascalCase） ===== */
  {"GetClosure", bytecode_getclosure},
  {"SetProto", bytecode_setproto},
  {"GetUpvalueValues", bytecode_getupvaluevalues},
  {"SetUpvalueValue", bytecode_setupvaluevalue},
  {"IsHotFixed", bytecode_ishotfixed},
  {"GetMaxStackSize", bytecode_getmaxstacksize},
  {"GetNumParams", bytecode_getnumparams},
  {"GetUpvalueCount", bytecode_getupvaluecount},
  {"GetSource", bytecode_getsource},

  /* ===== 纯小写别名（与 PascalCase 一一对应） ===== */
  {"checkfunction", bytecode_checkfunction},
  {"getproto", bytecode_getproto},
  {"getcodecount", bytecode_getcodecount},
  {"getcode", bytecode_getcode},
  {"setcode", bytecode_setcode},
  {"getline", bytecode_getline},
  {"getparamcount", bytecode_getparamcount},
  {"isgc", bytecode_isgc},
  {"getopcode", bytecode_getopcode},
  {"getargs", bytecode_getargs},
  {"make", bytecode_make},
  {"dump", bytecode_dump},

  {"getconstant", bytecode_getconstant},
  {"getconstants", bytecode_getconstants},
  {"getupvalue", bytecode_getupvalue},
  {"getupvalues", bytecode_getupvalues},
  {"getlocal", bytecode_getlocal},
  {"getlocals", bytecode_getlocals},
  {"getnestedproto", bytecode_getnestedproto},
  {"getnestedprotos", bytecode_getnestedprotos},
  {"getinstruction", bytecode_getinstruction},
  {"setinstruction", bytecode_setinstruction},
  {"lock", bytecode_lock},
  {"islocked", bytecode_islocked},
  {"markoriginal", bytecode_markoriginal},
  {"istampered", bytecode_istampered},

  /* proto 替换 API 的小写别名 */
  {"getclosure", bytecode_getclosure},
  {"setproto", bytecode_setproto},
  {"getupvaluevalues", bytecode_getupvaluevalues},
  {"setupvaluevalue", bytecode_setupvaluevalue},
  {"ishotfixed", bytecode_ishotfixed},
  {"getmaxstacksize", bytecode_getmaxstacksize},
  {"getnumparams", bytecode_getnumparams},
  {"getupvaluecount", bytecode_getupvaluecount},
  {"getsource", bytecode_getsource},

  {NULL, NULL}
};

LUAMOD_API int luaopen_ByteCode (lua_State *L) {
  luaL_newlib(L, bytecode_funcs);

  /* ===== OpCodes 表（PascalCase 原名） ===== */
  lua_newtable(L);
  for (int i = 0; i < NUM_OPCODES; i++) {
    if (opnames[i]) {
      lua_pushinteger(L, i);
      lua_setfield(L, -2, opnames[i]);
    }
  }
  lua_setfield(L, -2, "OpCodes");

  /* ===== OpCodes 表的纯小写别名 ===== */
  lua_newtable(L);
  for (int i = 0; i < NUM_OPCODES; i++) {
    if (opnames[i]) {
      lua_pushinteger(L, i);
      lua_setfield(L, -2, opnames[i]);
    }
  }
  lua_setfield(L, -2, "opcodes");

  return 1;
}
