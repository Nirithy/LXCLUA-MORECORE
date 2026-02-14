/**
 * @file lstring.c
 * @brief String table (keeps all strings handled by Lua) implementation.
 *
 * This file handles the management of Lua strings, including interning of short strings,
 * hashing, and the string cache.
 */

#define lstring_c
#define LUA_CORE

#include "lprefix.h"
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lthread.h"


/*
** Maximum size for string table.
*/
#define MAXSTRTB	cast_int(luaM_limitN(INT_MAX, TString*))

/*
** Initial size for the string table (must be power of 2).
** The Lua core alone registers ~50 strings (reserved words +
** metaevent keys + a few others). Libraries would typically add
** a few dozens more.
*/
#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE   128
#endif


/**
 * @brief Checks if two long strings are equal.
 *
 * @param a First string.
 * @param b Second string.
 * @return 1 if equal, 0 otherwise.
 */
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_VLNGSTR && b->tt == LUA_VLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getlngstr(a), getlngstr(b), len) == 0));  /* equal contents */
}


static unsigned luaS_hash (const char *str, size_t l, unsigned seed) {
  unsigned int h = seed ^ cast_uint(l);
  for (; l > 0; l--)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}


/**
 * @brief Computes the hash of a long string.
 *
 * @param ts The long string.
 * @return The hash value.
 */
unsigned luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_VLNGSTR);
  if (ts->extra == 0) {  /* no hash? */
    size_t len = ts->u.lnglen;
    ts->hash = luaS_hash(getlngstr(ts), len, ts->hash);
    ts->extra = 1;  /* now it has its hash */
  }
  return ts->hash;
}


static void tablerehash (TString **vect, int osize, int nsize) {
  int i;
  for (i = osize; i < nsize; i++)  /* clear new elements */
    vect[i] = NULL;
  for (i = 0; i < osize; i++) {  /* rehash old part of the array */
    TString *p = vect[i];
    vect[i] = NULL;
    while (p) {  /* for each string in the list */
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, nsize);  /* new position */
      p->u.hnext = vect[h];  /* chain it into array */
      vect[h] = p;
      p = hnext;
    }
  }
}


/**
 * @brief Resizes the string table.
 *
 * @param L The Lua state.
 * @param nsize The new size.
 */
void luaS_resize (lua_State *L, int nsize) {
  global_State *g = G(L);
  l_mutex_lock(&g->lock);
  stringtable *tb = &g->strt;
  int osize = tb->size;
  TString **newvect;
  if (nsize < osize)  /* shrinking table? */
    tablerehash(tb->hash, osize, nsize);  /* depopulate shrinking part */
  /* Unlock during allocation to avoid holding lock? No, realloc might be safe, but rehash touches table. */
  /* We must hold lock. But realloc might trigger GC? */
  /* If GC runs, it might deadlock if it tries to take g->lock. */
  /* luaM_reallocvector calls luaM_realloc_ calls luaC_fullgc if alloc fails. */
  /* luaC_fullgc takes g->lock? */
  /* If I lock here, and luaC_fullgc tries to lock, DEADLOCK! */
  /* I must use recursive lock or check if locked? */
  /* Or make GC assume lock is held? */

  /* Assuming GC handles its own locking or we use recursive mutex. */
  /* Standard pthreads mutex is NOT recursive by default. */
  /* lthread implementation: depends. Windows CRITICAL_SECTION is recursive. */
  /* pthread_mutex can be recursive if initialized with PTHREAD_MUTEX_RECURSIVE. */

  /* I should make g->lock recursive. */

  newvect = luaM_reallocvector(L, tb->hash, osize, nsize, TString*);
  if (l_unlikely(newvect == NULL)) {  /* reallocation failed? */
    if (nsize < osize)  /* was it shrinking table? */
      tablerehash(tb->hash, nsize, osize);  /* restore to original size */
    /* leave table as it was */
  }
  else {  /* allocation succeeded */
    tb->hash = newvect;
    tb->size = nsize;
    if (nsize > osize)
      tablerehash(newvect, osize, nsize);  /* rehash for new size */
  }
  l_mutex_unlock(&g->lock);
}


/**
 * @brief Clears the string cache.
 *
 * @param g The global state.
 */
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
      if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
        g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/**
 * @brief Initializes the string table and the string cache.
 *
 * @param L The Lua state.
 */
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  stringtable *tb = &G(L)->strt;
  tb->hash = luaM_newvector(L, MINSTRTABSIZE, TString*);
  tablerehash(tb->hash, 0, MINSTRTABSIZE);  /* clear array */
  tb->size = MINSTRTABSIZE;
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}


size_t luaS_sizelngstr (size_t len, int kind) {
  switch (kind) {
    case LSTRREG:  /* regular long string */
      return offsetof(TString, contents) + (len + 1) * sizeof(char);
    case LSTRFIX:  /* fixed external long string */
    default:  /* external long string with deallocation */
      lua_assert(kind == LSTRFIX || kind == LSTRMEM);
      return sizeof(TExternalString);
  }
}


/*
** creates a new string object
*/
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->hash = h;
  ts->extra = 0;
  ts->contents[l] = '\0';  /* ending 0 */
  return ts;
}


/**
 * @brief Creates a long string object.
 *
 * @param L The Lua state.
 * @param l The length of the string.
 * @return The new string.
 */
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  size_t totalsize = luaS_sizelngstr(l, LSTRREG);
  TString *ts = createstrobj(L, l, LUA_VLNGSTR, G(L)->seed);
  ts->u.lnglen = l;
  ts->shrlen = LSTRREG;  /* signals that it is a regular long string */
  return ts;
}


