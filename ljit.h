#ifndef ljit_h
#define ljit_h

#include "lua.h"
#include "lobject.h"

void luaJ_compile(lua_State *L, Proto *p);
void luaJ_freeproto(Proto *p);

#endif
