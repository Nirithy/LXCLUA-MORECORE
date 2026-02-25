#include "lua.h"
#include "lauxlib.h"

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    {
        int nargs = lua_gettop(L);
        int nparams = 0;
        lua_createtable(L, (nargs > nparams) ? nargs - nparams : 0, 0);
        if (nargs > nparams) {
            for (int i = nparams + 1; i <= nargs; i++) {
                lua_pushvalue(L, i);
                lua_rawseti(L, -2, i - nparams);
            }
        }
        int table_pos = lua_gettop(L);
        int target = 2 + 1;
        if (table_pos >= target) {
            lua_replace(L, target);
            lua_settop(L, target);
        } else {
            lua_settop(L, target);
            lua_pushvalue(L, table_pos);
            lua_replace(L, target);
            lua_pushnil(L);
            lua_replace(L, table_pos);
        }
    }
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
    /* NOP/EXTRAARG */
    Label_5: /* SETFIELD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "test_table_debug");
    lua_pop(L, 1);
    Label_6: /* RETURN */
    lua_pushvalue(L, 2);
    return 1;
    Label_7: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 5); /* Max Stack Size */
    Label_1: /* NEWTABLE */
    lua_createtable(L, 3, 1);
    lua_replace(L, 1);
    Label_2: /* EXTRAARG */
    /* NOP/EXTRAARG */
    Label_3: /* LOADI */
    lua_pushinteger(L, 10);
    lua_replace(L, 2);
    Label_4: /* LOADI */
    lua_pushinteger(L, 20);
    lua_replace(L, 3);
    Label_5: /* LOADI */
    lua_pushinteger(L, 30);
    lua_replace(L, 4);
    Label_6: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 100);
    lua_setfield(L, -2, "x");
    lua_pop(L, 1);
    Label_7: /* SETLIST */
    {
        int n = 3;
        if (n == 0) n = lua_gettop(L) - 1;
        lua_pushvalue(L, 1); /* table */
        for (int j = 1; j <= n; j++) {
            lua_pushvalue(L, 1 + j);
            lua_seti(L, -2, -50 + j);
        }
        lua_pop(L, 1);
    }
    Label_8: /* GETTABUP */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushlstring(L, "print", 5);
    lua_gettable(L, -2);
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_9: /* LOADK */
    lua_pushlstring(L, "t[1]=", 5);
    lua_replace(L, 3);
    Label_10: /* GETTABUP */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushlstring(L, "tostring", 8);
    lua_gettable(L, -2);
    lua_replace(L, 4);
    lua_pop(L, 1);
    Label_11: /* GETI */
    lua_pushvalue(L, 1);
    lua_geti(L, -1, 1);
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_12: /* CALL */
    lua_pushvalue(L, 4); /* func */
    lua_pushvalue(L, 5); /* arg 0 */
    lua_call(L, 1, 1);
    lua_replace(L, 4);
    Label_13: /* CONCAT */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    lua_concat(L, 2);
    lua_replace(L, 3);
    Label_14: /* CALL */
    lua_pushvalue(L, 2); /* func */
    lua_pushvalue(L, 3); /* arg 0 */
    lua_call(L, 1, 0);
    Label_15: /* GETTABUP */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushlstring(L, "print", 5);
    lua_gettable(L, -2);
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_16: /* LOADK */
    lua_pushlstring(L, "t.x=", 4);
    lua_replace(L, 3);
    Label_17: /* GETTABUP */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushlstring(L, "tostring", 8);
    lua_gettable(L, -2);
    lua_replace(L, 4);
    lua_pop(L, 1);
    Label_18: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "x");
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_19: /* CALL */
    lua_pushvalue(L, 4); /* func */
    lua_pushvalue(L, 5); /* arg 0 */
    lua_call(L, 1, 1);
    lua_replace(L, 4);
    Label_20: /* CONCAT */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    lua_concat(L, 2);
    lua_replace(L, 3);
    Label_21: /* CALL */
    lua_pushvalue(L, 2); /* func */
    lua_pushvalue(L, 3); /* arg 0 */
    lua_call(L, 1, 0);
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
