/**
 * @file lstate.c
 * @brief Global State implementation.
 *
 * This file contains functions to manage the Lua state (threads) and global state.
 */

#define lstate_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"



/*
** thread state + extra space
*/
typedef struct LX {
  lu_byte extra_[LUA_EXTRASPACE];
  lua_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** these macros allow user-specific actions when a thread is
** created/deleted
*/
#if !defined(luai_userstateopen)
#define luai_userstateopen(L)		((void)L)
#endif
#if !defined(luai_makeseed)

#include <time.h>

/*
** Compute an initial seed with some level of randomness.
** Rely on Address Space Layout Randomization (if present) and
** current time.
*/
#define addbuff(b,p,e) \
  { size_t t = cast_sizet(e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }



#endif


/**
 * @brief Sets GCdebt to a new value keeping the value (GCtotalbytes + GCdebt) invariant.
 *
 * This function updates the GC debt while ensuring that the total perceived memory usage remains consistent.
 * It also avoids underflows in 'GCtotalbytes'.
 *
 * @param g The global state.
 * @param debt The new debt value.
 */
void luaE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);
  lua_assert(tb > 0);
  if (debt < tb - MAX_LMEM)
    debt = tb - MAX_LMEM;  /* will make 'GCtotalbytes == MAX_LMEM' */
  g->GCtotalbytes = tb - debt;
  l_atomic_store(&g->GCdebt, debt);
}


/**
 * @brief Sets the C stack limit.
 *
 * @param L The Lua state.
 * @param limit The new limit (unused in this implementation).
 * @return The maximum number of C calls (LUAI_MAXCCALLS).
 */
LUA_API int lua_setcstacklimit (lua_State *L, unsigned int limit) {
  UNUSED(L); UNUSED(limit);
  return LUAI_MAXCCALLS;  /* warning?? */
}


/**
 * @brief Extends the CallInfo list for a thread.
 *
 * @param L The Lua state.
 * @return A pointer to the new CallInfo structure.
 */
CallInfo *luaE_extendCI (lua_State *L) {
  CallInfo *ci;
  lua_assert(L->ci->next == NULL);
  ci = luaM_new(L, CallInfo);
  lua_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  ci->u.l.trap = 0;
  L->nci++;
  return ci;
}


/**
 * @brief Frees all CallInfo structures not in use by a thread.
 *
 * @param L The Lua state.
 */
static void freeCI (lua_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    luaM_free(L, ci);
    L->nci--;
  }
}


/**
 * @brief Frees half of the CallInfo structures not in use by a thread, keeping the first one.
 *
 * @param L The Lua state.
 */
void luaE_shrinkCI (lua_State *L) {
  CallInfo *ci = L->ci->next;  /* first free CallInfo */
  CallInfo *next;
  if (ci == NULL)
    return;  /* no extra elements */
  while ((next = ci->next) != NULL) {  /* two extra elements? */
    CallInfo *next2 = next->next;  /* next's next */
    ci->next = next2;  /* remove next from the list */
    L->nci--;
    luaM_free(L, next);  /* free next */
    if (next2 == NULL)
      break;  /* no more elements */
    else {
      next2->previous = ci;
      ci = next2;  /* continue */
    }
  }
}


/**
 * @brief Checks if the C stack has overflowed or is approaching overflow.
 *
 * Called when 'getCcalls(L)' is larger or equal to LUAI_MAXCCALLS.
 * If equal, raises an overflow error. If value is larger than
 * LUAI_MAXCCALLS (which means it is handling an overflow) but
 * not much larger, does not report an error (to allow overflow
 * handling to work).
 *
 * @param L The Lua state.
 */
void luaE_checkcstack (lua_State *L) {
  if (getCcalls(L) == LUAI_MAXCCALLS)
    luaG_runerror(L, "C stack overflow");
  else if (getCcalls(L) >= (LUAI_MAXCCALLS / 10 * 11))
    luaD_errerr(L);  /* error while handling stack error */
}


/**
 * @brief Increments the C call depth and checks for stack overflow.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaE_incCstack (lua_State *L) {
  L->nCcalls++;
  if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS))
    luaE_checkcstack(L);
}


/**
 * @brief Initializes the stack for a new thread.
 *
 * @param L1 The new thread.
 * @param L The existing thread (used for memory allocation).
 */
static void stack_init (lua_State *L1, lua_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack.p = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  L1->tbclist.p = L1->stack.p;
  for (i = 0; i < BASIC_STACK_SIZE + EXTRA_STACK; i++)
    setnilvalue(s2v(L1->stack.p + i));  /* erase new stack */
  L1->top.p = L1->stack.p;
  L1->stack_last.p = L1->stack.p + BASIC_STACK_SIZE;
  /* initialize first ci */
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = CIST_C;
  ci->func.p = L1->top.p;
  ci->u.c.k = NULL;
  ci->nresults = 0;
  setnilvalue(s2v(L1->top.p));  /* 'function' entry for this 'ci' */
  L1->top.p++;
  ci->top.p = L1->top.p + LUA_MINSTACK;
  L1->ci = ci;
}


