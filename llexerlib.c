/*
** $Id: llexerlib.c $
** Lexer library for LXCLUA
** See Copyright Notice in lua.h
*/

#define llexerlib_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>
#include <ctype.h>

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

static int lexer_token2str(lua_State *L) {
    int token = luaL_checkinteger(L, 1);

    if (token > TK_RAWSTRING || token < 0) {
        lua_pushnil(L);
        return 1;
    }

    if (token < FIRST_RESERVED) {
        /* single-byte symbols */
        char s[2] = {(char)token, '\0'};
        lua_pushstring(L, s);
    } else {
        /* reserved words and other tokens */
        /* We use luaX_token2str with a dummy LexState to get the string representation.
           Some tokens cause luaX_token2str to push the string onto the stack,
           while others just return a const char*. We handle both cases using lua_gettop. */
        LexState dummy_ls;
        memset(&dummy_ls, 0, sizeof(LexState));
        dummy_ls.L = L;

        int top = lua_gettop(L);
        const char *str = luaX_token2str(&dummy_ls, token);
        if (str) {
            if (lua_gettop(L) == top) {
                /* luaX_token2str returned a string but didn't push it */
                lua_pushstring(L, str);
            }
            return 1;
        } else {
            lua_pushnil(L);
            return 1;
        }
    }
    return 1;
}

/*
** Iterator state for gmatch
*/
typedef struct {
  ZIO z;
  Mbuffer buff;
  TString *source;
  LexState lexstate;
  LoadS ls;
  int firstchar_read;
  int done;
} GMatchLexState;

static int gmatch_gc(lua_State *L) {
    GMatchLexState *gstate = (GMatchLexState *)lua_touserdata(L, 1);
    if (gstate) {
        luaZ_freebuffer(L, &gstate->buff);
    }
    return 0;
}

static int protected_gmatch_iter(lua_State *L) {
    GMatchLexState *gstate = (GMatchLexState *)lua_touserdata(L, 1);

    if (gstate->done) {
        return 0;
    }

    if (!gstate->firstchar_read) {
        int firstchar = zgetc(&gstate->z);
        luaX_setinput(L, &gstate->lexstate, &gstate->z, gstate->source, firstchar);
        gstate->firstchar_read = 1;
    }

    luaX_next(&gstate->lexstate);
    int token = gstate->lexstate.t.token;

    if (token == TK_EOS) {
        gstate->done = 1;
        return 0;
    }

    /* return line, token, type, value */
    lua_pushinteger(L, gstate->lexstate.linenumber);
    lua_pushinteger(L, token);

    const char *tok_str = luaX_token2str(&gstate->lexstate, token);
    if (tok_str) {
        lua_pushstring(L, tok_str);
    } else {
        lua_pushnil(L);
    }

    if (token == TK_NAME || token == TK_STRING || token == TK_INTERPSTRING || token == TK_RAWSTRING) {
        if (gstate->lexstate.t.seminfo.ts) {
            lua_pushstring(L, getstr(gstate->lexstate.t.seminfo.ts));
        } else {
            lua_pushnil(L);
        }
    } else if (token == TK_INT) {
        lua_pushinteger(L, gstate->lexstate.t.seminfo.i);
    } else if (token == TK_FLT) {
        lua_pushnumber(L, gstate->lexstate.t.seminfo.r);
    } else {
        lua_pushnil(L);
    }

    return 4;
}

static int gmatch_iter(lua_State *L) {
    GMatchLexState *gstate = (GMatchLexState *)lua_touserdata(L, lua_upvalueindex(1));
    if (gstate->done) return 0;

    int top_before = lua_gettop(L);
    lua_pushcfunction(L, protected_gmatch_iter);
    lua_pushlightuserdata(L, gstate);

    int status = lua_pcall(L, 1, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        gstate->done = 1;
        return lua_error(L);
    }

    return lua_gettop(L) - top_before;
}

