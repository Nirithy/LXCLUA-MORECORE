#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);
static int function_2(lua_State *L);
static int function_3(lua_State *L);
static int function_4(lua_State *L);
static int function_5(lua_State *L);
static int function_6(lua_State *L);
static int function_7(lua_State *L);
static int function_8(lua_State *L);
static int function_9(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 4;
    lua_tcc_prologue(L, 0, 3);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushcclosure(L, function_1, 0);
    lua_replace(L, 1);
    Label_3: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "test_math", -1);
    Label_4: /* CLOSURE */
    lua_pushcclosure(L, function_2, 0);
    lua_replace(L, 1);
    Label_5: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "test_cmp", -1);
    Label_6: /* CLOSURE */
    lua_pushcclosure(L, function_3, 0);
    lua_replace(L, 1);
    Label_7: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "test_vararg", -1);
    Label_8: /* CLOSURE */
    lua_pushcclosure(L, function_4, 0);
    lua_replace(L, 1);
    Label_9: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "test_table", -1);
    Label_10: /* NEWCLASS */
    lua_pushlstring(L, "Point", 5);
    lua_newclass(L, lua_tostring(L, -1));
    lua_replace(L, 1);
    lua_pop(L, 1);
    Label_11: /* LOADI */
    lua_tcc_loadk_int(L, 2, 0);
    Label_12: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__statics");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_13: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "x");
    lua_pop(L, 1);
    Label_14: /* LOADI */
    lua_tcc_loadk_int(L, 2, 0);
    Label_15: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__statics");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_16: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "y");
    lua_pop(L, 1);
    Label_17: /* CLOSURE */
    lua_pushcclosure(L, function_5, 0);
    lua_replace(L, 2);
    Label_18: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__methods");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_19: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "__init__");
    lua_pop(L, 1);
    Label_20: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushcclosure(L, function_6, 1);
    lua_replace(L, 2);
    Label_21: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__methods");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_22: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "add");
    lua_pop(L, 1);
    Label_23: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "Point", -1);
    Label_24: /* NEWCLASS */
    lua_pushlstring(L, "Point3D", 7);
    lua_newclass(L, lua_tostring(L, -1));
    lua_replace(L, 1);
    lua_pop(L, 1);
    Label_25: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "Point", 2);
    Label_26: /* INHERIT */
    lua_inherit(L, 1, 2);
    Label_27: /* LOADI */
    lua_tcc_loadk_int(L, 2, 0);
    Label_28: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__statics");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_29: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "z");
    lua_pop(L, 1);
    Label_30: /* CLOSURE */
    lua_pushcclosure(L, function_7, 0);
    lua_replace(L, 2);
    Label_31: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__methods");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_32: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "__init__");
    lua_pop(L, 1);
    Label_33: /* CLOSURE */
    lua_pushcclosure(L, function_8, 0);
    lua_replace(L, 2);
    Label_34: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "__methods");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_35: /* SETFIELD */
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "get_z");
    lua_pop(L, 1);
    Label_36: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "Point3D", -1);
    Label_37: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushcclosure(L, function_9, 1);
    lua_replace(L, 1);
    Label_38: /* SETTABUP */
    lua_pushvalue(L, 1);
    lua_tcc_settabup(L, 1, "test_oo", -1);
    Label_39: /* NEWTABLE */
    lua_createtable(L, 0, 8);
    lua_replace(L, 1);
    Label_40: /* EXTRAARG */
    /* EXTRAARG */
    Label_41: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "test_math", 2);
    Label_42: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "test_math");
    lua_pop(L, 1);
    Label_43: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "test_cmp", 2);
    Label_44: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "test_cmp");
    lua_pop(L, 1);
    Label_45: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "test_vararg", 2);
    Label_46: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "test_vararg");
    lua_pop(L, 1);
    Label_47: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "test_table", 2);
    Label_48: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "test_table");
    lua_pop(L, 1);
    Label_49: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "test_oo", 2);
    Label_50: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "test_oo");
    lua_pop(L, 1);
    Label_51: /* RETURN */
    lua_tcc_push_args(L, 1, 1);
    return 1;
    Label_52: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    lua_settop(L, 3); /* Max Stack Size */
    Label_1: /* ADD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_arith(L, 0);
    lua_replace(L, 3);
    Label_2: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_3: /* MULK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 2);
    lua_arith(L, 2);
    lua_replace(L, 3);
    Label_4: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_5: /* ADDI */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, -5);
    lua_arith(L, 0);
    lua_replace(L, 3);
    Label_6: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_7: /* DIVK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 2);
    lua_arith(L, 5);
    lua_replace(L, 3);
    Label_8: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_9: /* IDIVK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 1);
    lua_arith(L, 6);
    lua_replace(L, 3);
    Label_10: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_11: /* MODK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 4);
    lua_arith(L, 3);
    lua_replace(L, 3);
    Label_12: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_13: /* POWK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 3);
    lua_arith(L, 4);
    lua_replace(L, 3);
    Label_14: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_15: /* BANDK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 255);
    lua_arith(L, 7);
    lua_replace(L, 3);
    Label_16: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_17: /* BORK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 15);
    lua_arith(L, 8);
    lua_replace(L, 3);
    Label_18: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_19: /* BXORK */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 170);
    lua_arith(L, 9);
    lua_replace(L, 3);
    Label_20: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_21: /* SHLI */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, -1);
    lua_arith(L, 11);
    lua_replace(L, 3);
    Label_22: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_23: /* SHLI */
    lua_pushvalue(L, 3);
    lua_pushinteger(L, 1);
    lua_arith(L, 11);
    lua_replace(L, 3);
    Label_24: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_25: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_26: /* RETURN0 */
    return 0;
}