/**
 * @brief Frees the stack of a thread.
 *
 * @param L The Lua state.
 */
static void freestack (lua_State *L) {
  if (L->stack.p == NULL)
    return;  /* stack not completely built yet */
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  freeCI(L);
  lua_assert(L->nci == 0);
  luaM_freearray(L, L->stack.p, stacksize(L) + EXTRA_STACK);  /* free stack */
}


/**
 * @brief Creates the registry table and its predefined values.
 *
 * @param L The Lua state.
 * @param g The global state.
 */
static void init_registry (lua_State *L, global_State *g) {
  /* create registry */
  Table *registry = luaH_new(L);
  sethvalue(L, &g->l_registry, registry);
  luaH_resize(L, registry, LUA_RIDX_LAST, 0);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(L, &registry->array[LUA_RIDX_MAINTHREAD - 1], L);
  /* registry[LUA_RIDX_GLOBALS] = new table (table of globals) */
  sethvalue(L, &registry->array[LUA_RIDX_GLOBALS - 1], luaH_new(L));
}


/**
 * @brief Opens parts of the state that may cause memory-allocation errors.
 *
 * @param L The Lua state.
 * @param ud User data (unused).
 */
static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  luaS_init(L);
  luaT_init(L);
  luaX_init(L);
  g->gcstp = 0;  /* allow gc */
  setnilvalue(&g->nilvalue);  /* now state is complete */
  luai_userstateopen(L);
}


/**
 * @brief Pre-initializes a thread with consistent values without allocating any memory.
 *
 * @param L The thread to initialize.
 * @param g The global state.
 */
static void preinit_thread (lua_State *L, global_State *g) {
  G(L) = g;
  L->stack.p = NULL;
  L->ci = NULL;
  L->nci = 0;
  L->twups = L;  /* thread has no upvalues */
  L->nCcalls = 0;
  L->errorJmp = NULL;
  L->hook = NULL;
  L->hookmask = 0;
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;
  L->status = LUA_OK;
  L->errfunc = 0;
  L->oldpc = 0;
}


/**
 * @brief Closes a Lua state, cleaning up all resources.
 *
 * @param L The Lua state to close.
 */
static void close_state (lua_State *L) {
  global_State *g = G(L);
  if (!completestate(g))  /* closing a partially built state? */
    luaC_freeallobjects(L);  /* just collect its objects */
  else {  /* closing a fully built state */
    L->ci = &L->base_ci;  /* unwind CallInfo list */
    L->errfunc = 0;   /* stack unwind can "throw away" the error function */
    luaD_closeprotected(L, 1, LUA_OK);  /* close all upvalues */
    L->top.p = L->stack.p + 1;  /* empty the stack to run finalizers */
    luaC_freeallobjects(L);  /* collect all objects */
    luai_userstateclose(L);
  }
  luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size);
  luaM_poolshutdown(L);  /* shutdown memory pool */
  l_mutex_destroy(&g->lock);
  freestack(L);
  lua_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


/**
 * @brief Creates a new thread (coroutine).
 *
 * @param L The Lua state.
 * @return Pointer to the new thread (lua_State).
 */