static int lexer_gmatch(lua_State *L) {
    size_t l;
    const char *s = luaL_checklstring(L, 1, &l);

        /* Create userdata for the iterator state */
    GMatchLexState *gstate = (GMatchLexState *)lua_newuserdatauv(L, sizeof(GMatchLexState), 0);
    memset(gstate, 0, sizeof(GMatchLexState));

    /* Setup __gc metatable for the userdata */
    if (luaL_newmetatable(L, "lexer_gmatch_state")) {
        lua_pushcfunction(L, gmatch_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);


    gstate->ls.s = s;
    gstate->ls.size = l;

    luaZ_init(L, &gstate->z, getS, &gstate->ls);
    luaZ_initbuffer(L, &gstate->buff);
    gstate->lexstate.buff = &gstate->buff;

    /* create table for scanner to avoid collecting/reusing strings */
    lua_newtable(L);
    gstate->lexstate.h = hvalue(s2v(L->top.p - 1));

    lua_pushstring(L, "=(lexer)");
    gstate->source = tsvalue(s2v(L->top.p - 1));

    /* push the input string to avoid gc */
    lua_pushvalue(L, 1);

    /* We push the C closure with 4 upvalues: gstate userdata, table for strings, source string, input code */
    lua_pushcclosure(L, gmatch_iter, 4);

    return 1;
}

static void addquoted (luaL_Buffer *b, const char *s, size_t len) {
  luaL_addchar(b, '"');
  while (len--) {
    if (*s == '"' || *s == '\\' || *s == '\n') {
      luaL_addchar(b, '\\');
      luaL_addchar(b, *s);
    }
    else if (iscntrl((unsigned char)*s)) {
      char buff[10];
      if (!isdigit((unsigned char)*(s+1)))
        snprintf(buff, sizeof(buff), "\\%d", (int)(unsigned char)*s);
      else
        snprintf(buff, sizeof(buff), "\\%03d", (int)(unsigned char)*s);
      luaL_addstring(b, buff);
    }
    else
      luaL_addchar(b, *s);
    s++;
  }
  luaL_addchar(b, '"');
}

static int lexer_reconstruct(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    int len = luaL_len(L, 1);
    int last_token = 0;

    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, 1, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        lua_getfield(L, -1, "token");
        int token = lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* Add space between identifiers/keywords/numbers to prevent syntax errors */
        if (i > 1) {
            if ((last_token >= FIRST_RESERVED && last_token <= TK_WITH) || last_token == TK_NAME || last_token == TK_INT || last_token == TK_FLT) {
                if ((token >= FIRST_RESERVED && token <= TK_WITH) || token == TK_NAME || token == TK_INT || token == TK_FLT) {
                    luaL_addchar(&b, ' ');
                }
            }
        }

        if (token == TK_NAME || token == TK_STRING || token == TK_INTERPSTRING || token == TK_RAWSTRING || token == TK_INT || token == TK_FLT) {
            lua_getfield(L, -1, "value");
            if (lua_isstring(L, -1) || lua_isnumber(L, -1)) {
                if (token == TK_STRING || token == TK_RAWSTRING || token == TK_INTERPSTRING) {
                    /* Use luaO_pushfstring with %q to properly escape */
                                                            size_t slen;
                    const char *sval = lua_tolstring(L, -1, &slen);
                    addquoted(&b, sval, slen);
                } else {
                    luaL_addstring(&b, lua_tostring(L, -1));
                }
            }
            lua_pop(L, 1);
        } else {
            /* use token2str */
            if (token < FIRST_RESERVED) {
                char s[2] = {(char)token, '\0'};
                luaL_addstring(&b, s);
            } else {
                LexState dummy_ls;
                memset(&dummy_ls, 0, sizeof(LexState));
                dummy_ls.L = L;
                int top = lua_gettop(L);
                const char *str = luaX_token2str(&dummy_ls, token);
                if (str) {
                                        size_t len = strlen(str);
                    if (len >= 2 && str[0] == '\'' && str[len-1] == '\'') {
                        luaL_addlstring(&b, str + 1, len - 2);
                    } else {
                        luaL_addstring(&b, str);
                    }
                    if (lua_gettop(L) > top) {
                        lua_settop(L, top);
                    }
                }
            }
        }

        last_token = token;
        lua_pop(L, 1); /* pop token table */
    }

    luaL_pushresult(&b);
    return 1;
}



static const luaL_Reg lexer_lib[] = {
    {"lex", lexer_lex},
    {"token2str", lexer_token2str},
    {"gmatch", lexer_gmatch},
    {"reconstruct", lexer_reconstruct},
    {NULL, NULL}
};

static int lexer_call(lua_State *L) {
    lua_remove(L, 1); /* remove the module table */
    return lexer_lex(L);
}

