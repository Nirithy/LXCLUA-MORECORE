/**
 * @file ldo.c
 * @brief Stack and Call structure of Lua.
 *
 * This file handles the stack management and function calls in Lua.
 */

#define ldo_c
#define LUA_CORE

#include "lprefix.h"


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"
#include "lzio.h"



#define errorstatus(s)	((s) > LUA_YIELD)

/*
** these macros allow user-specific actions when a thread is
** resumed/yielded.
*/
#if !defined(luai_userstateresume)
#define luai_userstateresume(L,n)	((void)L)
#endif

#if !defined(luai_userstateyield)
#define luai_userstateyield(L,n)	((void)L)
#endif

/*
** {===========================================
** Error-recovery functions
** ============================================
*/

/* chained list of long jump buffers */
typedef struct lua_longjmp {
  struct lua_longjmp *previous;
  jmp_buf b;
  volatile TStatus status;  /* error code */
} lua_longjmp;

/*
** LUAI_THROW/LUAI_TRY define how Lua does exception handling. By
** default, Lua handles errors with exceptions when compiling as
** C++ code, with _longjmp/_setjmp when available (POSIX), and with
** longjmp/setjmp otherwise.
*/
#if !defined(LUAI_THROW)				/* { */

#if defined(__cplusplus) && !defined(LUA_USE_LONGJMP)	/* { */

/* C++ exceptions */
#define LUAI_THROW(L,c)		throw(c)
#define LUAI_TRY(L,c,a) \
	try { a } catch(lua_longjmp *c1) { \
	  if (c1 != c) throw; \
	} catch(...) { \
	  if ((c)->status == 0) (c)->status = -1; \
	}

#define luai_jmpbuf		int  /* dummy variable */

#elif defined(LUA_USE_POSIX)				/* }{ */

/* in POSIX, try _longjmp/_setjmp (more efficient) */
#define LUAI_THROW(L,c)		_longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)		if (_setjmp((c)->b) == 0) { a }
#define luai_jmpbuf		jmp_buf

#else							/* }{ */

/* ISO C handling with long jumps */
#define LUAI_THROW(L,c)		longjmp((c)->b, 1)
#define LUAI_TRY(L,c,a)		if (setjmp((c)->b) == 0) { a }

#define luai_jmpbuf		jmp_buf

#endif							/* } */

#endif							/* } */




/**
 * @brief Sets an error object on the stack.
 *
 * @param L The Lua state.
 * @param errcode The error code.
 * @param oldtop The stack top before the error occurred.
 */
void luaD_seterrorobj (lua_State *L, TStatus errcode, StkId oldtop) {
  if (errcode == LUA_ERRMEM) {  /* memory error? */
    setsvalue2s(L, oldtop, G(L)->memerrmsg); /* reuse preregistered msg. */
  }
  else {
    lua_assert(errorstatus(errcode));  /* must be a real error */
    lua_assert(!ttisnil(s2v(L->top.p - 1)));  /* with a non-nil object */
    setobjs2s(L, oldtop, L->top.p - 1);  /* move it to 'oldtop' */
  }
  L->top.p = oldtop + 1;  /* top goes back to old top plus error object */
}


/**
 * @brief Throws a Lua error.
 *
 * @param L The Lua state.
 * @param errcode The error code.
 */
l_noret luaD_throw (lua_State *L, TStatus errcode) {
  if (L->errorJmp) {  /* thread has an error handler? */
    L->errorJmp->status = errcode;  /* set status */
    LUAI_THROW(L, L->errorJmp);  /* jump to it */
  }
  else {  /* thread has no error handler */
    global_State *g = G(L);
    errcode = luaE_resetthread(L, errcode);  /* close all upvalues */
    L->status = errcode;
    if (g->mainthread->errorJmp) {  /* main thread has a handler? */
      setobjs2s(L, g->mainthread->top.p++, L->top.p - 1);  /* copy error obj. */
      luaD_throw(g->mainthread, errcode);  /* re-throw in main thread */
    }
    else {  /* no handler at all; abort */
      if (g->panic) {  /* panic function? */
        lua_unlock(L);
        g->panic(L);  /* call panic function (last chance to jump out) */
      }
      abort();
    }
  }
}


/**
 * @brief Runs a function in protected mode (catching errors).
 *
 * @param L The Lua state.
 * @param f The function to call.
 * @param ud User data for the function.
 * @return The status code (LUA_OK or error code).
 */
int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {
  l_uint32 oldnCcalls = L->nCcalls;
  struct lua_longjmp lj;
  lj.status = LUA_OK;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  LUAI_TRY(L, &lj,
    (*f)(L, ud);
  );
  L->errorJmp = lj.previous;  /* restore old error handler */
  L->nCcalls = oldnCcalls;
  return lj.status;
}

/* }=========================================== */


/*
** {=======================================================
** Stack reallocation
** ========================================================
*/

/* some stack space for error handling */
#define STACKERRSPACE	200


/*
** LUAI_MAXSTACK limits the size of the Lua stack.
** It must fit into INT_MAX/2.
*/

#if !defined(LUAI_MAXSTACK)
#if 1000000 < (INT_MAX / 2)
#define LUAI_MAXSTACK           1000000
#else
#define LUAI_MAXSTACK           (INT_MAX / 2u)
#endif
#endif


/* maximum stack size that respects size_t */
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

/*
** Minimum between LUAI_MAXSTACK and MAXSTACK_BYSIZET
** (Maximum size for the stack must respect size_t.)
*/
#define MAXSTACK	cast_int(LUAI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? LUAI_MAXSTACK : MAXSTACK_BYSIZET)


/* stack size with extra space for error handling */
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


/**
 * @brief Raise an error while running the message handler.
 *
 * @param L The Lua state.
 */
l_noret luaD_errerr (lua_State *L) {
  TString *msg = luaS_newliteral(L, "error in error handling");
  setsvalue2s(L, L->top.p, msg);
  L->top.p++;  /* assume EXTRA_STACK */
  luaD_throw(L, LUA_ERRERR);
}


