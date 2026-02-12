/*
** lvmpro.c
** Virtual Machine Protection for Lua
*/

#define lvmpro_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"
#include "lundump.h"

/* Helper to convert Instruction to Lua Integer */
static lua_Integer inst2int(Instruction i) {
  return (lua_Integer)i;
}

/* Helper to generate opcode branch */
static void gen_opcode(lua_State *L, luaL_Buffer *b, int op, const char *name, const char *code) {
  lua_pushfstring(L, "    elseif _op == %d then -- %s\n", op, name);
  luaL_addvalue(b);
  luaL_addstring(b, code);
}

/* Recursive compilation function */
static int vm_compile(lua_State *L, Proto *p) {
  /* Stack: [..., resulting_factory_func] */

  /* 1. Constants Table (Index 2 in sub-call) */
  lua_createtable(L, p->sizek, 0);
  for (int i = 0; i < p->sizek; i++) {
    TValue *k = &p->k[i];
    if (ttisnil(k)) lua_pushnil(L);
    else if (ttisboolean(k)) lua_pushboolean(L, !ttisfalse(k));
    else if (ttisnumber(k)) {
      if (ttisinteger(k)) lua_pushinteger(L, ivalue(k));
      else lua_pushnumber(L, fltvalue(k));
    }
    else if (ttisstring(k)) {
      lua_pushlstring(L, getstr(tsvalue(k)), tsslen(tsvalue(k)));
    }
    else {
      lua_pushnil(L); /* Fallback */
    }
    lua_rawseti(L, -2, i);
  }

  /* 2. Sub-Protos Table (Index 3) */
  lua_createtable(L, p->sizep, 0);
  for (int i = 0; i < p->sizep; i++) {
    vm_compile(L, p->p[i]);
    lua_rawseti(L, -2, i);
  }

  /* 3. Code Table (Index 4) */
  lua_createtable(L, p->sizecode, 0);
  for (int i = 0; i < p->sizecode; i++) {
    lua_pushinteger(L, inst2int(p->code[i]));
    lua_rawseti(L, -2, i + 1);
  }

  /* 4. Upvalues Info (Index 5) */
  lua_pushinteger(L, p->sizeupvalues);

  /* 5. Generate VM Source */
  luaL_Buffer b;
  luaL_buffinit(L, &b);

  luaL_addstring(&b, "local _k, _p, _code, _nup = ...\n");
  luaL_addstring(&b, "return function(...)\n");
  luaL_addstring(&b, "  local _regs = table.pack(...)\n");
  luaL_addstring(&b, "  local _top = _regs.n\n");
  luaL_addstring(&b, "  local _pc = 1\n");
  luaL_addstring(&b, "  local _up = {}\n");

  luaL_addstring(&b, "  while true do\n");
  luaL_addstring(&b, "    local _inst = _code[_pc]\n");
  luaL_addstring(&b, "    if not _inst then return end\n");

  luaL_addstring(&b, "    local _op = _inst & 0x1FF\n");
  luaL_addstring(&b, "    local _a = (_inst >> 9) & 0xFFFF\n");
  luaL_addstring(&b, "    local _k = (_inst >> 25) & 1\n");
  luaL_addstring(&b, "    local _b = (_inst >> 26) & 0xFFFF\n");
  luaL_addstring(&b, "    local _c = (_inst >> 42) & 0xFFFF\n");
  luaL_addstring(&b, "    local _bx = (_inst >> 25) & 0x1FFFFFFFF\n");
  lua_pushfstring(L, "    local _sbx = _bx - %d\n", OFFSET_sBx);
  luaL_addvalue(&b);

  /* Helpers */
  luaL_addstring(&b, "    local function R(i) return _regs[i+1] end\n");
  luaL_addstring(&b, "    local function SR(i, v) _regs[i+1] = v end\n");
  luaL_addstring(&b, "    local function K(i) return _k[i] end\n");

  luaL_addstring(&b, "    _pc = _pc + 1\n");

  luaL_addstring(&b, "    if _op == -1 then\n"); // Dummy start

  gen_opcode(L, &b, OP_MOVE, "OP_MOVE", "      SR(_a, R(_b))\n");
  gen_opcode(L, &b, OP_LOADI, "OP_LOADI", "      SR(_a, _sbx)\n");
  gen_opcode(L, &b, OP_LOADK, "OP_LOADK", "      SR(_a, K(_bx))\n");
  gen_opcode(L, &b, OP_LOADNIL, "OP_LOADNIL", "      for i = _a, _a + _b do SR(i, nil) end\n");

  gen_opcode(L, &b, OP_ADD, "OP_ADD", "      SR(_a, R(_b) + R(_c))\n");
  gen_opcode(L, &b, OP_SUB, "OP_SUB", "      SR(_a, R(_b) - R(_c))\n");
  gen_opcode(L, &b, OP_MUL, "OP_MUL", "      SR(_a, R(_b) * R(_c))\n");
  gen_opcode(L, &b, OP_DIV, "OP_DIV", "      SR(_a, R(_b) / R(_c))\n");

  gen_opcode(L, &b, OP_ADDK, "OP_ADDK", "      SR(_a, R(_b) + K(_c))\n");
  gen_opcode(L, &b, OP_SUBK, "OP_SUBK", "      SR(_a, R(_b) - K(_c))\n");
  gen_opcode(L, &b, OP_MULK, "OP_MULK", "      SR(_a, R(_b) * K(_c))\n");
  gen_opcode(L, &b, OP_DIVK, "OP_DIVK", "      SR(_a, R(_b) / K(_c))\n");

  lua_pushfstring(L, "    elseif _op == %d then -- OP_JMP\n", OP_JMP);
  luaL_addvalue(&b);
  luaL_addstring(&b, "      local _sj = (_inst >> 9) & 0x1FFFFFFFFFFFF\n");
  lua_pushfstring(L, "      local _real_sj = _sj - %d\n", OFFSET_sJ);
  luaL_addvalue(&b);
  luaL_addstring(&b, "      _pc = _pc + _real_sj - 1\n");

  /* Comparison */
  gen_opcode(L, &b, OP_EQ, "OP_EQ",
    "      local val = (R(_a) == R(_b))\n"
    "      if (val ~= (_k~=0)) then _pc = _pc + 1 end\n");

  gen_opcode(L, &b, OP_LT, "OP_LT",
    "      local val = (R(_a) < R(_b))\n"
    "      if (val ~= (_k~=0)) then _pc = _pc + 1 end\n");

  gen_opcode(L, &b, OP_LE, "OP_LE",
    "      local val = (R(_a) <= R(_b))\n"
    "      if (val ~= (_k~=0)) then _pc = _pc + 1 end\n");

  /* Returns */
  gen_opcode(L, &b, OP_RETURN, "OP_RETURN",
    "      local count = _b - 1\n"
    "      if count == 1 then return R(_a) end\n"
    "      if count == 0 then return end\n"
    "      local ret = {}\n"
    "      if count < 0 then count = _top - _a + 1 end\n"
    "      for i=0, count-1 do table.insert(ret, R(_a+i)) end\n"
    "      return table.unpack(ret)\n");

  gen_opcode(L, &b, OP_RETURN1, "OP_RETURN1", "      return R(_a)\n");
  gen_opcode(L, &b, OP_RETURN0, "OP_RETURN0", "      return\n");

  /* Call */
  gen_opcode(L, &b, OP_CALL, "OP_CALL",
    "      local func = R(_a)\n"
    "      local args = {}\n"
    "      local nparams = _b - 1\n"
    "      if nparams < 0 then nparams = _top - _a end\n"
    "      for i=1, nparams do table.insert(args, R(_a+i)) end\n"
    "      local results = table.pack(func(table.unpack(args)))\n"
    "      local nres = _c - 1\n"
    "      if nres < 0 then\n"
    "        _top = _a + results.n - 1\n"
    "        nres = results.n\n"
    "      end\n"
    "      for i=1, nres do SR(_a+i-1, results[i]) end\n");

  /* Closure */
  gen_opcode(L, &b, OP_CLOSURE, "OP_CLOSURE", "       SR(_a, _p[_bx])\n");

  luaL_addstring(&b, "    else\n");
  luaL_addstring(&b, "      error('Unimplemented VMP opcode: ' .. _op)\n");
  luaL_addstring(&b, "    end\n");

  luaL_addstring(&b, "  end\n"); // while
  luaL_addstring(&b, "end\n"); // return function

  luaL_pushresult(&b);

  if (luaL_loadstring(L, lua_tostring(L, -1)) != LUA_OK) {
    return lua_error(L);
  }

  lua_remove(L, -2); /* source */

  /* Call Factory */
  int top = lua_gettop(L);
  printf("DEBUG Top: %d\n", top);
  if (top > 0) printf("Type at top: %s\n", lua_typename(L, lua_type(L, top)));
  if (top > 1) printf("Type at top-1: %s\n", lua_typename(L, lua_type(L, top-1)));

  /* Stack: f(1), k(2), p(3), code(4), nup(5), factory(6) */

  lua_pushvalue(L, 2); // k
  lua_pushvalue(L, 3); // p
  lua_pushvalue(L, 4); // code
  lua_pushvalue(L, 5); // nup

  if (lua_pcall(L, 4, 1, 0) != LUA_OK) {
    return lua_error(L);
  }

  /* Stack: f, k, p, code, nup, RESULT */
  lua_replace(L, 2); /* RESULT moves to position of k */
  lua_pop(L, 3); /* Remove p, code, nup */
  /* Stack: f, RESULT */

  return 1;
}

static int l_protect(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);

  CallInfo *ci = L->ci;
  StkId base = ci->func.p + 1;
  TValue *o = s2v(base);

  if (!isLfunction(o)) {
    luaL_error(L, "Only Lua functions can be protected");
  }

  LClosure *cl_in = clLvalue(o);
  Proto *p = cl_in->p;

  return vm_compile(L, p);
}

static const luaL_Reg vmlib[] = {
  {"protect", l_protect},
  {NULL, NULL}
};

LUAMOD_API int luaopen_vmprotect(lua_State *L) {
  luaL_newlib(L, vmlib);
  return 1;
}
