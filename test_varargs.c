#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 8;
    lua_tcc_prologue(L, 0, 7);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushvalue(L, lua_upvalueindex(1)); /* upval 0 (upval) */
    lua_pushcclosure(L, function_1, 1);
    lua_replace(L, 1);
    Label_3: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 2);
    Label_4: /* LOADI */
    lua_tcc_loadk_int(L, 3, 1);
    Label_5: /* LOADI */
    lua_tcc_loadk_int(L, 4, 2);
    Label_6: /* LOADI */
    lua_tcc_loadk_int(L, 5, 3);
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 6, 4);
    Label_8: /* LOADI */
    lua_tcc_loadk_int(L, 7, 5);
    Label_9: /* TAILCALL */
    lua_tcc_push_args(L, 2, 6); /* func + args */
    lua_call(L, 5, -1);
    return lua_gettop(L) - 8;
    Label_10: /* RETURN */
    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - 1);
    return lua_gettop(L) - 1;
    Label_11: /* RETURN */
    return 0;
}

/* Proto 1 */
static int function_1(lua_State *L) {
    int vtab_idx = 8;
    lua_tcc_prologue(L, 0, 7);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* LOADI */
    lua_tcc_loadk_int(L, 1, 0);
    Label_3: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "table", 2);
    Label_4: /* GETFIELD */
    lua_pushvalue(L, 2);
    lua_getfield(L, -1, "pack");
    lua_replace(L, 2);
    lua_pop(L, 1);
    Label_5: /* VARARG */
    {
        int nvar = (int)lua_rawlen(L, vtab_idx);
        lua_settop(L, 3 + nvar);
        lua_pushvalue(L, vtab_idx);
        lua_replace(L, 3 + nvar);
        vtab_idx = 3 + nvar;
        for (int i=1; i<=nvar; i++) {
            lua_rawgeti(L, vtab_idx, i);
            lua_replace(L, 3 + i - 1);
        }
    }
    Label_6: /* CALL */
    {
    if (vtab_idx == lua_gettop(L)) {
        int r = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_call(L, lua_gettop(L) - 2, 1);
        lua_rawgeti(L, LUA_REGISTRYINDEX, r);
        luaL_unref(L, LUA_REGISTRYINDEX, r);
        vtab_idx = lua_gettop(L);
    } else {
        lua_call(L, lua_gettop(L) - 2, 1);
    }
    lua_pushvalue(L, vtab_idx);
    lua_replace(L, 8);
    vtab_idx = 8;
    lua_settop(L, 8);
    }
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 3, 1);
    Label_8: /* GETFIELD */
    lua_pushvalue(L, 2);
    lua_getfield(L, -1, "n");
    lua_replace(L, 4);
    lua_pop(L, 1);
    Label_9: /* LOADI */
    lua_tcc_loadk_int(L, 5, 1);
    Label_10: /* FORPREP */
    {
        if (lua_isinteger(L, 3) && lua_isinteger(L, 5)) {
            lua_Integer step = lua_tointeger(L, 5);
            lua_Integer init = lua_tointeger(L, 3);
            lua_pushinteger(L, init - step);
            lua_replace(L, 3);
        } else {
            lua_Number step = lua_tonumber(L, 5);
            lua_Number init = lua_tonumber(L, 3);
            lua_pushnumber(L, init - step);
            lua_replace(L, 3);
        }
        goto Label_14;
    }
    Label_11: /* GETTABLE */
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 6);
    lua_gettable(L, -2);
    lua_replace(L, 7);
    lua_pop(L, 1);
    Label_12: /* ADD */
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 7);
    lua_arith(L, 0);
    lua_replace(L, 1);
    Label_13: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_14: /* FORLOOP */
    {
        if (lua_isinteger(L, 5)) {
            lua_Integer step = lua_tointeger(L, 5);
            lua_Integer limit = lua_tointeger(L, 4);
            lua_Integer idx = lua_tointeger(L, 3) + step;
            lua_pushinteger(L, idx);
            lua_replace(L, 3);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushinteger(L, idx);
                lua_replace(L, 6);
                goto Label_11;
            }
        } else {
            lua_Number step = lua_tonumber(L, 5);
            lua_Number limit = lua_tonumber(L, 4);
            lua_Number idx = lua_tonumber(L, 3) + step;
            lua_pushnumber(L, idx);
            lua_replace(L, 3);
            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {
                lua_pushnumber(L, idx);
                lua_replace(L, 6);
                goto Label_11;
            }
        }
    }
    Label_15: /* RETURN */
    lua_tcc_push_args(L, 1, 1);
    return 1;
    Label_16: /* RETURN */
    return 0;
}

int luaopen_test_varargs(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