/**
 * @brief Check whether stack has enough space to run a simple function.
 *
 * Such as a finalizer: At least BASIC_STACK_SIZE in the Lua stack and
 * 2 slots in the C stack.
 *
 * @param L The Lua state.
 * @return 1 if stack is sufficient, 0 otherwise.
 */
int luaD_checkminstack (lua_State *L) {
  return ((stacksize(L) < MAXSTACK - BASIC_STACK_SIZE) &&
          (getCcalls(L) < LUAI_MAXCCALLS - 2));
}


/*
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before a stack reallocation, all pointers
** should be changed to offsets, and after the reallocation they should
** be changed back to pointers. As during the reallocation the pointers
** are invalid, the reallocation cannot run emergency collections.
** Alternatively, we can use the old address after the deallocation.
** That is not strict ISO C, but seems to work fine everywhere.
** The following macro chooses how strict is the code.
*/
#if !defined(LUAI_STRICT_ADDRESS)
#define LUAI_STRICT_ADDRESS	1
#endif

#if LUAI_STRICT_ADDRESS
/**
 * @brief Change all pointers to the stack into offsets.
 *
 * @param L The Lua state.
 */
static void relstack (lua_State *L) {
  CallInfo *ci;
  UpVal *up;
  L->top.offset = savestack(L, L->top.p);
  L->tbclist.offset = savestack(L, L->tbclist.p);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.offset = savestack(L, uplevel(up));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.offset = savestack(L, ci->top.p);
    ci->func.offset = savestack(L, ci->func.p);
  }
}


/**
 * @brief Change back all offsets into pointers.
 *
 * @param L The Lua state.
 * @param oldstack The old stack pointer.
 */
static void correctstack (lua_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  UNUSED(oldstack);
  L->top.p = restorestack(L, L->top.offset);
  L->tbclist.p = restorestack(L, L->tbclist.offset);
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(restorestack(L, up->v.offset));
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = restorestack(L, ci->top.offset);
    ci->func.p = restorestack(L, ci->func.offset);
    if (isLua(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'luaV_execute' */
  }
}

#else
/*
** Assume that it is fine to use an address after its deallocation,
** as long as we do not dereference it.
*/

static void relstack (lua_State *L) { UNUSED(L); }  /* do nothing */
#define ERRORSTACKSIZE	(LUAI_MAXSTACK + 200)


/**
 * @brief Correct pointers into 'oldstack' to point into 'L->stack'.
 *
 * @param L The Lua state.
 * @param oldstack The old stack pointer.
 */
static void correctstack (lua_State *L, StkId oldstack) {
  CallInfo *ci;
  UpVal *up;
  StkId newstack = L->stack.p;
  if (oldstack == newstack)
    return;
  L->top.p = L->top.p - oldstack + newstack;
  L->tbclist.p = L->tbclist.p - oldstack + newstack;
  for (up = L->openupval; up != NULL; up = up->u.open.next)
    up->v.p = s2v(uplevel(up) - oldstack + newstack);
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    ci->top.p = ci->top.p - oldstack + newstack;
    ci->func.p = ci->func.p - oldstack + newstack;
    if (isLua(ci))
      ci->u.l.trap = 1;  /* signal to update 'trap' in 'luaV_execute' */
  }
}
#endif


/**
 * @brief Reallocates the stack to a new size.
 *
 * @param L The Lua state.
 * @param newsize The new size.
 * @param raiseerror Whether to raise an error on allocation failure.
 * @return 1 on success, 0 on failure.
 */
int luaD_reallocstack (lua_State *L, int newsize, int raiseerror) {
  int oldsize = stacksize(L);
  int i;
  StkId newstack;
  StkId oldstack = L->stack.p;
  lu_byte oldgcstop = G(L)->gcstopem;
  lua_assert(newsize <= LUAI_MAXSTACK || newsize == ERRORSTACKSIZE);
  relstack(L);  /* change pointers to offsets */
  G(L)->gcstopem = 1;  /* stop emergency collection */
  newstack = luaM_reallocvector(L, oldstack, oldsize + EXTRA_STACK,
                                   newsize + EXTRA_STACK, StackValue);
  G(L)->gcstopem = oldgcstop;  /* restore emergency collection */
  if (l_unlikely(newstack == NULL)) {  /* reallocation failed? */
    correctstack(L, oldstack);  /* change offsets back to pointers */
    if (raiseerror)
      luaM_error(L);
    else return 0;  /* do not raise an error */
  }
  L->stack.p = newstack;
  correctstack(L, oldstack);  /* change offsets back to pointers */
  L->stack_last.p = L->stack.p + newsize;
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i)); /* erase new segment */
  return 1;
}


/**
 * @brief Grows the stack by at least `n` elements.
 *
 * @param L The Lua state.
 * @param n Number of elements to grow by.
 * @param raiseerror Whether to raise an error on allocation failure or overflow.
 * @return 1 on success, 0 on failure.
 */