LUA_API lua_State *lua_newthread (lua_State *L) {
  global_State *g = G(L);
  GCObject *o;
  lua_State *L1;
  lua_lock(L);
  luaC_checkGC(L);
  /* create new thread */
  o = luaC_newobjdt(L, LUA_TTHREAD, sizeof(LX), offsetof(LX, l));
  L1 = gco2th(o);
  /* anchor it on L stack */
  setthvalue2s(L, L->top.p, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(lua_getextraspace(L1), lua_getextraspace(g->mainthread),
         LUA_EXTRASPACE);
  luai_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  lua_unlock(L);
  return L1;
}


/**
 * @brief Frees a thread.
 *
 * @param L The Lua state.
 * @param L1 The thread to free.
 */
void luaE_freethread (lua_State *L, lua_State *L1) {
  LX *l = fromstate(L1);
  luaF_closeupval(L1, L1->stack.p);  /* close all upvalues */
  lua_assert(L1->openupval == NULL);
  luai_userstatefree(L, L1);
  freestack(L1);
  luaM_free(L, l);
}


/**
 * @brief Resets a thread to its initial state.
 *
 * @param L The thread to reset.
 * @param status The status code.
 * @return The status code.
 */
int luaE_resetthread (lua_State *L, int status) {
  CallInfo *ci = L->ci = &L->base_ci;  /* unwind CallInfo list */
  setnilvalue(s2v(L->stack.p));  /* 'function' entry for basic 'ci' */
  ci->func.p = L->stack.p;
  ci->callstatus = CIST_C;
  if (status == LUA_YIELD)
    status = LUA_OK;
  L->status = LUA_OK;  /* so it can run __close metamethods */
  L->errfunc = 0;   /* stack unwind can "throw away" the error function */
  status = luaD_closeprotected(L, 1, status);
  if (status != LUA_OK)  /* errors? */
    luaD_seterrorobj(L, status, L->stack.p + 1);
  else
    L->top.p = L->stack.p + 1;
  ci->top.p = L->top.p + LUA_MINSTACK;
  luaD_reallocstack(L, cast_int(ci->top.p - L->stack.p), 0);
  return status;
}


/**
 * @brief Closes a thread or resets it if it's the main thread.
 *
 * @param L The thread to close.
 * @param from The thread calling this function.
 * @return The status code.
 */
LUA_API int lua_closethread (lua_State *L, lua_State *from) {
  int status;
  lua_lock(L);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  status = luaE_resetthread(L, L->status);
  lua_unlock(L);
  return status;
}


/**
 * @brief Deprecated! Use 'lua_closethread' instead.
 *
 * @param L The thread to reset.
 * @return The status code.
 */
LUA_API int lua_resetthread (lua_State *L) {
  return lua_closethread(L, NULL);
}


/**
 * @brief Creates a new, independent Lua state.
 *
 * @param f Memory allocator function.
 * @param ud User data for the allocator.
 * @param seed Random seed.
 * @return Pointer to the new Lua state, or NULL on memory allocation failure.
 */
LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud, unsigned seed) {
  int i;
  lua_State *L;
  global_State *g;
  LG *l = cast(LG *, (*f)(ud, NULL, LUA_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->tt = LUA_VTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = luaC_white(g);
  preinit_thread(L, g);
  g->allgc = obj2gco(L);  /* by now, only object is the main thread */
  L->next = NULL;
  incnny(L);  /* main thread is always non yieldable */
  g->frealloc = f;
  g->ud = ud;
  g->warnf = NULL;
  g->ud_warn = NULL;
  g->mainthread = L;
  g->seed = seed;
  g->gcstp = GCSTPGC;  /* no GC while building state */
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->gcstopem = 0;
  g->gcemergency = 0;
  g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->firstold1 = g->survival = g->old1 = g->reallyold = NULL;
  g->finobjsur = g->finobjold1 = g->finobjrold = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->GCtotalbytes = sizeof(LG);
  l_atomic_store(&g->GCdebt, 0);
  g->lastatomic = 0;
  setivalue(&g->nilvalue, 0);  /* to signal that state is not yet built */
  setgcparam(g->gcpause, LUAI_GCPAUSE);
  setgcparam(g->gcstepmul, LUAI_GCMUL);
  g->gcstepsize = LUAI_GCSTEPSIZE;
  setgcparam(g->genmajormul, LUAI_GENMAJORMUL);
  g->genminormul = LUAI_GENMINORMUL;
  for (i=0; i < LUA_NUMTAGS; i++) g->mt[i] = NULL;
  g->vm_code_list = NULL;  /* initialize VM code list */
  luaM_poolinit(L);  /* initialize memory pool */
  l_mutex_init(&g->lock);
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


/**
 * @brief Destroys all objects in the given Lua state and frees all dynamic memory used by it.
 *
 * @param L The Lua state to close.
 */
LUA_API void lua_close (lua_State *L) {
  lua_lock(L);
  L = G(L)->mainthread;  /* only the main thread can be closed */
  close_state(L);
}


/**
 * @brief Emits a warning message.
 *
 * @param L The Lua state.
 * @param msg The warning message.
 * @param tocont True if the message is continued in the next call.
 */
void luaE_warning (lua_State *L, const char *msg, int tocont) {
  lua_WarnFunction wf = G(L)->warnf;
  if (wf != NULL)
    wf(G(L)->ud_warn, msg, tocont);
}


/**
 * @brief Generates a warning from an error message on the stack.
 *
 * @param L The Lua state.
 * @param where The context where the error occurred.
 */
void luaE_warnerror (lua_State *L, const char *where) {
  TValue *errobj = s2v(L->top.p - 1);  /* error object */
  const char *msg = (ttisstring(errobj))
                  ? getstr(tsvalue(errobj))
                  : "error object is not a string";
  /* produce warning "error in %s (%s)" (where, msg) */
  luaE_warning(L, "error in ", 1);
  luaE_warning(L, where, 1);
  luaE_warning(L, " (", 1);
  luaE_warning(L, msg, 1);
  luaE_warning(L, ")", 0);
}