/* Proto 2 */
static int function_2(lua_State *L) {
    lua_settop(L, 3); /* Max Stack Size */
    Label_1: /* LT */
    {
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 1);
        int res = lua_compare(L, -2, -1, 1);
        lua_pop(L, 2);
        if (res != 0) goto Label_3;
    }
    Label_2: /* JMP */
    goto Label_5;
    Label_3: /* LOADI */
    lua_tcc_loadk_int(L, 3, 1);
    Label_4: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_5: /* LT */
    {
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        int res = lua_compare(L, -2, -1, 1);
        lua_pop(L, 2);
        if (res != 0) goto Label_7;
    }
    Label_6: /* JMP */
    goto Label_9;
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 3, -1);
    Label_8: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_9: /* LE */
    {
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 1);
        int res = lua_compare(L, -2, -1, 2);
        lua_pop(L, 2);
        if (res != 0) goto Label_11;
    }
    Label_10: /* JMP */
    goto Label_13;
    Label_11: /* LOADI */
    lua_tcc_loadk_int(L, 3, 2);
    Label_12: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_13: /* LE */
    {
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        int res = lua_compare(L, -2, -1, 2);
        lua_pop(L, 2);
        if (res != 0) goto Label_15;
    }
    Label_14: /* JMP */
    goto Label_17;
    Label_15: /* LOADI */
    lua_tcc_loadk_int(L, 3, -2);
    Label_16: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_17: /* EQ */
    {
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 2);
        int res = lua_compare(L, -2, -1, 0);
        lua_pop(L, 2);
        if (res != 0) goto Label_19;
    }
    Label_18: /* JMP */
    goto Label_21;
    Label_19: /* LOADI */
    lua_tcc_loadk_int(L, 3, 0);
    Label_20: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_21: /* LOADI */
    lua_tcc_loadk_int(L, 3, 99);
    Label_22: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_23: /* RETURN0 */
    return 0;
}