int luaD_growstack (lua_State *L, int n, int raiseerror) {
  int size = stacksize(L);
  if (l_unlikely(size > LUAI_MAXSTACK)) {
    /* if stack is larger than maximum, thread is already using the
       extra space reserved for errors, that is, thread is handling
       a stack error; cannot grow further than that. */
    lua_assert(stacksize(L) == ERRORSTACKSIZE);
    if (raiseerror)
      luaD_errerr(L);  /* error inside message handler */
    return 0;  /* if not 'raiseerror', just signal it */
  }
  else if (n < LUAI_MAXSTACK) {  /* avoids arithmetic overflows */
    int newsize = 2 * size;  /* tentative new size */
    int needed = cast_int(L->top.p - L->stack.p) + n;
    if (newsize > LUAI_MAXSTACK)  /* cannot cross the limit */
      newsize = LUAI_MAXSTACK;
    if (newsize < needed)  /* but must respect what was asked for */
      newsize = needed;
    if (l_likely(newsize <= LUAI_MAXSTACK))
      return luaD_reallocstack(L, newsize, raiseerror);
  }
  /* else stack overflow */
  /* add extra size to be able to handle the error message */
  luaD_reallocstack(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    luaG_runerror(L, "stack overflow");
  return 0;
}


/**
 * @brief Compute how much of the stack is being used.
 *
 * By computing the maximum top of all call frames in the stack and the current top.
 *
 * @param L The Lua state.
 * @return The number of stack slots in use.
 */
static int stackinuse (lua_State *L) {
  CallInfo *ci;
  int res;
  StkId lim = L->top.p;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {
    if (lim < ci->top.p) lim = ci->top.p;
  }
  lua_assert(lim <= L->stack_last.p + EXTRA_STACK);
  res = cast_int(lim - L->stack.p) + 1;  /* part of stack in use */
  if (res < LUA_MINSTACK)
    res = LUA_MINSTACK;  /* ensure a minimum size */
  return res;
}


/**
 * @brief Shrinks the stack if it is too large compared to current use.
 *
 * @param L The Lua state.
 */
void luaD_shrinkstack (lua_State *L) {
  int inuse = stackinuse(L);
  int max = (inuse > LUAI_MAXSTACK / 3) ? LUAI_MAXSTACK : inuse * 3;
  /* if thread is currently not handling a stack overflow and its
     size is larger than maximum "reasonable" size, shrink it */
  if (inuse <= LUAI_MAXSTACK && stacksize(L) > max) {
    int nsize = (inuse > LUAI_MAXSTACK / 2) ? LUAI_MAXSTACK : inuse * 2;
    luaD_reallocstack(L, nsize, 0);  /* ok if that fails */
  }
  else  /* don't change stack */
    condmovestack(L,{},{});  /* (change only for debugging) */
  luaE_shrinkCI(L);  /* shrink CI list */
}


/**
 * @brief Increments the stack top and checks for stack overflow.
 *
 * @param L The Lua state.
 */
void luaD_inctop (lua_State *L) {
  L->top.p++;
  luaD_checkstack(L, 1);
}

/* }======================================================= */


/**
 * @brief Calls a hook for the given event.
 *
 * @param L The Lua state.
 * @param event The event type (e.g., LUA_HOOKCALL).
 * @param line The current line number.
 * @param ftransfer First index for transfer (for returns).
 * @param ntransfer Number of values transferred (for returns).
 */
void luaD_hook (lua_State *L, int event, int line,
                              int ftransfer, int ntransfer) {
  lua_Hook hook = L->hook;
  if (hook && L->allowhook) {  /* make sure there is a hook */
    int mask = CIST_HOOKED;
    CallInfo *ci = L->ci;
    ptrdiff_t top = savestack(L, L->top.p);  /* preserve original 'top' */
    ptrdiff_t ci_top = savestack(L, ci->top.p);  /* idem for 'ci->top' */
    lua_Debug ar;
    ar.event = event;
    ar.currentline = line;
    ar.i_ci = ci;
    if (ntransfer != 0) {
      mask |= CIST_TRAN;  /* 'ci' has transfer information */
      ci->u2.transferinfo.ftransfer = ftransfer;
      ci->u2.transferinfo.ntransfer = ntransfer;
    }
    if (isLua(ci) && L->top.p < ci->top.p)
      L->top.p = ci->top.p;  /* protect entire activation register */
    luaD_checkstack(L, LUA_MINSTACK);  /* ensure minimum stack size */
    if (ci->top.p < L->top.p + LUA_MINSTACK)
      ci->top.p = L->top.p + LUA_MINSTACK;
    L->allowhook = 0;  /* cannot call hooks inside a hook */
    ci->callstatus |= mask;
    lua_unlock(L);
    (*hook)(L, &ar);
    lua_lock(L);
    lua_assert(!L->allowhook);
    L->allowhook = 1;
    ci->top.p = restorestack(L, ci_top);
    L->top.p = restorestack(L, top);
    ci->callstatus &= ~mask;
  }
}


/**
 * @brief Executes a call hook for Lua functions.
 *
 * @param L The Lua state.
 * @param ci The CallInfo of the function.
 */
void luaD_hookcall (lua_State *L, CallInfo *ci) {
  L->oldpc = 0;  /* set 'oldpc' for new function */
  if (L->hookmask & LUA_MASKCALL) {  /* is call hook on? */
    int event = (ci->callstatus & CIST_TAIL) ? LUA_HOOKTAILCALL
                                             : LUA_HOOKCALL;
    Proto *p = ci_func(ci)->p;
    ci->u.l.savedpc++;  /* hooks assume 'pc' is already incremented */
    luaD_hook(L, event, -1, 1, p->numparams);
    ci->u.l.savedpc--;  /* correct 'pc' */
  }
}


/**
 * @brief Executes a return hook for Lua and C functions and sets/corrects 'oldpc'.
 *
 * @param L The Lua state.
 * @param ci The CallInfo of the returning function.
 * @param nres The number of results.
 */
static void rethook (lua_State *L, CallInfo *ci, int nres) {
  if (L->hookmask & LUA_MASKRET) {  /* is return hook on? */
    StkId firstres = L->top.p - nres;  /* index of first result */
    int delta = 0;  /* correction for vararg functions */
    int ftransfer;
    if (isLua(ci)) {
      Proto *p = ci_func(ci)->p;
      if (p->is_vararg)
        delta = ci->u.l.nextraargs + p->numparams + 1;
    }
    ci->func.p += delta;  /* if vararg, back to virtual 'func' */
    ftransfer = cast(unsigned short, firstres - ci->func.p);
    luaD_hook(L, LUA_HOOKRET, -1, ftransfer, nres);  /* call it */
    ci->func.p -= delta;
  }
  if (isLua(ci = ci->previous))
    L->oldpc = pcRel(ci->u.l.savedpc, ci_func(ci)->p);  /* set 'oldpc' */
}


/**
 * @brief Check whether 'func' has a '__call' metafield.
 *
 * If so, put it in the stack, below original 'func', so that 'luaD_precall' can call it.
 * Raise an error if there is no '__call' metafield.
 *
 * @param L The Lua state.
 * @param func The function index on the stack.
 * @return The new function index (if metamethod found).
 */
static StkId tryfuncTM (lua_State *L, StkId func) {
  const TValue *tm;
  StkId p;
  checkstackGCp(L, 1, func);  /* space for metamethod */
  tm = luaT_gettmbyobj(L, s2v(func), TM_CALL);  /* (after previous GC) */
  if (l_unlikely(ttisnil(tm)))
    luaG_callerror(L, s2v(func));  /* nothing to call */
  for (p = L->top.p; p > func; p--)  /* open space for metamethod */
    setobjs2s(L, p, p-1);
  L->top.p++;  /* stack space pre-allocated by the caller */
  setobj2s(L, func, tm);  /* metamethod is the new function to be called */
  return func;
}


/**
 * @brief Generic case for 'moveresult'.
 *
 * @param L The Lua state.
 * @param res The destination register.
 * @param nres The number of results.
 * @param wanted The number of wanted results.
 */
l_sinline void genmoveresults (lua_State *L, StkId res, int nres,
                                             int wanted) {
  StkId firstresult = L->top.p - nres;  /* index of first result */
  int i;
  if (nres > wanted)  /* extra results? */
    nres = wanted;  /* don't need them */
  for (i = 0; i < nres; i++)  /* move all results to correct place */
    setobjs2s(L, res + i, firstresult + i);
  for (; i < wanted; i++)  /* complete wanted number of results */
    setnilvalue(s2v(res + i));
  L->top.p = res + wanted;  /* top points after the last result */
}


/**
 * @brief Given 'nres' results at 'firstResult', move 'wanted' of them to 'res'.
 *
 * Handle most typical cases (zero results for commands, one result for
 * expressions, multiple results for tail calls/single parameters)
 * separated.
 *
 * @param L The Lua state.
 * @param res The destination register.
 * @param nres The number of results.
 * @param wanted The number of wanted results.
 */
l_sinline void moveresults (lua_State *L, StkId res, int nres, int wanted) {
  StkId firstresult;
  int i;
  switch (wanted) {  /* handle typical cases separately */
    case 0:  /* no values needed */
      L->top.p = res;
      return;
    case 1:  /* one value needed */
      if (nres == 0)   /* no results? */
        setnilvalue(s2v(res));  /* adjust with nil */
      else  /* at least one result */
        setobjs2s(L, res, L->top.p - nres);  /* move it to proper place */
      L->top.p = res + 1;
      return;
    case LUA_MULTRET:
      wanted = nres;  /* we want all results */
      break;
    default:  /* two/more results and/or to-be-closed variables */
      if (hastocloseCfunc(wanted)) {  /* to-be-closed variables? */
        L->ci->callstatus |= CIST_CLSRET;  /* in case of yields */
        L->ci->u2.nres = nres;
        res = luaF_close(L, res, CLOSEKTOP, 1);
        L->ci->callstatus &= ~CIST_CLSRET;
        if (L->hookmask) {  /* if needed, call hook after '__close's */
          ptrdiff_t savedres = savestack(L, res);
          rethook(L, L->ci, nres);
          res = restorestack(L, savedres);  /* hook can move stack */
        }
        wanted = decodeNresults(wanted);
        if (wanted == LUA_MULTRET)
          wanted = nres;  /* we want all results */
      }
      break;
  }
  /* generic case */
  firstresult = L->top.p - nres;  /* index of first result */
  if (nres > wanted)  /* extra results? */
    nres = wanted;  /* don't need them */
  for (i = 0; i < nres; i++)  /* move all results to correct place */
    setobjs2s(L, res + i, firstresult + i);
  for (; i < wanted; i++)  /* complete wanted number of results */
    setnilvalue(s2v(res + i));
  L->top.p = res + wanted;  /* top points after the last result */
}


/**
 * @brief Finishes a function call.
 *
 * Calls hook if necessary, moves current number of results to proper place,
 * and returns to previous call info.
 *
 * @param L The Lua state.
 * @param ci The CallInfo of the finished function.
 * @param nres The number of results returned.
 */
void luaD_poscall (lua_State *L, CallInfo *ci, int nres) {
  int wanted = ci->nresults;
  if (l_unlikely(L->hookmask && !hastocloseCfunc(wanted)))
    rethook(L, ci, nres);
  /* move results to proper place */
  moveresults(L, ci->func.p, nres, wanted);
  /* function cannot be in any of these cases when returning */
  lua_assert(!(ci->callstatus &
        (CIST_HOOKED | CIST_YPCALL | CIST_FIN | CIST_TRAN | CIST_CLSRET)));
  L->ci = ci->previous;  /* back to caller (after closing variables) */
}



#define next_ci(L)  (L->ci->next ? L->ci->next : luaE_extendCI(L))


/**
 * @brief Prepare a new CallInfo.
 *
 * @param L The Lua state.
 * @param func The function register.
 * @param nret The number of results expected.
 * @param mask The call status mask.
 * @param top The stack top.
 * @return The new CallInfo.
 */
l_sinline CallInfo *prepCallInfo (lua_State *L, StkId func, int nret,
                                                int mask, StkId top) {
  CallInfo *ci = L->ci = next_ci(L);  /* new frame */
  ci->func.p = func;
  ci->nresults = nret;
  ci->callstatus = mask;
  ci->top.p = top;
  return ci;
}


/**
 * @brief Precall for C functions.
 *
 * @param L The Lua state.
 * @param func The function register.
 * @param nresults The number of results expected.
 * @param f The C function to call.
 * @return The number of results returned.
 */
l_sinline int precallC (lua_State *L, StkId func, int nresults,
                                            lua_CFunction f) {
  int n;  /* number of returns */
  CallInfo *ci;
  checkstackGCp(L, LUA_MINSTACK, func);  /* ensure minimum stack size */
  L->ci = ci = prepCallInfo(L, func, nresults, CIST_C,
                               L->top.p + LUA_MINSTACK);
  lua_assert(ci->top.p <= L->stack_last.p);
  if (l_unlikely(L->hookmask & LUA_MASKCALL)) {
    int narg = cast_int(L->top.p - func) - 1;
    luaD_hook(L, LUA_HOOKCALL, -1, 1, narg);
  }
  lua_unlock(L);
  n = (*f)(L);  /* do the actual call */
  lua_lock(L);
  api_checknelems(L, n);
  luaD_poscall(L, ci, n);
  return n;
}


/**
 * @brief Prepare a function for a tail call.
 *
 * Building its call info on top of the current call info.
 *
 * @param L The Lua state.
 * @param ci The current CallInfo.
 * @param func The function register.
 * @param narg1 The number of arguments + 1.
 * @param delta The delta adjustment for varargs.
 * @return The number of results if C function, -1 if Lua function.
 */
int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,
                                    int narg1, int delta) {
 retry:
  switch (ttypetag(s2v(func))) {
    case LUA_VCCL:  /* C closure */
      return precallC(L, func, LUA_MULTRET, clCvalue(s2v(func))->f);
    case LUA_VLCF:  /* light C function */
      return precallC(L, func, LUA_MULTRET, fvalue(s2v(func)));
    case LUA_VCONCEPT: {  /* Lua concept */
      Proto *p = gco2concept(val_(s2v(func)).gc)->p;
      int fsize = p->maxstacksize;  /* frame size */
      int nfixparams = p->numparams;
      int i;
      checkstackGCp(L, fsize - delta, func);
      ci->func.p -= delta;  /* restore 'func' (if vararg) */
      for (i = 0; i < narg1; i++)  /* move down function and arguments */
        setobjs2s(L, ci->func.p + i, func + i);
      func = ci->func.p;  /* moved-down function */
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));  /* complete missing arguments */
      ci->top.p = func + 1 + fsize;  /* top for new function */
      lua_assert(ci->top.p <= L->stack_last.p);
      ci->u.l.savedpc = p->code;  /* starting point */
      ci->callstatus |= CIST_TAIL;
      L->top.p = func + narg1;  /* set top */
      return -1;
    }
    case LUA_VLCL: {  /* Lua function */
      Proto *p = clLvalue(s2v(func))->p;
      int fsize = p->maxstacksize;  /* frame size */
      int nfixparams = p->numparams;
      int i;
      checkstackGCp(L, fsize - delta, func);
      ci->func.p -= delta;  /* restore 'func' (if vararg) */
      for (i = 0; i < narg1; i++)  /* move down function and arguments */
        setobjs2s(L, ci->func.p + i, func + i);
      func = ci->func.p;  /* moved-down function */
      for (; narg1 <= nfixparams; narg1++)
        setnilvalue(s2v(func + narg1));  /* complete missing arguments */
      ci->top.p = func + 1 + fsize;  /* top for new function */
      lua_assert(ci->top.p <= L->stack_last.p);
      ci->u.l.savedpc = p->code;  /* starting point */
      ci->callstatus |= CIST_TAIL;
      L->top.p = func + narg1;  /* set top */
      return -1;
    }
    default: {  /* not a function */
      func = tryfuncTM(L, func);  /* try to get '__call' metamethod */
      /* return luaD_pretailcall(L, ci, func, narg1 + 1, delta); */
      narg1++;
      goto retry;  /* try again */
    }
  }
}


