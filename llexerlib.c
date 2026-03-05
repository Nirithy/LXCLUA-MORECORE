/*
** $Id: llexerlib.c $
** Lexer library for LXCLUA
** See Copyright Notice in lua.h
*/

#define llexerlib_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "llex.h"
#include "lzio.h"
#include "lstring.h"
#include "lstate.h"
#include "ltable.h"

/*
** Reader function for luaZ_init
*/
typedef struct {
  const char *s;
  size_t size;
} LoadS;

static const char *getS (lua_State *L, void *ud, size_t *size) {
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}

/*
** Lexer state structure to pass to pcall
*/
typedef struct {
  ZIO *z;
  Mbuffer *buff;
  TString *source;
} PCallLexState;

static int protected_lex(lua_State *L) {
    PCallLexState *pls = (PCallLexState *)lua_touserdata(L, 1);
    LexState lexstate;
    memset(&lexstate, 0, sizeof(LexState));

    lexstate.buff = pls->buff;

    /* create table for scanner to avoid collecting/reusing strings */
    lua_newtable(L);
    lexstate.h = hvalue(s2v(L->top.p - 1));

    int firstchar = zgetc(pls->z);

    /* Call the lexer initialization */
    luaX_setinput(L, &lexstate, pls->z, pls->source, firstchar);

    lua_newtable(L); /* Result array table */
    int i = 1;

    int array_idx = lua_gettop(L);
    while (1) {
        luaX_next(&lexstate);
        int token = lexstate.t.token;
        if (token == TK_EOS) {
            break;
        }

        lua_newtable(L); /* Sub-table for this token */
        int token_tbl_idx = lua_gettop(L);

        /* token integer */
        lua_pushinteger(L, token);
        lua_setfield(L, token_tbl_idx, "token");

        /* line number */
        lua_pushinteger(L, lexstate.linenumber);
        lua_setfield(L, token_tbl_idx, "line");

        /* token string representation */
        const char *tok_str = luaX_token2str(&lexstate, token);
        if (tok_str) {
            lua_pushstring(L, tok_str);
            lua_setfield(L, token_tbl_idx, "type");
        }
        /* luaX_token2str might push string onto the stack via luaO_pushfstring */
        lua_settop(L, token_tbl_idx);

        /* semantic info based on token */
        if (token == TK_NAME || token == TK_STRING || token == TK_INTERPSTRING || token == TK_RAWSTRING) {
            if (lexstate.t.seminfo.ts) {
                lua_pushstring(L, getstr(lexstate.t.seminfo.ts));
                lua_setfield(L, token_tbl_idx, "value");
            }
        } else if (token == TK_INT) {
            lua_pushinteger(L, lexstate.t.seminfo.i);
            lua_setfield(L, token_tbl_idx, "value");
        } else if (token == TK_FLT) {
            lua_pushnumber(L, lexstate.t.seminfo.r);
            lua_setfield(L, token_tbl_idx, "value");
        }

        /* Add token to the result array */
        lua_rawseti(L, array_idx, i++);
    }

    return 1; /* Return the array table */
}

static int lexer_lex(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);

    LoadS ls;
    ls.s = s;
    ls.size = l;

    ZIO z;
    luaZ_init(L, &z, getS, &ls);

    /* Anchor source string to avoid GC */
    lua_pushstring(L, "=(lexer)");
    TString *source = tsvalue(s2v(L->top.p - 1));

    /* Initialize Mbuffer before luaX_setinput */
    Mbuffer buff;
    luaZ_initbuffer(L, &buff);

    PCallLexState pls;
    pls.z = &z;
    pls.buff = &buff;
    pls.source = source;

    /* Push light userdata and C function to call in protected mode */
    lua_pushcfunction(L, protected_lex);
    lua_pushlightuserdata(L, &pls);

    int status = lua_pcall(L, 1, 1, 0);

    /* Clean up buffers whether it failed or succeeded */
    luaZ_freebuffer(L, &buff);

    if (status != LUA_OK) {
        /* Error message is at the top of the stack */
        return lua_error(L);
    }

    /* Move the result table below the anchored source string, then pop the source string */
    lua_insert(L, -2);
    lua_pop(L, 1);

    return 1;
}

static const luaL_Reg lexer_lib[] = {
    {"lex", lexer_lex},
    {NULL, NULL}
};

LUAMOD_API int luaopen_lexer(lua_State *L) {
    luaL_newlib(L, lexer_lib);

    /* Register token constants */
    /* Let's just register a few common ones for ease of use, or we can iterate if we want */
    lua_pushinteger(L, TK_NAME);
    lua_setfield(L, -2, "TK_NAME");

    lua_pushinteger(L, TK_STRING);
    lua_setfield(L, -2, "TK_STRING");

    lua_pushinteger(L, TK_INT);
    lua_setfield(L, -2, "TK_INT");

    lua_pushinteger(L, TK_FLT);
    lua_setfield(L, -2, "TK_FLT");

    lua_pushinteger(L, TK_EOS);
    lua_setfield(L, -2, "TK_EOS");

    return 1;
}