/* Proto 3 */
static int function_3(lua_State *L) {
    int vtab_idx = 7;
    lua_tcc_prologue(L, 0, 6);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* VARARG */
    if (1 + 3 >= vtab_idx) {
        lua_settop(L, 1 + 3);
        lua_pushvalue(L, vtab_idx);
        lua_replace(L, 1 + 3);
        vtab_idx = 1 + 3;
    }
    for (int i=0; i<3; i++) {
        lua_rawgeti(L, vtab_idx, i+1);
        lua_replace(L, 1 + i);
    }
    Label_3: /* MOVE */
    lua_pushvalue(L, 3);
    lua_replace(L, 4);
    Label_4: /* MOVE */
    lua_pushvalue(L, 2);
    lua_replace(L, 5);
    Label_5: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 6);
    Label_6: /* RETURN */
    lua_tcc_push_args(L, 4, 3);
    return 3;
    Label_7: /* RETURN */
    return 0;
}

/* Proto 4 */
static int function_4(lua_State *L) {
    lua_settop(L, 4); /* Max Stack Size */
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
    Label_8: /* GETI */
    lua_pushvalue(L, 1);
    lua_geti(L, -1, 1);
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_9: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "x");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_10: /* ADD */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_arith(L, 0);
    lua_replace(L, 2);
    Label_11: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_12: /* RETURN1 */
    lua_pushvalue(L, 2);
    return 1;
    Label_13: /* RETURN0 */
    return 0;
}

/* Proto 5 */
static int function_5(lua_State *L) {
    lua_settop(L, 3); /* Max Stack Size */
    Label_1: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "x");
    lua_pop(L, 1);
    Label_2: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, "y");
    lua_pop(L, 1);
    Label_3: /* RETURN0 */
    return 0;
}

/* Proto 6 */
static int function_6(lua_State *L) {
    lua_settop(L, 6); /* Max Stack Size */
    Label_1: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "Point", 3);
    Label_2: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "x");
    lua_replace(L, 4);
    lua_pop(L, 1);
    Label_3: /* GETFIELD */
    lua_pushvalue(L, 2);
    lua_getfield(L, -1, "x");
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_4: /* ADD */
    lua_pushvalue(L, 4);
    lua_pushvalue(L, 5);
    lua_arith(L, 0);
    lua_replace(L, 4);
    Label_5: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_6: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "y");
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_7: /* GETFIELD */
    lua_pushvalue(L, 2);
    lua_getfield(L, -1, "y");
    lua_replace(L, 6);
    lua_pop(L, 1);
    Label_8: /* ADD */
    lua_pushvalue(L, 5);
    lua_pushvalue(L, 6);
    lua_arith(L, 0);
    lua_replace(L, 5);
    Label_9: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_10: /* TAILCALL */
    lua_tcc_push_args(L, 3, 3); /* func + args */
    lua_call(L, 2, -1);
    return lua_gettop(L) - 6;
    Label_11: /* RETURN */
    return lua_gettop(L) - 2;
    Label_12: /* RETURN0 */
    return 0;
}

/* Proto 7 */
static int function_7(lua_State *L) {
    lua_settop(L, 8); /* Max Stack Size */
    Label_1: /* GETSUPER */
    lua_pushvalue(L, 1);
    lua_pushlstring(L, "__init__", 8);
    lua_getsuper(L, -2, lua_tostring(L, -1));
    lua_replace(L, 5);
    lua_pop(L, 2);
    Label_2: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 6);
    Label_3: /* MOVE */
    lua_pushvalue(L, 2);
    lua_replace(L, 7);
    Label_4: /* MOVE */
    lua_pushvalue(L, 3);
    lua_replace(L, 8);
    Label_5: /* CALL */
    {
    lua_tcc_push_args(L, 5, 4); /* func + args */
    lua_call(L, 3, 0);
    lua_tcc_store_results(L, 5, 0);
    }
    Label_6: /* SETFIELD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 4);
    lua_setfield(L, -2, "z");
    lua_pop(L, 1);
    Label_7: /* RETURN0 */
    return 0;
}

