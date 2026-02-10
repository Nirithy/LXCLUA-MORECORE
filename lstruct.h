/*
** $Id: lstruct.h $
** Struct support
*/

#ifndef lstruct_h
#define lstruct_h

#include "lua.h"
#include "lobject.h"

LUAI_FUNC void luaS_copystruct (lua_State *L, TValue *dest, const TValue *src);
LUAI_FUNC int luaopen_struct (lua_State *L);
LUAI_FUNC void luaS_structindex (lua_State *L, const TValue *t, TValue *key, StkId val);
LUAI_FUNC void luaS_structnewindex (lua_State *L, const TValue *t, TValue *key, TValue *val);
LUAI_FUNC int luaS_structeq (const TValue *t1, const TValue *t2);

#endif
