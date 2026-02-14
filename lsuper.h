#ifndef lsuper_h
#define lsuper_h

#include "lobject.h"

LUAI_FUNC SuperStruct *luaS_newsuperstruct (lua_State *L, TString *name, unsigned int size);
LUAI_FUNC void luaS_setsuperstruct (lua_State *L, SuperStruct *ss, TValue *key, TValue *val);
LUAI_FUNC const TValue *luaS_getsuperstruct (SuperStruct *ss, TValue *key);
LUAI_FUNC const TValue *luaS_getsuperstruct_str (SuperStruct *ss, TString *key);
LUAI_FUNC void luaS_freesuperstruct (lua_State *L, SuperStruct *ss);

#endif
