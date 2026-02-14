/*
** $Id: lnamespace.c $
** Namespace Implementation
** See Copyright Notice in lua.h
*/

#define lnamespace_c
#define LUA_CORE

#include "lprefix.h"

#include "lua.h"

#include "lgc.h"
#include "lnamespace.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"

Namespace *luaN_new (lua_State *L, TString *name) {
  GCObject *o = luaC_newobj(L, LUA_VNAMESPACE, sizeof(Namespace));
  Namespace *ns = gco2ns(o);
  ns->name = name;
  ns->data = NULL;
  ns->using_next = NULL;
  ns->using_next = NULL;

  /* Anchor ns on stack to prevent collection during table allocation */
  setnsvalue(L, s2v(L->top.p), ns);
  luaD_inctop(L);

  ns->data = luaH_new(L);
  /* The table is not reachable yet, but ns is anchored.
     We need to ensure barrier if ns was black (it's new so it's white). */

  L->top.p--; /* Unanchor */

  return ns;
}

void luaN_free (lua_State *L, Namespace *ns) {
  luaM_free(L, ns);
}
