#include "lua.h"
#include "lauxlib.h"
#include <string.h>

static int function_0(lua_State *L);
static int function_1(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    lua_tcc_prologue(L, 0, 4);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* CLOSURE */
    lua_pushcclosure(L, function_1, 0);
    lua_replace(L, 1);
    Label_3: /* GETTABUP */
    lua_tcc_gettabup(L, 1, "print", 2);
    Label_4: /* LOADK */
    lua_tcc_loadk_str(L, 3, "Hello from TCC compiled module!");
    Label_5: /* CALL */
    {
    lua_tcc_push_args(L, 2, 2); /* func + args */
    lua_call(L, 1, 0);
    lua_tcc_store_results(L, 2, 0);
    }
    Label_6: /* MOVE */
    lua_pushvalue(L, 1);
    lua_replace(L, 2);
    Label_7: /* LOADI */
    lua_tcc_loadk_int(L, 3, 10);
    Label_8: /* LOADI */
    lua_tcc_loadk_int(L, 4, 20);
    Label_9: /* TAILCALL */
    lua_tcc_push_args(L, 2, 3); /* func + args */
    lua_call(L, 2, LUA_MULTRET);
    return lua_gettop(L) - 5;
    Label_10: /* RETURN */
    return lua_gettop(L) - 1;
    Label_11: /* RETURN */
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
    Label_3: /* RETURN1 */
    lua_pushvalue(L, 3);
    return 1;
    Label_4: /* RETURN0 */
    return 0;
}

int luaopen_test_mod_tcc(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
