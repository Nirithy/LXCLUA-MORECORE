#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    lua_tcc_prologue(L, 0, 3);
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
    lua_setfield(L, -2, "fib");
    lua_pop(L, 1);
    Label_6: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "fib");
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 3, 10);
    Label_8: /* TAILCALL */
    lua_tcc_push_args(L, 2, 2); /* func + args */
    lua_call(L, 1, LUA_MULTRET);
    return lua_gettop(L) - 4;
    Label_9: /* RETURN */
    return lua_gettop(L) - 1;
    Label_10: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 4); /* Max Stack Size */
    Label_1: /* LTI */
    {
        lua_pushvalue(L, 1);
        lua_pushinteger(L, 2);
        int res = lua_compare(L, -2, -1, LUA_OPLT);
        lua_pop(L, 2);
        if (res != 0) goto Label_3;
    }
    Label_2: /* JMP */
    goto Label_4;
    Label_3: /* RETURN1 */
    lua_pushvalue(L, 1);
    return 1;
    Label_4: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "fib", 2);
    Label_5: /* ADDI */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, -1);
    lua_arith(L, LUA_OPADD);
    lua_replace(L, 3);
    Label_6: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_7: /* CALL */
    {
    lua_tcc_push_args(L, 2, 2); /* func + args */
    lua_call(L, 1, 1);
    lua_tcc_store_results(L, 2, 1);
    }
    Label_8: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "fib", 3);
    Label_9: /* ADDI */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, -2);
    lua_arith(L, LUA_OPADD);
    lua_replace(L, 4);
    Label_10: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_11: /* CALL */
    {
    lua_tcc_push_args(L, 3, 2); /* func + args */
    lua_call(L, 1, 1);
    lua_tcc_store_results(L, 3, 1);
    }
    Label_12: /* ADD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_arith(L, 0);
    lua_replace(L, 2);
    Label_13: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_14: /* RETURN1 */
    lua_pushvalue(L, 2);
    return 1;
    Label_15: /* RETURN0 */
    return 0;
}

int luaopen_test_fib_rec(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
