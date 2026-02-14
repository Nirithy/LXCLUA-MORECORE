#define lsuper_c
#define LUA_CORE

#include "lprefix.h"
#include "lua.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "lsuper.h"
#include "lstring.h"
#include <stdio.h>
#include <string.h>
#include "ltable.h"
#include "lvm.h"

SuperStruct *luaS_newsuperstruct (lua_State *L, TString *name, unsigned int size) {
  SuperStruct *ss = (SuperStruct *)luaC_newobj(L, LUA_TSUPERSTRUCT, sizeof(SuperStruct));
  ss->name = name;
  ss->nsize = size;
  ss->data = NULL;
  if (size > 0) {
    ss->data = luaM_newvector(L, size * 2, TValue);
    for (unsigned int i = 0; i < size * 2; i++) {
      setnilvalue(&ss->data[i]);
    }
  }
  return ss;
}

void luaS_freesuperstruct (lua_State *L, SuperStruct *ss) {
  if (ss->data)
    luaM_freearray(L, ss->data, ss->nsize * 2);
  luaM_free(L, ss);
}

void luaS_setsuperstruct (lua_State *L, SuperStruct *ss, TValue *key, TValue *val) {
  /* Linear search */
  if (ss->data) {
    for (unsigned int i = 0; i < ss->nsize; i++) {
      TValue *k = &ss->data[i * 2];
      if (luaV_equalobj(NULL, k, key)) {
        setobj2t(L, &ss->data[i * 2 + 1], val);
        return;
      }
    }
  }
  /* Not found, append */
  unsigned int oldsize = ss->nsize;
  unsigned int newsize = oldsize + 1;
  ss->data = luaM_reallocvector(L, ss->data, oldsize * 2, newsize * 2, TValue);
  ss->nsize = newsize;
  setobj2t(L, &ss->data[oldsize * 2], key);
  setobj2t(L, &ss->data[oldsize * 2 + 1], val);
}

const TValue *luaS_getsuperstruct (SuperStruct *ss, TValue *key) {
  for (unsigned int i = 0; i < ss->nsize; i++) {
    TValue *k = &ss->data[i * 2];
    if (luaV_equalobj(NULL, k, key)) {
      return &ss->data[i * 2 + 1];
    }
  }
  return NULL; /* Not found */
}

const TValue *luaS_getsuperstruct_str (SuperStruct *ss, TString *key) {
  for (unsigned int i = 0; i < ss->nsize; i++) {
    TValue *k = &ss->data[i * 2];
    if (ttisstring(k)) {
        if (ttisshrstring(k) && eqshrstr(tsvalue(k), key)) {
            return &ss->data[i * 2 + 1];
        }
        if (ttislngstring(k) && luaS_eqlngstr(tsvalue(k), key)) {
            return &ss->data[i * 2 + 1];
        }
        /* Handle mixed short/long comparison (shouldn't happen if key is short) */
        if (strcmp(getstr(tsvalue(k)), getstr(key)) == 0) {
            return &ss->data[i * 2 + 1];
        }
    }
  }
  return NULL; /* Not found */
}