/**
 * @brief Prepares the call to a function (C or Lua).
 *
 * For C functions, it also does the call.
 *
 * @param L The Lua state.
 * @param func The function to call (index in stack).
 * @param nresults Number of results expected.
 * @return The CallInfo to be executed (if Lua function), or NULL (if C function).
 */
CallInfo *luaD_precall (lua_State *L, StkId func, int nresults) {
 retry:
  switch (ttypetag(s2v(func))) {
    case LUA_VCCL:  /* C closure */
      precallC(L, func, nresults, clCvalue(s2v(func))->f);
      return NULL;
    case LUA_VLCF:  /* light C function */
      precallC(L, func, nresults, fvalue(s2v(func)));
      return NULL;
    case LUA_VCONCEPT: {  /* Lua concept */
      CallInfo *ci;
      Proto *p = gco2concept(val_(s2v(func)).gc)->p;

      /* Enhanced Upvalue check */
      if (p->sizeupvalues > 0) {
        for (int i = 0; i < p->sizeupvalues; i++) {
          const Upvaldesc *uv = &p->upvalues[i];
          // Check instack value
          if (uv->instack != 0 && uv->instack != 1) {
            luaG_runerror(L, "invalid upvalue instack value");
          }
          // Check idx value range (lu_byte max 255)
          if (uv->idx > 255) {
            luaG_runerror(L, "invalid upvalue idx value");
          }
          // Check kind value
          if (uv->kind < 0 || uv->kind > 2) {
            luaG_runerror(L, "invalid upvalue kind value");
          }
        }
      }

      int narg = cast_int(L->top.p - func) - 1;  /* number of real arguments */
      int nfixparams = p->numparams;
      int fsize = p->maxstacksize;  /* frame size */
      checkstackGCp(L, fsize, func);
      L->ci = ci = prepCallInfo(L, func, nresults, 0, func + 1 + fsize);
      ci->u.l.savedpc = p->code;  /* starting point */
      for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));  /* complete missing arguments */
      lua_assert(ci->top.p <= L->stack_last.p);
      return ci;
    }
    case LUA_VLCL: {  /* Lua function */
      CallInfo *ci;
      Proto *p = clLvalue(s2v(func))->p;
      
      /* Enhanced Upvalue check */
      if (p->sizeupvalues > 0) {
        for (int i = 0; i < p->sizeupvalues; i++) {
          const Upvaldesc *uv = &p->upvalues[i];
          // Check instack value
          if (uv->instack != 0 && uv->instack != 1) {
            luaG_runerror(L, "invalid upvalue instack value");
          }
          // Check idx value range (lu_byte max 255)
          if (uv->idx > 255) {
            luaG_runerror(L, "invalid upvalue idx value");
          }
          // Check kind value
          if (uv->kind < 0 || uv->kind > 2) {
            luaG_runerror(L, "invalid upvalue kind value");
          }
        }
      }
      
      int narg = cast_int(L->top.p - func) - 1;  /* number of real arguments */
      int nfixparams = p->numparams;
      int fsize = p->maxstacksize;  /* frame size */
      checkstackGCp(L, fsize, func);
      L->ci = ci = prepCallInfo(L, func, nresults, 0, func + 1 + fsize);
      ci->u.l.savedpc = p->code;  /* starting point */
      for (; narg < nfixparams; narg++)
        setnilvalue(s2v(L->top.p++));  /* complete missing arguments */
      lua_assert(ci->top.p <= L->stack_last.p);
      return ci;
    }
    default: {  /* not a function */
      func = tryfuncTM(L, func);  /* try to get '__call' metamethod */
      /* return luaD_precall(L, func, nresults); */
      goto retry;  /* try again with metamethod */
    }
  }
}


