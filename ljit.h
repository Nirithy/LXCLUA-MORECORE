#ifndef ljit_h
#define ljit_h

#include "lua.h"
#include "lobject.h"

void luaJ_compile(lua_State *L, Proto *p);
void luaJ_freeproto(Proto *p);

/* Helper functions for JIT compiled code */
void luaJ_prep_return0(lua_State *L, CallInfo *ci);
void luaJ_prep_return1(lua_State *L, CallInfo *ci, int ra);

#endif
