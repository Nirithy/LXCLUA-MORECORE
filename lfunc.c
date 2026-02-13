/**
 * @file lfunc.c
 * @brief Auxiliary functions to manipulate prototypes and closures.
 *
 * This file contains functions to create and manipulate function prototypes,
 * closures, and upvalues.
 */

#define lfunc_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "ljit.h"


/**
 * @brief Creates a new C closure.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new C closure.
 */
CClosure *luaF_newCclosure (lua_State *L, int nupvals) {
  GCObject *o = luaC_newobj(L, LUA_VCCL, sizeCclosure(nupvals));
  CClosure *c = gco2ccl(o);
  c->nupvalues = cast_byte(nupvals);
  c->ishotfixed = 0;
  return c;
}


/**
 * @brief Creates a new Lua closure.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new Lua closure.
 */
LClosure *luaF_newLclosure (lua_State *L, int nupvals) {
  GCObject *o = luaC_newobj(L, LUA_VLCL, sizeLclosure(nupvals));
  LClosure *c = gco2lcl(o);
  c->p = NULL;
  c->nupvalues = cast_byte(nupvals);
  c->ishotfixed = 0;
  while (nupvals--) c->upvals[nupvals] = NULL;
  return c;
}


/**
 * @brief Creates a new Concept.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new Concept.
 */
Concept *luaF_newconcept (lua_State *L, int nupvals) {
  GCObject *o = luaC_newobj(L, LUA_VCONCEPT, sizeConcept(nupvals));
  Concept *c = gco2concept(o);
  c->p = NULL;
  c->nupvalues = cast_byte(nupvals);
  c->ishotfixed = 0;
  while (nupvals--) c->upvals[nupvals] = NULL;
  return c;
}


/**
 * @brief Fills a closure with new closed upvalues.
 *
 * @param L The Lua state.
 * @param cl The Lua closure.
 */
void luaF_initupvals (lua_State *L, LClosure *cl) {
  int i;
  for (i = 0; i < cl->nupvalues; i++) {
    GCObject *o = luaC_newobj(L, LUA_VUPVAL, sizeof(UpVal));
    UpVal *uv = gco2upv(o);
    uv->v.p = &uv->u.value;  /* make it closed */
    setnilvalue(uv->v.p);
    cl->upvals[i] = uv;
    luaC_objbarrier(L, cl, uv);
  }
}


/**
 * @brief Hotfix: replaces closure prototype while keeping upvalues.
 *
 * This allows runtime code replacement for closures.
 *
 * @param L The Lua state.
 * @param cl The closure object (as GCObject*).
 * @param newproto The new prototype.
 */
void luaF_hotreplace (lua_State *L, GCObject *cl, Proto *newproto) {
  if (cl->tt == LUA_VLCL) {
    LClosure *lcl = gco2lcl(cl);
    luaC_objbarrier(L, lcl, newproto);
    lcl->p = newproto;
    lcl->ishotfixed = 1;
    cast(Closure *, cl)->c.ishotfixed = 1;
  }
}


/*
** Create a new upvalue at the given level, and link it to the list of
** open upvalues of 'L' after entry 'prev'.
**/
static UpVal *newupval (lua_State *L, StkId level, UpVal **prev) {
  GCObject *o = luaC_newobj(L, LUA_VUPVAL, sizeof(UpVal));
  UpVal *uv = gco2upv(o);
  UpVal *next = *prev;
  uv->v.p = s2v(level);  /* current value lives in the stack */
  uv->u.open.next = next;  /* link it to list of open upvalues */
  uv->u.open.previous = prev;
  if (next)
    next->u.open.previous = &uv->u.open.next;
  *prev = uv;
  if (!isintwups(L)) {  /* thread not in list of threads with upvalues? */
    L->twups = G(L)->twups;  /* link it to the list */
    G(L)->twups = L;
  }
  return uv;
}


/**
 * @brief Finds and reuses, or creates if it does not exist, an upvalue at the given level.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @return The upvalue.
 */