/**
 * @brief Call a function (C or Lua) through C.
 *
 * @param L The Lua state.
 * @param func The function register.
 * @param nResults The number of results.
 * @param inc The C stack increment.
 */
l_sinline void ccall (lua_State *L, StkId func, int nResults, l_uint32 inc) {
  CallInfo *ci;
  L->nCcalls += inc;
  if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS)) {
    checkstackp(L, 0, func);  /* free any use of EXTRA_STACK */
    luaE_checkcstack(L);
  }
  if ((ci = luaD_precall(L, func, nResults)) != NULL) {  /* Lua function? */
    ci->callstatus = CIST_FRESH;  /* mark that it is a "fresh" execute */
    luaV_execute(L, ci);  /* call it */
  }
  L->nCcalls -= inc;
}


/**
 * @brief External interface for 'ccall'. Calls a function.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param nResults Number of results expected.
 */
void luaD_call (lua_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, 1);
}


/**
 * @brief Similar to 'luaD_call', but does not allow yields during the call.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param nResults Number of results expected.
 */
void luaD_callnoyield (lua_State *L, StkId func, int nResults) {
  ccall(L, func, nResults, nyci);
}


/**
 * @brief Finish the job of 'lua_pcallk' after it was interrupted by an yield.
 *
 * @param L The Lua state.
 * @param ci The CallInfo.
 * @return The status.
 */