#define REG_TK(L, tk) \
  lua_pushinteger(L, tk); \
  lua_setfield(L, -2, #tk)

LUAMOD_API int luaopen_lexer(lua_State *L) {
    luaL_newlib(L, lexer_lib);

    /* set metatable for __call */
    lua_newtable(L);
    lua_pushcfunction(L, lexer_call);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);

    /* Register token constants */
    REG_TK(L, TK_ADDEQ);
    REG_TK(L, TK_AND);
    REG_TK(L, TK_ARROW);
    REG_TK(L, TK_ASM);
    REG_TK(L, TK_ASYNC);
    REG_TK(L, TK_AWAIT);
    REG_TK(L, TK_BANDEQ);
    REG_TK(L, TK_BOOL);
    REG_TK(L, TK_BOREQ);
    REG_TK(L, TK_BREAK);
    REG_TK(L, TK_BXOREQ);
    REG_TK(L, TK_CASE);
    REG_TK(L, TK_CATCH);
    REG_TK(L, TK_CHAR);
    REG_TK(L, TK_COMMAND);
    REG_TK(L, TK_CONCAT);
    REG_TK(L, TK_CONCATEQ);
    REG_TK(L, TK_CONCEPT);
    REG_TK(L, TK_CONST);
    REG_TK(L, TK_CONTINUE);
    REG_TK(L, TK_DBCOLON);
    REG_TK(L, TK_DEFAULT);
    REG_TK(L, TK_DEFER);
    REG_TK(L, TK_DIVEQ);
    REG_TK(L, TK_DO);
    REG_TK(L, TK_DOLLAR);
    REG_TK(L, TK_DOLLDOLL);
    REG_TK(L, TK_DOTS);
    REG_TK(L, TK_DOUBLE);
    REG_TK(L, TK_ELSE);
    REG_TK(L, TK_ELSEIF);
    REG_TK(L, TK_END);
    REG_TK(L, TK_ENUM);
    REG_TK(L, TK_EOS);
    REG_TK(L, TK_EQ);
    REG_TK(L, TK_EXPORT);
    REG_TK(L, TK_FALSE);
    REG_TK(L, TK_FINALLY);
    REG_TK(L, TK_FLT);
    REG_TK(L, TK_FOR);
    REG_TK(L, TK_FUNCTION);
    REG_TK(L, TK_GE);
    REG_TK(L, TK_GLOBAL);
    REG_TK(L, TK_GOTO);
    REG_TK(L, TK_IDIV);
    REG_TK(L, TK_IDIVEQ);
    REG_TK(L, TK_IF);
    REG_TK(L, TK_IN);
    REG_TK(L, TK_INT);
    REG_TK(L, TK_INTERPSTRING);
    REG_TK(L, TK_IS);
    REG_TK(L, TK_KEYWORD);
    REG_TK(L, TK_LAMBDA);
    REG_TK(L, TK_LE);
    REG_TK(L, TK_LET);
    REG_TK(L, TK_LOCAL);
    REG_TK(L, TK_LONG);
    REG_TK(L, TK_MEAN);
    REG_TK(L, TK_MODEQ);
    REG_TK(L, TK_MULEQ);
    REG_TK(L, TK_NAME);
    REG_TK(L, TK_NAMESPACE);
    REG_TK(L, TK_NE);
    REG_TK(L, TK_NIL);
    REG_TK(L, TK_NOT);
    REG_TK(L, TK_NULLCOAL);
    REG_TK(L, TK_OPERATOR);
    REG_TK(L, TK_OPTCHAIN);
    REG_TK(L, TK_OR);
    REG_TK(L, TK_PIPE);
    REG_TK(L, TK_PLUSPLUS);
    REG_TK(L, TK_RAWSTRING);
    REG_TK(L, TK_REPEAT);
    REG_TK(L, TK_REQUIRES);
    REG_TK(L, TK_RETURN);
    REG_TK(L, TK_REVPIPE);
    REG_TK(L, TK_SAFEPIPE);
    REG_TK(L, TK_SHL);
    REG_TK(L, TK_SHLEQ);
    REG_TK(L, TK_SHR);
    REG_TK(L, TK_SHREQ);
    REG_TK(L, TK_SPACESHIP);
    REG_TK(L, TK_STRING);
    REG_TK(L, TK_STRUCT);
    REG_TK(L, TK_SUBEQ);
    REG_TK(L, TK_SUPERSTRUCT);
    REG_TK(L, TK_SWITCH);
    REG_TK(L, TK_TAKE);
    REG_TK(L, TK_THEN);
    REG_TK(L, TK_TRUE);
    REG_TK(L, TK_TRY);
    REG_TK(L, TK_TYPE_FLOAT);
    REG_TK(L, TK_TYPE_INT);
    REG_TK(L, TK_UNTIL);
    REG_TK(L, TK_USING);
    REG_TK(L, TK_VOID);
    REG_TK(L, TK_WALRUS);
    REG_TK(L, TK_WHEN);
    REG_TK(L, TK_WHILE);
    REG_TK(L, TK_WITH);

    return 1;
}