UpVal *luaF_findupval (lua_State *L, StkId level) {
  UpVal **pp = &L->openupval;
  UpVal *p;
  lua_assert(isintwups(L) || L->openupval == NULL);
  while ((p = *pp) != NULL && uplevel(p) >= level) {  /* search for it */
    lua_assert(!isdead(G(L), p));
    if (uplevel(p) == level)  /* corresponding upvalue? */
      return p;  /* return it */
    pp = &p->u.open.next;
  }
  /* not found: create a new upvalue after 'pp' */
  return newupval(L, level, pp);
}


/*
** Call closing method for object 'obj' with error message 'err'. The
** boolean 'yy' controls whether the call is yieldable.
** (This function assumes EXTRA_STACK.)
*/
static void callclosemethod (lua_State *L, TValue *obj, TValue *err, int yy) {
  StkId top = L->top.p;
  const TValue *tm = luaT_gettmbyobj(L, obj, TM_CLOSE);
  if (ttisnil(tm) && ttisfunction(obj))
    tm = obj;  /* use object itself as the close method */
  setobj2s(L, top, tm);  /* will call metamethod... */
  setobj2s(L, top + 1, obj);  /* with 'self' as the 1st argument */
  setobj2s(L, top + 2, err);  /* and error msg. as 2nd argument */
  L->top.p = top + 3;  /* add function and arguments */
  if (yy)
    luaD_call(L, top, 0);
  else
    luaD_callnoyield(L, top, 0);
}


/*
** Check whether object at given level has a close metamethod and raise
** an error if not.
*/
static void checkclosemth (lua_State *L, StkId level) {
  const TValue *tm = luaT_gettmbyobj(L, s2v(level), TM_CLOSE);
  if (ttisnil(tm) && !ttisfunction(s2v(level))) {  /* no metamethod and not a function? */
    int idx = cast_int(level - L->ci->func.p);  /* variable index */
    const char *vname = luaG_findlocal(L, L->ci, idx, NULL);
    if (vname == NULL) vname = "?";
    luaG_runerror(L, "variable '%s' got a non-closable value", vname);
  }
}


/*
** Prepare and call a closing method.
** If status is CLOSEKTOP, the call to the closing method will be pushed
** at the top of the stack. Otherwise, values can be pushed right after
** the 'level' of the upvalue being closed, as everything after that
** won't be used again.
*/
static void prepcallclosemth (lua_State *L, StkId level, TStatus status,
                                            int yy) {
  TValue *uv = s2v(level);  /* value being closed */
  TValue *errobj;
  
  switch (status){
  	case CLOSEKTOP:
  	errobj = &G(L)->nilvalue;  /* error object is nil */
  	break;
  	
    case LUA_OK:
          L->top.p = level + 1;  /* call will be at this level */
      /* FALLTHROUGH */
    
        break;
  default:  /* 'luaD_seterrorobj' will set top to level + 2 */
    errobj = s2v(level + 1);  /* error object goes after 'uv' */
    luaD_seterrorobj(L, status, level + 1);  /* set error object */
          break;
  }
  callclosemethod(L, uv, errobj, yy);
}


/* Maximum value for deltas in 'tbclist' */
#define MAXDELTA       USHRT_MAX


/**
 * @brief Inserts a variable in the list of to-be-closed variables.
 *
 * @param L The Lua state.
 * @param level The stack level of the variable.
 */
void luaF_newtbcupval (lua_State *L, StkId level) {
  lua_assert(level > L->tbclist.p);
  if (l_isfalse(s2v(level)))
    return;  /* false doesn't need to be closed */
  checkclosemth(L, level);  /* value must have a close method */
  while (cast_uint(level - L->tbclist.p) > MAXDELTA) {
    L->tbclist.p += MAXDELTA;  /* create a dummy node at maximum delta */
    L->tbclist.p->tbclist.delta = 0;
  }
  level->tbclist.delta = cast(unsigned short, level - L->tbclist.p);
  L->tbclist.p = level;
}


