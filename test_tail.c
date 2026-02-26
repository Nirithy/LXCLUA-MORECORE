#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 5;
    lua_tcc_prologue(L, 0, 4);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* NEWTABLE */
    lua_createtable(L, 0, 0);
    lua_replace(L, 1);
    Label_3: /* EXTRAARG */
    /* NOP/EXTRAARG */
    Label_4: /* CLOSURE */
    lua_pushvalue(L, 1); /* upval 0 (local) */
    lua_pushcclosure(L, function_1, 1);
    lua_replace(L, 2);
    Label_5: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "tail_sum");
    lua_pop(L, 1);
    Label_6: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "tail_sum");
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 3, 100);
    Label_8: /* LOADI */
    lua_tcc_loadk_int(L, 4, 0);
    Label_9: /* TAILCALL */
    lua_tcc_push_args(L, 2, 3); /* func + args */
    lua_call(L, 2, LUA_MULTRET);
    return lua_gettop(L) - 5;
    Label_10: /* RETURN */
    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - 1);
    return lua_gettop(L) - 1;
    Label_11: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 5); /* Max Stack Size */
    Label_1: /* EQI */
    {
        lua_pushvalue(L, 1);
        lua_pushinteger(L, 0);
        int res = lua_compare(L, -2, -1, LUA_OPEQ);
        lua_pop(L, 2);
        if (res != 0) goto Label_3;
    }
    Label_2: /* JMP */
    goto Label_4;
    Label_3: /* RETURN1 */
    lua_pushvalue(L, 2);
    return 1;
    Label_4: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "tail_sum", 3);
    Label_5: /* ADDI */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, -1);
    lua_arith(L, LUA_OPADD);
    lua_replace(L, 4);
    Label_6: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_7: /* ADD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    lua_arith(L, 0);
    lua_replace(L, 5);
    Label_8: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_9: /* TAILCALL */
    lua_tcc_push_args(L, 3, 3); /* func + args */
    lua_call(L, 2, LUA_MULTRET);
    return lua_gettop(L) - 5;
    Label_10: /* RETURN */
    return lua_gettop(L) - 2;
    Label_11: /* RETURN0 */
    return 0;
}

int luaopen_test_tail(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