/* Proto 8 */
static int function_8(lua_State *L) {
    lua_settop(L, 2); /* Max Stack Size */
    Label_1: /* GETFIELD */
    lua_pushvalue(L, 1);
    lua_getfield(L, -1, "z");
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_2: /* RETURN1 */
    lua_pushvalue(L, 2);
    return 1;
    Label_3: /* RETURN0 */
    return 0;
}

/* Proto 9 */
static int function_9(lua_State *L) {
    lua_settop(L, 7); /* Max Stack Size */
    Label_1: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "Point", 1);
    Label_2: /* LOADI */
    lua_tcc_loadk_int(L, 2, 10);
    Label_3: /* LOADI */
    lua_tcc_loadk_int(L, 3, 20);
    Label_4: /* CALL */
    {
    lua_tcc_push_args(L, 1, 3); /* func + args */
    lua_call(L, 2, 1);
    lua_tcc_store_results(L, 1, 1);
    }
    Label_5: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "Point", 2);
    Label_6: /* LOADI */
    lua_tcc_loadk_int(L, 3, 5);
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 4, 5);
    Label_8: /* CALL */
    {
    lua_tcc_push_args(L, 2, 3); /* func + args */
    lua_call(L, 2, 1);
    lua_tcc_store_results(L, 2, 1);
    }
    Label_9: /* SELF */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -1);
    lua_replace(L, 4);
    lua_getfield(L, -1, "add");
    lua_replace(L, 3);
    lua_pop(L, 1);
    Label_10: /* MOVE */
    lua_pushvalue(L, 2);
    lua_replace(L, 5);
    Label_11: /* CALL */
    {
    lua_tcc_push_args(L, 3, 3); /* func + args */
    lua_call(L, 2, 1);
    lua_tcc_store_results(L, 3, 1);
    }
    Label_12: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "Point3D", 4);
    Label_13: /* LOADI */
    lua_tcc_loadk_int(L, 5, 1);
    Label_14: /* LOADI */
    lua_tcc_loadk_int(L, 6, 2);
    Label_15: /* LOADI */
    lua_tcc_loadk_int(L, 7, 3);
    Label_16: /* CALL */
    {
    lua_tcc_push_args(L, 4, 4); /* func + args */
    lua_call(L, 3, 1);
    lua_tcc_store_results(L, 4, 1);
    }
    Label_17: /* GETFIELD */
    lua_pushvalue(L, 3);
    lua_getfield(L, -1, "x");
    lua_replace(L, 5);
    lua_pop(L, 1);
    Label_18: /* GETFIELD */
    lua_pushvalue(L, 3);
    lua_getfield(L, -1, "y");
    lua_replace(L, 6);
    lua_pop(L, 1);
    Label_19: /* ADD */
    lua_pushvalue(L, 5);
    lua_pushvalue(L, 6);
    lua_arith(L, 0);
    lua_replace(L, 5);
    Label_20: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_21: /* SELF */
    lua_pushvalue(L, 4);
    lua_pushvalue(L, -1);
    lua_replace(L, 7);
    lua_getfield(L, -1, "get_z");
    lua_replace(L, 6);
    lua_pop(L, 1);
    Label_22: /* CALL */
    {
    lua_tcc_push_args(L, 6, 2); /* func + args */
    lua_call(L, 1, 1);
    lua_tcc_store_results(L, 6, 1);
    }
    Label_23: /* ADD */
    lua_pushvalue(L, 5);
    lua_pushvalue(L, 6);
    lua_arith(L, 0);
    lua_replace(L, 5);
    Label_24: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_25: /* RETURN1 */
    lua_pushvalue(L, 5);
    return 1;
    Label_26: /* RETURN0 */
    return 0;
}

int luaopen_test_mod_comprehensive(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