static int finishpcallk (lua_State *L,  CallInfo *ci) {
  int status = getcistrecst(ci);  /* get original status */
  if (l_likely(status == LUA_OK))  /* no error? */
    status = LUA_YIELD;  /* was interrupted by an yield */
  else {  /* error */
    StkId func = restorestack(L, ci->u2.funcidx);
    L->allowhook = getoah(ci->callstatus);  /* restore 'allowhook' */
    func = luaF_close(L, func, status, 1);  /* can yield or raise an error */
    luaD_seterrorobj(L, status, func);
    luaD_shrinkstack(L);   /* restore stack size in case of overflow */
    setcistrecst(ci, LUA_OK);  /* clear original status */
  }
  ci->callstatus &= ~CIST_YPCALL;
  L->errfunc = ci->u.c.old_errfunc;
  /* if it is here, there were errors or yields; unlike 'lua_pcallk',
     do not change status */
  return status;
}


/**
 * @brief Completes the execution of a C function interrupted by an yield.
 *
 * @param L The Lua state.
 * @param ci The CallInfo.
 */
static void finishCcall (lua_State *L, CallInfo *ci) {
  int n;  /* actual number of results from C function */
  if (ci->callstatus & CIST_CLSRET) {  /* was returning? */
    lua_assert(hastocloseCfunc(ci->nresults));
    n = ci->u2.nres;  /* just redo 'luaD_poscall' */
    /* don't need to reset CIST_CLSRET, as it will be set again anyway */
  }
  else {
    int status = LUA_YIELD;  /* default if there were no errors */
    /* must have a continuation and must be able to call it */
    lua_assert(ci->u.c.k != NULL && yieldable(L));
    if (ci->callstatus & CIST_YPCALL)   /* was inside a 'lua_pcallk'? */
      status = finishpcallk(L, ci);  /* finish it */
    adjustresults(L, LUA_MULTRET);  /* finish 'lua_callk' */
    lua_unlock(L);
    n = (*ci->u.c.k)(L, status, ci->u.c.ctx);  /* call continuation */
    lua_lock(L);
    api_checknelems(L, n);
  }
  luaD_poscall(L, ci, n);  /* finish 'luaD_call' */
}


/**
 * @brief Executes "full continuation" of a previously interrupted coroutine.
 *
 * @param L The Lua state.
 * @param ud User data.
 */
static void unroll (lua_State *L, void *ud) {
  CallInfo *ci;
  UNUSED(ud);
  while ((ci = L->ci) != &L->base_ci) {  /* something in the stack */
    if (!isLua(ci))  /* C function? */
      finishCcall(L, ci);  /* complete its execution */
    else {  /* Lua function */
      luaV_finishOp(L);  /* finish interrupted instruction */
      luaV_execute(L, ci);  /* execute down to higher C 'boundary' */
    }
  }
}


/**
 * @brief Try to find a suspended protected call (a "recover point").
 *
 * @param L The Lua state.
 * @return The CallInfo of the recover point, or NULL.
 */
static CallInfo *findpcall (lua_State *L) {
  CallInfo *ci;
  for (ci = L->ci; ci != NULL; ci = ci->previous) {  /* search for a pcall */
    if (ci->callstatus & CIST_YPCALL)
      return ci;
  }
  return NULL;  /* no pending pcall */
}


/**
 * @brief Signal an error in the call to 'lua_resume'.
 *
 * @param L The Lua state.
 * @param msg The error message.
 * @param narg The number of arguments to pop.
 * @return LUA_ERRRUN.
 */
static int resume_error (lua_State *L, const char *msg, int narg) {
  L->top.p -= narg;  /* remove args from the stack */
  setsvalue2s(L, L->top.p, luaS_new(L, msg));  /* push error message */
  api_incr_top(L);
  lua_unlock(L);
  return LUA_ERRRUN;
}


