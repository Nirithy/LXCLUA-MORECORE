/*
** ByteCode Library
** Implements low-level bytecode manipulation
*/

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lgc.h"
#include "ldebug.h"

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
  lua_pushinteger(L, p->sizecode);
  return 1;
}

/*
** 4. ByteCode.GetCode(proto, index)
** Returns the instruction at the given 1-based index as an integer.
*/
static int bytecode_getcode (lua_State *L) {
  Proto *p = get_proto_from_arg(L, 1);
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
  luaC_fix(L, obj2gco(p));
  return 0;
}

static const luaL_Reg bytecode_funcs[] = {
  {"CheckFunction", bytecode_checkfunction},
  {"GetProto", bytecode_getproto},
  {"GetCodeCount", bytecode_getcodecount},
  {"GetCode", bytecode_getcode},
  {"SetCode", bytecode_setcode},
  {"GetLine", bytecode_getline},
  {"GetParamCount", bytecode_getparamcount},
  {"IsGC", bytecode_isgc},
  {NULL, NULL}
};

LUAMOD_API int luaopen_ByteCode (lua_State *L) {
  luaL_newlib(L, bytecode_funcs);
  return 1;
}
