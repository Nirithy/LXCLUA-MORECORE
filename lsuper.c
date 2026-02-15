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
#include "ldebug.h"

static int super_compare(const TValue *k1, const TValue *k2) {
  int t1 = ttype(k1);
  int t2 = ttype(k2);
  if (t1 != t2) return t1 - t2;

  switch (t1) {
    case LUA_TNIL: return 0;
    case LUA_TBOOLEAN: return (ttistrue(k1) ? 1 : 0) - (ttistrue(k2) ? 1 : 0);
    case LUA_TNUMBER: {
        lua_Number n1, n2;
        if (ttisinteger(k1) && ttisinteger(k2)) {
            lua_Integer i1 = ivalue(k1);
            lua_Integer i2 = ivalue(k2);
            return (i1 < i2) ? -1 : (i1 > i2 ? 1 : 0);
        }
        if (!tonumber(k1, &n1) || !tonumber(k2, &n2)) {
             return 0;
        }
        return (n1 < n2) ? -1 : (n1 > n2 ? 1 : 0);
    }
    case LUA_TSTRING: {
        return strcmp(getstr(tsvalue(k1)), getstr(tsvalue(k2)));
    }
    default: {
        if (iscollectable(k1)) {
             return (gcvalue(k1) < gcvalue(k2)) ? -1 : ((gcvalue(k1) > gcvalue(k2)) ? 1 : 0);
        }
        return 0;
    }
  }
  return 0;
}

SuperStruct *luaS_newsuperstruct (lua_State *L, TString *name, unsigned int size) {
  SuperStruct *ss = (SuperStruct *)luaC_newobj(L, LUA_TSUPERSTRUCT, sizeof(SuperStruct));
  ss->name = name;
  ss->nsize = 0;
  ss->ncapacity = size > 0 ? size : 4;
  ss->data = luaM_newvector(L, ss->ncapacity * 2, TValue);
  return ss;
}

void luaS_freesuperstruct (lua_State *L, SuperStruct *ss) {
  if (ss->data)
    luaM_freearray(L, ss->data, ss->ncapacity * 2);
  luaM_free(L, ss);
}

void luaS_setsuperstruct (lua_State *L, SuperStruct *ss, TValue *key, TValue *val) {
  int left = 0;
  int right = ss->nsize - 1;
  int mid;
  int cmp;

  if (ss->data) {
    while (left <= right) {
      mid = left + (right - left) / 2;
      TValue *k = &ss->data[mid * 2];
      cmp = super_compare(key, k);
      if (cmp == 0) {
        /* Found */
        if (ttisnil(val)) {
           /* Delete by shifting */
           int nmove = ss->nsize - 1 - mid;
           if (nmove > 0) {
              memmove(&ss->data[mid * 2], &ss->data[(mid + 1) * 2], nmove * 2 * sizeof(TValue));
           }
           ss->nsize--;
           /* Optional: shrink capacity if too small? */
        } else {
           setobj2t(L, &ss->data[mid * 2 + 1], val);
        }
        return;
      }
      if (cmp < 0) {
        right = mid - 1;
      } else {
        left = mid + 1;
      }
    }
  }

  /* Not found, insert at 'left' */
  if (ttisnil(val)) return;

  if (ss->nsize >= ss->ncapacity) {
    unsigned int oldcapacity = ss->ncapacity;
    unsigned int newcapacity = oldcapacity > 0 ? oldcapacity * 2 : 4;
    ss->data = luaM_reallocvector(L, ss->data, oldcapacity * 2, newcapacity * 2, TValue);
    ss->ncapacity = newcapacity;
  }

  /* Shift to right */
  int nmove = ss->nsize - left;
  if (nmove > 0) {
    memmove(&ss->data[(left + 1) * 2], &ss->data[left * 2], nmove * 2 * sizeof(TValue));
  }

  setobj2t(L, &ss->data[left * 2], key);
  setobj2t(L, &ss->data[left * 2 + 1], val);
  ss->nsize++;
}

const TValue *luaS_getsuperstruct (SuperStruct *ss, TValue *key) {
  int left = 0;
  int right = ss->nsize - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    TValue *k = &ss->data[mid * 2];
    int cmp = super_compare(key, k);
    if (cmp == 0) return &ss->data[mid * 2 + 1];
    if (cmp < 0) right = mid - 1;
    else left = mid + 1;
  }
  return NULL;
}

const TValue *luaS_getsuperstruct_str (SuperStruct *ss, TString *key) {
  TValue k;
  setsvalue(NULL, &k, key);
  return luaS_getsuperstruct(ss, &k);
}

int luaS_next (lua_State *L, SuperStruct *ss, StkId key) {
  unsigned int i = 0;
  if (!ttisnil(s2v(key))) {
    /* Binary search for key */
    int left = 0;
    int right = ss->nsize - 1;
    int mid;
    int cmp;
    int found = 0;

    while (left <= right) {
      mid = left + (right - left) / 2;
      TValue *k = &ss->data[mid * 2];
      cmp = super_compare(s2v(key), k);
      if (cmp == 0) {
        i = mid + 1;
        found = 1;
        break;
      }
      if (cmp < 0) right = mid - 1;
      else left = mid + 1;
    }
    if (!found) {
      luaG_runerror(L, "invalid key to 'next'");
    }
  }

  if (i < ss->nsize) {
    setobj2s(L, key, &ss->data[i * 2]);
    setobj2s(L, key + 1, &ss->data[i * 2 + 1]);
    return 1;
  }
  return 0;
}