/**
 * @brief Do the work for 'lua_resume' in protected mode.
 *
 * @param L The Lua state.
 * @param ud User data (pointer to nargs).
 */
static void resume (lua_State *L, void *ud) {
  int n = *(cast(int*, ud));  /* number of arguments */
  StkId firstArg = L->top.p - n;  /* first argument */
  CallInfo *ci = L->ci;
  if (L->status == LUA_OK)  /* starting a coroutine? */
    ccall(L, firstArg - 1, LUA_MULTRET, 0);  /* just call its body */
  else {  /* resuming from previous yield */
    lua_assert(L->status == LUA_YIELD);
    L->status = LUA_OK;  /* mark that it is running (again) */
    if (isLua(ci)) {  /* yielded inside a hook? */
      /* undo increment made by 'luaG_traceexec': instruction was not
         executed yet */
      lua_assert(ci->callstatus & CIST_HOOKYIELD);
      ci->u.l.savedpc--;
      L->top.p = firstArg;  /* discard arguments */
      luaV_execute(L, ci);  /* just continue running Lua code */
    }
    else {  /* 'common' yield */
      if (ci->u.c.k != NULL) {  /* does it have a continuation function? */
        lua_unlock(L);
        n = (*ci->u.c.k)(L, LUA_YIELD, ci->u.c.ctx); /* call continuation */
        lua_lock(L);
        api_checknelems(L, n);
      }
      luaD_poscall(L, ci, n);  /* finish 'luaD_call' */
    }
    unroll(L, NULL);  /* run continuation */
  }
}


/**
 * @brief Unrolls a coroutine in protected mode while there are recoverable errors.
 *
 * @param L The Lua state.
 * @param status The current status.
 * @return The final status.
 */
static TStatus precover (lua_State *L, TStatus status) {
  CallInfo *ci;
  while (errorstatus(status) && (ci = findpcall(L)) != NULL) {
    L->ci = ci;  /* go down to recovery functions */
    setcistrecst(ci, status);  /* status to finish 'pcall' */
    status = luaD_rawrunprotected(L, unroll, NULL);
  }
  return status;
}


/**
 * @brief Resumes a coroutine.
 *
 * @param L The coroutine state.
 * @param from The thread that resumed this coroutine.
 * @param nargs Number of arguments.
 * @param nresults Output for number of results.
 * @return Status code (LUA_OK, LUA_YIELD, or error).
 */
LUA_API int lua_resume (lua_State *L, lua_State *from, int nargs,
                                      int *nresults) {
  TStatus status;
  lua_lock(L);
  if (L->status == LUA_OK) {  /* may be starting a coroutine */
    if (L->ci != &L->base_ci)  /* not in base level? */
      return resume_error(L, "cannot resume non-suspended coroutine", nargs);
    else if (L->top.p - (L->ci->func.p + 1) == nargs)  /* no function? */
      return resume_error(L, "cannot resume dead coroutine", nargs);
  }
  else if (L->status != LUA_YIELD)  /* ended with errors? */
    return resume_error(L, "cannot resume dead coroutine", nargs);
  L->nCcalls = (from) ? getCcalls(from) : 0;
  if (getCcalls(L) >= LUAI_MAXCCALLS)
    return resume_error(L, "C stack overflow", nargs);
  L->nCcalls++;
  luai_userstateresume(L, nargs);
  api_checknelems(L, (L->status == LUA_OK) ? nargs + 1 : nargs);
  status = luaD_rawrunprotected(L, resume, &nargs);
   /* continue running after recoverable errors */
  status = precover(L, status);
  if (l_likely(!errorstatus(status)))
    lua_assert(status == L->status);  /* normal end or yield */
  else {  /* unrecoverable error */
    L->status = status;  /* mark thread as 'dead' */
    luaD_seterrorobj(L, status, L->top.p);  /* push error message */
    L->ci->top.p = L->top.p;
  }
  *nresults = (status == LUA_YIELD) ? L->ci->u2.nyield
                                    : cast_int(L->top.p - (L->ci->func.p + 1));
  lua_unlock(L);
  return APIstatus(status);
}


/**
 * @brief Checks if a coroutine can yield.
 *
 * @param L The coroutine state.
 * @return 1 if yieldable, 0 otherwise.
 */
LUA_API int lua_isyieldable (lua_State *L) {
  return yieldable(L);
}


/**
 * @brief Yields a coroutine.
 *
 * @param L The coroutine state.
 * @param nresults Number of results.
 * @param ctx Continuation context.
 * @param k Continuation function.
 * @return 0 on success (actually returns to luaD_hook, does not return to caller).
 */
LUA_API int lua_yieldk (lua_State *L, int nresults, lua_KContext ctx,
                        lua_KFunction k) {
  CallInfo *ci;
  luai_userstateyield(L, nresults);
  lua_lock(L);
  ci = L->ci;
  api_checknelems(L, nresults);
  if (l_unlikely(!yieldable(L))) {
    if (L != G(L)->mainthread)
      luaG_runerror(L, "[!] 错误: 尝试在C调用边界处yield");
    else
      luaG_runerror(L, "[!] 错误: 尝试从协程外部执行yield");
  }
  L->status = LUA_YIELD;
  ci->u2.nyield = nresults;  /* save number of results */
  if (isLua(ci)) {  /* inside a hook? */
    lua_assert(!isLuacode(ci));
    api_check(L, nresults == 0, "hooks cannot yield values");
    api_check(L, k == NULL, "hooks cannot continue after yielding");
  }
  else {
    if ((ci->u.c.k = k) != NULL)  /* is there a continuation? */
      ci->u.c.ctx = ctx;  /* save context */
    luaD_throw(L, LUA_YIELD);
  }
  lua_assert(ci->callstatus & CIST_HOOKED);  /* must be inside a hook */
  lua_unlock(L);
  return 0;  /* return to 'luaD_hook' */
}


