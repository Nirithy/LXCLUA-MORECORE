#include "lua.h"
#include "lauxlib.h"
#include <string.h>
#include <math.h>

static int function_0(lua_State *L);

/* Proto 0 */
static int function_0(lua_State *L) {
    int vtab_idx = 4;
    lua_tcc_prologue(L, 0, 3);
    Label_1: /* VARARGPREP */
    /* VARARGPREP: adjust varargs if needed */
    Label_2: /* LOADI */
    lua_tcc_loadk_int(L, 1, 10);
    Label_3: /* LOADI */
    lua_tcc_loadk_int(L, 2, 20);
    Label_4: /* ADD */
    lua_pushnumber(L, (lua_Number)lua_tonumber(L, 1) + (lua_Number)lua_tonumber(L, 2));
    lua_replace(L, 3);
    Label_5: /* MMBIN */
    /* MMBIN: ignored as lua_arith handles it */
    Label_6: /* MULK */
    lua_pushnumber(L, (lua_Number)lua_tonumber(L, 3) * (lua_Number)2);
    lua_replace(L, 3);
    Label_7: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_8: /* DIVK */
    lua_pushnumber(L, (lua_Number)lua_tonumber(L, 3) / (lua_Number)5);
    lua_replace(L, 3);
    Label_9: /* MMBINK */
    /* MMBIN: ignored as lua_arith handles it */
    Label_10: /* RETURN */
    lua_tcc_push_args(L, 3, 1);
    return 1;
    Label_11: /* RETURN */
    return 0;
}

int luaopen_test_pure_c(lua_State *L) {
    lua_pushglobaltable(L);
    lua_pushcclosure(L, function_0, 1);
    lua_call(L, 0, 1);
    return 1;
}