/**
 * @brief Unlinks an upvalue from the list of open upvalues.
 *
 * @param uv The upvalue.
 */
void luaF_unlinkupval (UpVal *uv) {
  lua_assert(upisopen(uv));
  *uv->u.open.previous = uv->u.open.next;
  if (uv->u.open.next)
    uv->u.open.next->u.open.previous = uv->u.open.previous;
}


/**
 * @brief Closes all upvalues up to the given stack level.
 *
 * @param L The Lua state.
 * @param level The stack level.
 */
void luaF_closeupval (lua_State *L, StkId level) {
  UpVal *uv;
  while ((uv = L->openupval) != NULL && uplevel(uv) >= level) {
    TValue *slot = &uv->u.value;  /* new position for value */
    lua_assert(uplevel(uv) < L->top.p);
    luaF_unlinkupval(uv);  /* remove upvalue from 'openupval' list */
    setobj(L, slot, uv->v.p);  /* move value to upvalue slot */
    uv->v.p = slot;  /* now current value lives here */
    if (!iswhite(uv)) {  /* neither white nor dead? */
      nw2black(uv);  /* closed upvalues cannot be gray */
      luaC_barrier(L, uv, slot);
    }
  }
}


/*
** Remove first element from the tbclist plus its dummy nodes.
*/
static void poptbclist (lua_State *L) {
  StkId tbc = L->tbclist.p;
  lua_assert(tbc->tbclist.delta > 0);  /* first element cannot be dummy */
  tbc -= tbc->tbclist.delta;
  while (tbc > L->stack.p && tbc->tbclist.delta == 0)
    tbc -= MAXDELTA;  /* remove dummy nodes */
  L->tbclist.p = tbc;
}


/**
 * @brief Closes all upvalues and to-be-closed variables up to the given stack level.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @param status The status code (LUA_OK or error code).
 * @param yy Whether calls can yield.
 * @return The restored 'level'.
 */
StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy) {
  ptrdiff_t levelrel = savestack(L, level);
  luaF_closeupval(L, level);  /* first, close the upvalues */
  while (L->tbclist.p >= level) {  /* traverse tbc's down to that level */
    StkId tbc = L->tbclist.p;  /* get variable index */
    poptbclist(L);  /* remove it from list */
    prepcallclosemth(L, tbc, status, yy);  /* close variable */
    level = restorestack(L, levelrel);
  }
  return level;
}


/**
 * @brief Creates a new function prototype.
 *
 * @param L The Lua state.
 * @return The new prototype.
 */
Proto *luaF_newproto (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VPROTO, sizeof(Proto));
  Proto *f = gco2p(o);
  f->k = NULL;
  f->sizek = 0;
  f->p = NULL;
  f->sizep = 0;
  f->code = NULL;
  f->sizecode = 0;
  f->lineinfo = NULL;
  f->sizelineinfo = 0;
  f->abslineinfo = NULL;
  f->sizeabslineinfo = 0;
  f->upvalues = NULL;
  f->sizeupvalues = 0;
  f->numparams = 0;
  f->is_vararg = 0;
  f->maxstacksize = 0;
  f->nodiscard = 0;
  f->difierline_mode = 0;
  f->difierline_magicnum = 0;
  f->difierline_data = 0;
  f->locvars = NULL;
  f->sizelocvars = 0;
  f->linedefined = 0;
  f->lastlinedefined = 0;
  f->source = NULL;
  f->is_sleeping = 0;
  f->call_queue = NULL;
  f->jit_code = NULL;
  f->jit_size = 0;
  return f;
}


/**
 * @brief Calculates the memory size of a prototype.
 *
 * @param p The prototype.
 * @return The size in bytes.
 */