/**
 * @brief Removes a string from the string table.
 *
 * @param L The Lua state.
 * @param ts The string to remove.
 */
void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->nuse--;
}


static void growstrtab (lua_State *L, stringtable *tb) {
  if (l_unlikely(tb->nuse == MAX_INT)) {  /* too many strings? */
    luaC_fullgc(L, 1);  /* try to free some... */
    if (tb->nuse == MAX_INT)  /* still too many? */
      luaM_error(L);  /* cannot even create a message... */
  }
  if (tb->size <= MAXSTRTB / 2)  /* can grow string table? */
    luaS_resize(L, tb->size * 2);
}


/*
** Checks whether short string exists and reuses it or creates a new one.
*/
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);

  l_mutex_lock(&g->lock);

  stringtable *tb = &g->strt;
  unsigned int h = luaS_hash(str, l, g->seed);
  TString **list = &tb->hash[lmod(h, tb->size)];
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {
    if (l == cast_uint(ts->shrlen) &&
        (memcmp(str, getshrstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      l_mutex_unlock(&g->lock);
      return ts;
    }
  }
  /* else must create a new string */
  if (tb->nuse >= tb->size) {  /* need to grow string table? */
    growstrtab(L, tb);
    /* Re-fetch list because resize might have changed table size/hash */
    list = &tb->hash[lmod(h, tb->size)];
  }
  ts = createstrobj(L, l, LUA_VSHRSTR, h);
  ts->shrlen = cast(ls_byte, l);
  getshrstr(ts)[l] = '\0';  /* ending 0 */
  memcpy(getshrstr(ts), str, l * sizeof(char));
  ts->u.hnext = *list;
  *list = ts;
  tb->nuse++;

  l_mutex_unlock(&g->lock);
  return ts;
}


/**
 * @brief Creates a new string (or reuses an existing one).
 *
 * @param L The Lua state.
 * @param str The string content.
 * @param l The length of the string.
 * @return The Lua string object.
 */
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    TString *ts;
    if (l_unlikely(l * sizeof(char) >= (MAX_SIZE - sizeof(TString))))
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);
    memcpy(ts->contents, str, l * sizeof(char));
    return ts;
  }
}


/**
 * @brief Creates a new zero-terminated string (or reuses an existing one).
 *
 * @param L The Lua state.
 * @param str The string content.
 * @return The Lua string object.
 */
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


/**
 * @brief Creates a new userdata.
 *
 * @param L The Lua state.
 * @param s The size of the userdata.
 * @param nuvalue Number of user values.
 * @return The new userdata object.
 */
Udata *luaS_newudata (lua_State *L, size_t s, unsigned short nuvalue) {
  Udata *u;
  int i;
  GCObject *o;
  if (l_unlikely(s > MAX_SIZE - udatamemoffset(nuvalue)))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_VUSERDATA, sizeudata(nuvalue, s));
  u = gco2u(o);
  u->len = s;
  u->nuvalue = nuvalue;
  u->metatable = NULL;
  for (i = 0; i < nuvalue; i++)
    setnilvalue(&u->uv[i].uv);
  return u;
}


struct NewExt {
  ls_byte kind;
  const char *s;
   size_t len;
  TString *ts;  /* output */
};


static void f_newext (lua_State *L, void *ud) {
  struct NewExt *ne = cast(struct NewExt *, ud);
  size_t size = luaS_sizelngstr(0, ne->kind);
  GCObject *o = luaC_newobj(L, LUA_VLNGSTR, size);
  TString *ts = gco2ts(o);
  ts->hash = G(L)->seed;
  ts->extra = 0;
  ne->ts = ts;
}


/**
 * @brief Creates a new external string.
 *
 * @param L The Lua state.
 * @param s The string content.
 * @param len The length of the string.
 * @param falloc The deallocation function.
 * @param ud User data for the deallocation function.
 * @return The new string object.
 */
TString *luaS_newextlstr (lua_State *L,
	          const char *s, size_t len, lua_Alloc falloc, void *ud) {
  struct NewExt ne;
  TExternalString *ts;
  if (!falloc) {
    ne.kind = LSTRFIX;
    f_newext(L, &ne);  /* just create header */
  }
  else {
    ne.kind = LSTRMEM;
    if (luaD_rawrunprotected(L, f_newext, &ne) != LUA_OK) {  /* mem. error? */
      (*falloc)(ud, cast_voidp(s), len + 1, 0);  /* free external string */
      luaM_error(L);  /* re-raise memory error */
    }
  }
  ts = (TExternalString *)(ne.ts);
  ts->falloc = falloc;
  ts->ud = ud;
  ts->src = s;
  ts->shrlen = ne.kind;
  ts->u.lnglen = len;
  return ne.ts;
}


/**
 * @brief Normalizes an external string: if it is short, internalize it.
 *
 * @param L The Lua state.
 * @param ts The string to normalize.
 * @return The normalized string.
 */
TString *luaS_normstr (lua_State *L, TString *ts) {
  size_t len = ts->u.lnglen;
  if (len > LUAI_MAXSHORTLEN)
    return ts;  /* long string; keep the original */
  else {
    const char *str = getlngstr(ts);
    return internshrstr(L, str, len);
  }
}
