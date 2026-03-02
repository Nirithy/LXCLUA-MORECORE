#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 10;
    lua_tcc_prologue(L, 0, 9);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* LOADI */
    lua_tcc_loadk_int(L, 1, 0);
    Label_3: /* LOADI */
    lua_tcc_loadk_int(L, 2, 1);
    Label_4: /* LOADI */
    lua_tcc_loadk_int(L, 3, 10);
    Label_5: /* LOADI */
    lua_tcc_loadk_int(L, 4, 1);
    Label_6: /* FORPREP */
    {
        if (lua_isinteger(L, 2) && lua_isinteger(L, 4)) {
            lua_Integer step = lua_tointeger(L, 4);
            lua_Integer init = lua_tointeger(L, 2);
            lua_pushinteger(L, init - step);
            lua_replace(L, 2);
        } else {
            lua_Number step = lua_tonumber(L, 4);
            lua_Number init = lua_tonumber(L, 2);
            lua_pushnumber(L, init - step);
            lua_replace(L, 2);
        }
        goto Label_21;
    }
    Label_7: /* MODK */
    lua_pushvalue(L, 5);
    lua_pushinteger(L, 2);
    lua_arith(L, 3);
    lua_replace(L, 6);
    Label_8: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_9: /* EQI */
    {
        lua_pushvalue(L, 6);
        lua_pushinteger(L, 0);
        int res = lua_compare(L, -2, -1, 0);
        lua_pop(L, 2);
        if (res != 0) goto Label_11;
    }
    Label_10: /* JMP */
    goto Label_21;
    Label_11: /* LOADI */
    lua_tcc_loadk_int(L, 6, 1);
    Label_12: /* LOADI */
    lua_tcc_loadk_int(L, 7, 5);
    Label_13: /* LOADI */
    lua_tcc_loadk_int(L, 8, 1);
    Label_14: /* FORPREP */
    {
        if (lua_isinteger(L, 6) && lua_isinteger(L, 8)) {
            lua_Integer step = lua_tointeger(L, 8);
            lua_Integer init = lua_tointeger(L, 6);
            lua_pushinteger(L, init - step);
            lua_replace(L, 6);
        } else {
            lua_Number step = lua_tonumber(L, 8);
            lua_Number init = lua_tonumber(L, 6);
            lua_pushnumber(L, init - step);
            lua_replace(L, 6);
        }
        goto Label_19;
    }
    Label_15: /* EQI */
    {
        lua_pushvalue(L, 9);
        lua_pushinteger(L, 3);
        int res = lua_compare(L, -2, -1, 0);
        lua_pop(L, 2);
        if (res != 1) goto Label_17;
    }
    Label_16: /* JMP */
    goto Label_21;
    Label_17: /* ADDI */
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 1);
    lua_arith(L, 0);
    lua_replace(L, 1);
    Label_18: /* MMBINI */
    /* MMBIN: ignored as lua_arith handles it */
    Label_19: /* FORLOOP */
    {
        if (lua_isinteger(L, 8)) {
            lua_Integer step = lua_tointeger(L, 8);
            lua_Integer limit = lua_tointeger(L, 7);
            lua_Integer idx = lua_tointeger(L, 6) + step;
            lua_pushinteger(L, idx);
            lua_replace(L, 6);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushinteger(L, idx);
                lua_replace(L, 9);
                goto Label_15;
            }
        } else {
            lua_Number step = lua_tonumber(L, 8);
            lua_Number limit = lua_tonumber(L, 7);
            lua_Number idx = lua_tonumber(L, 6) + step;
            lua_pushnumber(L, idx);
            lua_replace(L, 6);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushnumber(L, idx);
                lua_replace(L, 9);
                goto Label_15;
            }
        }
    }
    Label_20: /* JMP */
    goto Label_21;
    Label_21: /* FORLOOP */
    {
        if (lua_isinteger(L, 4)) {
            lua_Integer step = lua_tointeger(L, 4);
            lua_Integer limit = lua_tointeger(L, 3);
            lua_Integer idx = lua_tointeger(L, 2) + step;
            lua_pushinteger(L, idx);
            lua_replace(L, 2);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushinteger(L, idx);
                lua_replace(L, 5);
                goto Label_7;
            }
        } else {
            lua_Number step = lua_tonumber(L, 4);
            lua_Number limit = lua_tonumber(L, 3);
            lua_Number idx = lua_tonumber(L, 2) + step;
            lua_pushnumber(L, idx);
            lua_replace(L, 2);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushnumber(L, idx);
                lua_replace(L, 5);
                goto Label_7;
            }
        }
    }
    Label_22: /* RETURN */
    lua_tcc_push_args(L, 1, 1);
    return 1;
    Label_23: /* RETURN */
    return 0;
}

int luaopen_test_nested(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