/*
** Auxiliary structure to call 'luaF_close' in protected mode.
*/
struct CloseP {
  StkId level;
  TStatus status;
};


/**
 * @brief Auxiliary function to call 'luaF_close' in protected mode.
 */
static void closepaux (lua_State *L, void *ud) {
  struct CloseP *pcl = cast(struct CloseP *, ud);
  luaF_close(L, pcl->level, pcl->status, 0);
}


/**
 * @brief Calls 'luaF_close' in protected mode.
 *
 * Return the original status or, in case of errors, the new status.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @param status The current status.
 * @return The new status.
 */
TStatus luaD_closeprotected (lua_State *L, ptrdiff_t level, TStatus status) {
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  for (;;) {  /* keep closing upvalues until no more errors */
    struct CloseP pcl;
    pcl.level = restorestack(L, level); pcl.status = status;
    status = luaD_rawrunprotected(L, &closepaux, &pcl);
    if (l_likely(status == LUA_OK))  /* no more errors? */
      return pcl.status;
    else {  /* an error occurred; restore saved state and repeat */
      L->ci = old_ci;
      L->allowhook = old_allowhooks;
    }
  }
}


/**
 * @brief Calls a C function in protected mode.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param u User data.
 * @param old_top Stack top before the call.
 * @param ef Error function index.
 * @return The status code.
 */
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  TStatus status;
  CallInfo *old_ci = L->ci;
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = luaD_rawrunprotected(L, func, u);
  if (l_unlikely(status != LUA_OK)) {  /* an error occurred? */
    L->ci = old_ci;
    L->allowhook = old_allowhooks;
    status = luaD_closeprotected(L, old_top, status);
    luaD_seterrorobj(L, status, restorestack(L, old_top));
    luaD_shrinkstack(L);   /* restore stack size in case of overflow */
  }
  L->errfunc = old_errfunc;
  return status;
}



/*
** Execute a protected parser.
*/
struct SParser {  /* data to 'f_parser' */
  ZIO *z;
  Mbuffer buff;  /* dynamic structure used by the scanner */
  Dyndata dyd;  /* dynamic structures used by the parser */
  const char *mode;
  const char *name;
};


/**
 * @brief Check if mode is valid.
 *
 * @param L The Lua state.
 * @param mode The mode string.
 * @param x The current chunk type ("text" or "binary").
 */
static void checkmode (lua_State *L, const char *mode, const char *x) {
  if (mode && strchr(mode, x[0]) == NULL) {
    luaO_pushfstring(L,
       "attempt to load a %s chunk (mode is '%s')", x, mode);
    luaD_throw(L, LUA_ERRSYNTAX);
  }
}


/**
 * @brief Protected parser function.
 *
 * @param L The Lua state.
 * @param ud User data (SParser).
 */
static void f_parser (lua_State *L, void *ud) {
  LClosure *cl;
  struct SParser *p = cast(struct SParser *, ud);
  const char *mode = p->mode ? p->mode : "bt";
  int c = zgetc(p->z);  /* read first character */

  /* Check for encryption wrapper \x1bEnc */
  if (c == LUA_SIGNATURE[0]) {
    char sig[4] = {0};
    /* We need to peek ahead without consuming if it's NOT Enc */
    /* ZIO doesn't support extensive peeking, but we can read and unread/buffer */
    /* Check if next 3 bytes are "Enc" */
    /* Actually, we are committed if we see \x1b. Standard Lua binary starts with \x1bLua */
    /* So we can read 3 more bytes. If it's "Enc", handled. If "Lua", standard binary. If anything else, error (or push back?) */

    char next3[3];
    if (luaZ_read(p->z, next3, 3) == 0) {
      if (memcmp(next3, "Enc", 3) == 0) {
        /* It is encrypted! */
        uint64_t timestamp;
        uint8_t iv[16];
        if (luaZ_read(p->z, &timestamp, 8) == 0 && luaZ_read(p->z, iv, 16) == 0) {
          luaZ_init_decrypt(p->z, timestamp, iv);
          /* Now read the *real* first character (decrypted) */
          c = zgetc(p->z);
        } else {
          luaD_throw(L, LUA_ERRSYNTAX); /* Truncated encrypted header */
        }
      } else {
        /* Not "Enc". Push back the 3 bytes so normal processing can continue. */
        /* Note: ungetting in reverse order of reading (if relevant) or just decrementing p 3 times. */
        zungetc(p->z);
        zungetc(p->z);
        zungetc(p->z);
      }
    } else {
        /* Failed to read 3 bytes. Unlikely for valid chunks but possible for short files. */
        /* If we failed, we probably don't have a valid chunk anyway. */
        /* But to be safe, we should probably handle it? */
        /* If read fails, n is 0. zungetc works if p > buffer start. */
    }
  }

  if (c == LUA_SIGNATURE[0]) {
    int fixed = 0;
    if (strchr(mode, 'B') != NULL)
      fixed = 1;
    else
      checkmode(L, mode, "binary");
    int force_standard = (strchr(mode, 'S') != NULL);
    cl = luaU_undump(L, p->z, p->name, force_standard);
  }
  else {
    checkmode(L, mode, "text");
    cl = luaY_parser(L, p->z, &p->buff, &p->dyd, p->name, c);
  }
  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  luaF_initupvals(L, cl);
}


/**
 * @brief Parses a chunk in protected mode.
 *
 * @param L The Lua state.
 * @param z The input stream.
 * @param name The chunk name.
 * @param mode The loading mode.
 * @return The status code.
 */
TStatus luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                            const char *mode) {
  struct SParser p;
  TStatus status;
  incnny(L);  /* cannot yield during parsing */
  p.z = z; p.name = name; p.mode = mode;
  p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
  p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
  p.dyd.label.arr = NULL; p.dyd.label.size = 0;
  luaZ_initbuffer(L, &p.buff);
  status = luaD_pcall(L, f_parser, &p, savestack(L, L->top.p), L->errfunc);
  luaZ_freebuffer(L, &p.buff);
  luaM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
  luaM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
  luaM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
  decnny(L);
  return status;
}
