#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);
static int function_2(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 3;
    lua_tcc_prologue(L, 0, 2);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushcclosure(L, function_1, 0);
    lua_replace(L, 1);
    Label_3: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 2);
    Label_4: /* TAILCALL */
    lua_tcc_push_args(L, 2, 1); /* func + args */
    lua_call(L, 0, -1);
    return lua_gettop(L) - 3;
    Label_5: /* RETURN */
    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - 1);
    return lua_gettop(L) - 1;
    Label_6: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 2); /* Max Stack Size */
    Label_1: /* LOADI */
    lua_tcc_loadk_int(L, 1, 0);
    Label_2: /* CLOSURE */
    lua_pushvalue(L, 1); /* upval 0 (local) */
    lua_pushcclosure(L, function_2, 1);
    lua_replace(L, 2);
    Label_3: /* RETURN */
    lua_tcc_push_args(L, 2, 1);
    return 1;
    Label_4: /* RETURN */
    return 0;
}

/* Proto 2 */
static int function_2(lua_State *L) {
    lua_settop(L, 2); /* Max Stack Size */
    Label_1: /* GETUPVAL */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_replace(L, 1);
    Label_2: /* ADDI */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 1);
    lua_arith(L, 0);
    lua_replace(L, 1);
    Label_3: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_4: /* SETUPVAL */
    lua_pushvalue(L, 1);
    lua_replace(L, lua_upvalueindex(1));
    Label_5: /* GETUPVAL */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_replace(L, 1);
    Label_6: /* RETURN1 */
    lua_pushvalue(L, 1);
    return 1;
    Label_7: /* RETURN0 */
    return 0;
}

int luaopen_test_closure(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