lu_mem luaF_protosize (Proto *p) {
  lu_mem sz = cast(lu_mem, sizeof(Proto))
            + cast_uint(p->sizep) * sizeof(Proto*)
            + cast_uint(p->sizek) * sizeof(TValue)
            + cast_uint(p->sizelocvars) * sizeof(LocVar)
            + cast_uint(p->sizeupvalues) * sizeof(Upvaldesc);
  if (!(p->flag & PF_FIXED)) {
    sz += cast_uint(p->sizecode) * sizeof(Instruction);
    sz += cast_uint(p->sizelineinfo) * sizeof(lu_byte);
    sz += cast_uint(p->sizeabslineinfo) * sizeof(AbsLineInfo);
  }
  return sz;
}


/**
 * @brief Frees a prototype and its associated memory.
 *
 * @param L The Lua state.
 * @param f The prototype.
 */
void luaF_freeproto (lua_State *L, Proto *f) {
  luaJ_freeproto(f);
  luaM_freearray(L, f->code, f->sizecode);
  luaM_freearray(L, f->p, f->sizep);
  luaM_freearray(L, f->k, f->sizek);
  luaM_freearray(L, f->lineinfo, f->sizelineinfo);
  luaM_freearray(L, f->abslineinfo, f->sizeabslineinfo);
  luaM_freearray(L, f->locvars, f->sizelocvars);
  luaM_freearray(L, f->upvalues, f->sizeupvalues);
  luaF_freecallqueue(L, f->call_queue);
  luaM_free(L, f);
}


/**
 * @brief Looks for the name of a local variable at a given instruction pointer.
 *
 * @param f The function prototype.
 * @param local_number The index of the local variable.
 * @param pc The program counter.
 * @return The name of the variable, or NULL if not found.
 */
const char *luaF_getlocalname (const Proto *f, int local_number, int pc) {
  int i;
  for (i = 0; i<f->sizelocvars && f->locvars[i].startpc <= pc; i++) {
    if (pc < f->locvars[i].endpc) {  /* is variable active? */
      local_number--;
      if (local_number == 0)
        return getstr(f->locvars[i].varname);
    }
  }
  return NULL;  /* not found */
}


/*
** {=======================================================
** Call Queue for Function Sleep/Wake Mechanism
** ========================================================
*/

/**
 * @brief Creates a new call queue.
 *
 * @param L The Lua state.
 * @return The new call queue.
 */
CallQueue *luaF_newcallqueue (lua_State *L) {
  CallQueue *q = (CallQueue *)luaM_new(L, CallQueue);
  q->head = NULL;
  q->tail = NULL;
  q->size = 0;
  return q;
}

/**
 * @brief Frees a call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 */
void luaF_freecallqueue (lua_State *L, CallQueue *q) {
  if (q == NULL) return;
  CallNode *node = q->head;
  while (node != NULL) {
    CallNode *next = node->next;
    luaM_free(L, node);
    node = next;
  }
  luaM_free(L, q);
}

/**
 * @brief Pushes arguments into the call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 * @param nargs Number of arguments.
 */
void luaF_callqueuepush (lua_State *L, CallQueue *q, int nargs) {
  CallNode *node = (CallNode *)luaM_new(L, CallNode);
  node->nargs = nargs;
  node->next = NULL;
  
  StkId base = L->top.p - nargs;
  for (int i = 0; i < nargs; i++) {
    setobj(L, &node->args[i], s2v(base + i));
  }
  
  if (q->tail == NULL) {
    q->head = q->tail = node;
  } else {
    q->tail->next = node;
    q->tail = node;
  }
  q->size++;
}

/**
 * @brief Pops arguments from the call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 * @param nargs Output for number of arguments.
 * @param args Buffer to store arguments.
 * @return 1 if queue was not empty, 0 otherwise.
 */
int luaF_callqueuepop (lua_State *L, CallQueue *q, int *nargs, TValue *args) {
  if (q->head == NULL) {
    return 0;
  }
  
  CallNode *node = q->head;
  *nargs = node->nargs;
  
  for (int i = 0; i < node->nargs; i++) {
    setobj(L, &args[i], &node->args[i]);
  }
  
  q->head = node->next;
  if (q->head == NULL) {
    q->tail = NULL;
  }
  q->size--;
  
  luaM_free(L, node);
  return 1;
}
