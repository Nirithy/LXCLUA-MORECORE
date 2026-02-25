#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    lua_tcc_prologue(L, 0, 2);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushcclosure(L, function_1, 1);
    lua_replace(L, 1);
    Label_3: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 2);
    Label_4: /* TAILCALL */
    lua_tcc_push_args(L, 2, 1); /* func + args */
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - 3;
    Label_5: /* RETURN */
    return lua_gettop(L) - 1;
    Label_6: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 10); /* Max Stack Size */
    Label_1: /* NEWTABLE */
    lua_createtable(L, 0, 4);
    lua_replace(L, 1);
    Label_2: /* EXTRAARG */
    /* NOP/EXTRAARG */
    Label_3: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "a");
    lua_pop(L, 1);
    Label_4: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "b");
    lua_pop(L, 1);
    Label_5: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 3);
    lua_setfield(L, -2, "c");
    lua_pop(L, 1);
    Label_6: /* LOADI */
    lua_tcc_loadk_int(L, 2, 0);
    Label_7: /* NEWTABLE */
    lua_createtable(L, 0, 0);
    lua_replace(L, 3);
    Label_8: /* EXTRAARG */
    /* NOP/EXTRAARG */
    Label_9: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "pairs", 4);
    Label_10: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 5);
    Label_11: /* CALL */
    {
    lua_tcc_push_args(L, 4, 2); /* func + args */
    lua_call(L, 1, 4);
    lua_tcc_store_results(L, 4, 4);
    }
    Label_12: /* TFORPREP */
    lua_toclose(L, 7);
    goto Label_20;
    Label_13: /* ADDI */
    lua_pushvalue(L, 2);
    lua_pushinteger(L, 1);
    lua_arith(L, LUA_OPADD);
    lua_replace(L, 2);
    Label_14: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_15: /* SETTABLE */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 8);
    lua_pushboolean(L, 1);
    lua_settable(L, -3);
    lua_pop(L, 1);
    Label_16: /* GTI */
    lua_pushinteger(L, 10);
    lua_pushvalue(L, 2);
    if (lua_compare(L, -2, -1, LUA_OPLT) != 0) goto Label_18;
    lua_pop(L, 2);
    Label_17: /* JMP */
    goto Label_20;
    Label_18: /* LOADK */
    lua_tcc_loadk_str(L, 10, "infinite loop");
    Label_19: /* RETURN */
    lua_tcc_push_args(L, 10, 1);
    return 1;
    Label_20: /* TFORCALL */
    lua_pushvalue(L, 4);
    lua_pushvalue(L, 5);
    lua_pushvalue(L, 6);
    lua_call(L, 2, 2);
    lua_replace(L, 9);
    lua_replace(L, 8);
    Label_21: /* TFORLOOP */
    if (!lua_isnil(L, 8)) {
        lua_pushvalue(L, 8);
        lua_replace(L, 6);
        goto Label_13;
    }
    Label_22: /* CLOSE */
    lua_closeslot(L, 4);
    Label_23: /* RETURN */
    lua_tcc_push_args(L, 2, 1);
    return 1;
    Label_24: /* RETURN */
    return 0;
}

int luaopen_repro_forloop(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
