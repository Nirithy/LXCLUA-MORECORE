/*
** $Id: lnamespace.h $
** Namespace Implementation
** See Copyright Notice in lua.h
*/

#ifndef lnamespace_h
#define lnamespace_h

#include "lobject.h"

LUAI_FUNC Namespace *luaN_new (lua_State *L, TString *name);
LUAI_FUNC void luaN_free (lua_State *L, Namespace *ns);

#endif
