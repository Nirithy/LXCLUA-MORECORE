#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 3;
    lua_tcc_prologue(L, 0, 2);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushcclosure(L, function_1, 1);
    lua_replace(L, 1);
    Label_3: /* NEWTABLE */
    lua_createtable(L, 0, 1);
    lua_replace(L, 2);
    Label_4: /* EXTRAARG */
    /* EXTRAARG */
    Label_5: /* SETFIELD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "test_table_debug");
    lua_pop(L, 1);
    Label_6: /* RETURN */
    lua_tcc_push_args(L, 2, 1);
    return 1;
    Label_7: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 5); /* Max Stack Size */
    Label_1: /* NEWTABLE */
    lua_createtable(L, 12, 1);
    lua_replace(L, 1);
    Label_2: /* EXTRAARG */
    /* EXTRAARG */
    Label_3: /* LOADI */
    lua_tcc_loadk_int(L, 2, 10);
    Label_4: /* LOADI */
    lua_tcc_loadk_int(L, 3, 20);
    Label_5: /* LOADI */
    lua_tcc_loadk_int(L, 4, 30);
    Label_6: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 100);
    lua_setfield(L, -2, "x");
    lua_pop(L, 1);
    Label_7: /* SETLIST */
    {
        int n = 3;
        if (n == 0) {
            n = lua_gettop(L) - 1;
        }
        lua_pushvalue(L, 1); /* table */
        for (int j = 1; j <= n; j++) {
            lua_pushvalue(L, 1 + j);
            lua_seti(L, -2, 0 + j);
        }
        lua_pop(L, 1);
    }
    Label_8: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "print", 2);
    Label_9: /* LOADK */
    lua_tcc_loadk_str(L, 3, "t[1]=");
    Label_10: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "tostring", 4);
    Label_11: /* GETI */
    lua_pushvalue(L, 1);
    lua_geti(L, -1, 1);
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_12: /* CALL */
    {
    lua_tcc_push_args(L, 4, 2); /* func + args */
    lua_call(L, 1, 1);
    lua_tcc_store_results(L, 4, 1);
    }
    Label_13: /* CONCAT */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    lua_concat(L, 2);
    lua_replace(L, 3);
    Label_14: /* CALL */
    {
    lua_tcc_push_args(L, 2, 2); /* func + args */
    lua_call(L, 1, 0);
    lua_tcc_store_results(L, 2, 0);
    }
    Label_15: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "print", 2);
    Label_16: /* LOADK */
    lua_tcc_loadk_str(L, 3, "t.x=");
    Label_17: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "tostring", 4);
    Label_18: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "x");
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_19: /* CALL */
    {
    lua_tcc_push_args(L, 4, 2); /* func + args */
    lua_call(L, 1, 1);
    lua_tcc_store_results(L, 4, 1);
    }
    Label_20: /* CONCAT */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    lua_concat(L, 2);
    lua_replace(L, 3);
    Label_21: /* CALL */
    {
    lua_tcc_push_args(L, 2, 2); /* func + args */
    lua_call(L, 1, 0);
    lua_tcc_store_results(L, 2, 0);
    }
    Label_22: /* GETI */
    lua_pushvalue(L, 1);
    lua_geti(L, -1, 1);
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_23: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "x");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_24: /* ADD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_arith(L, 0);
    lua_replace(L, 2);
    Label_25: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_26: /* RETURN1 */
    lua_pushvalue(L, 2);
    return 1;
    Label_27: /* RETURN0 */
    return 0;
}

int luaopen_test_table_debug(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
