#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);
static int function_2(lua_State *L);
static int function_3(lua_State *L);
static int function_4(lua_State *L);

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
    Label_3: /* RETURN */
    lua_tcc_push_args(L, 1, 1);
    return 1;
    Label_4: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 6); /* Max Stack Size */
    Label_1: /* NEWTABLE */
    lua_createtable(L, 0, 0);
    lua_replace(L, 2);
    Label_2: /* EXTRAARG */
    /* EXTRAARG */
    Label_3: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushvalue(L, 2); /* upval 1 (local) */
    lua_pushcclosure(L, function_2, 2);
    lua_replace(L, 3);
    Label_4: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 4);
    Label_5: /* JMP */
    goto Label_6;
    Label_6: /* EQI */
    {
        lua_pushvalue(L, 4);
        lua_pushinteger(L, 1);
        int res = lua_compare(L, -2, -1, 0);
        lua_pop(L, 2);
        if (res != 0) goto Label_8;
    }
    Label_7: /* JMP */
    goto Label_13;
    Label_8: /* JMP */
    goto Label_10;
    Label_9: /* JMP */
    goto Label_13;
    Label_10: /* CLOSURE */
    lua_pushvalue(L, 3); /* upval 0 (local) */
    lua_pushcclosure(L, function_3, 1);
    lua_replace(L, 5);
    Label_11: /* TBC */
    lua_toclose(L, 5);
    Label_12: /* JMP */
    goto Label_17;
    Label_13: /* EQI */
    {
        lua_pushvalue(L, 4);
        lua_pushinteger(L, 2);
        int res = lua_compare(L, -2, -1, 0);
        lua_pop(L, 2);
        if (res != 0) goto Label_15;
    }
    Label_14: /* JMP */
    goto Label_20;
    Label_15: /* JMP */
    goto Label_17;
    Label_16: /* JMP */
    goto Label_20;
    Label_17: /* CLOSURE */
    lua_pushvalue(L, 3); /* upval 0 (local) */
    lua_pushcclosure(L, function_4, 1);
    lua_replace(L, 6);
    Label_18: /* TBC */
    lua_toclose(L, 6);
    Label_19: /* JMP */
    goto Label_20;
    Label_20: /* CLOSE */
    lua_closeslot(L, 4);
    Label_21: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "table", 4);
    Label_22: /* GETFIELD */
    lua_pushvalue(L, 4);
    lua_getfield(L, -1, "concat");
    lua_replace(L, 4);
    lua_pop(L, 1);
    Label_23: /* MOVE */
    lua_pushvalue(L, 2);
    lua_replace(L, 5);
    Label_24: /* LOADK */
    lua_tcc_loadk_str(L, 6, ",");
    Label_25: /* TAILCALL */
    lua_tcc_push_args(L, 4, 3); /* func + args */
    lua_call(L, 2, -1);
    return lua_gettop(L) - 6;
    Label_26: /* RETURN */
    return lua_gettop(L) - 3;
    Label_27: /* RETURN */
    return 0;
}

/* Proto 2 */
static int function_2(lua_State *L) {
    lua_settop(L, 4); /* Max Stack Size */
    Label_1: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "table", 2);
    Label_2: /* GETFIELD */
    lua_pushvalue(L, 2);
    lua_getfield(L, -1, "insert");
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_3: /* GETUPVAL */
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_replace(L, 3);
    Label_4: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 4);
    Label_5: /* CALL */
    {
    lua_tcc_push_args(L, 2, 3); /* func + args */
    lua_call(L, 2, 0);
    lua_tcc_store_results(L, 2, 0);
    }
    Label_6: /* RETURN0 */
    return 0;
}

/* Proto 3 */
static int function_3(lua_State *L) {
    lua_settop(L, 2); /* Max Stack Size */
    Label_1: /* GETUPVAL */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_replace(L, 1);
    Label_2: /* LOADK */
    lua_tcc_loadk_str(L, 2, "d1");
    Label_3: /* CALL */
    {
    lua_tcc_push_args(L, 1, 2); /* func + args */
    lua_call(L, 1, 0);
    lua_tcc_store_results(L, 1, 0);
    }
    Label_4: /* RETURN0 */
    return 0;
}

/* Proto 4 */
static int function_4(lua_State *L) {
    lua_settop(L, 2); /* Max Stack Size */
    Label_1: /* GETUPVAL */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_replace(L, 1);
    Label_2: /* LOADK */
    lua_tcc_loadk_str(L, 2, "d2");
    Label_3: /* CALL */
    {
    lua_tcc_push_args(L, 1, 2); /* func + args */
    lua_call(L, 1, 0);
    lua_tcc_store_results(L, 1, 0);
    }
    Label_4: /* RETURN0 */
    return 0;
}

int luaopen_test_tcc_switch_mod(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
