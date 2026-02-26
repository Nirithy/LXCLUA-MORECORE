/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


#define sizeCclosure(n)  \
	(offsetof(CClosure, upvalue) + sizeof(TValue) * cast_uint(n))

#define sizeLclosure(n)  \
	(offsetof(LClosure, upvals) + sizeof(UpVal *) * cast_uint(n))

#define sizeConcept(n)  \
	(offsetof(Concept, upvals) + sizeof(UpVal *) * cast_uint(n))


/* test whether thread is in 'twups' list */
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
#define MAXUPVAL	255


#define upisopen(up)	((up)->v.p != &(up)->u.value)


#define uplevel(up)	check_exp(upisopen(up), cast(StkId, (up)->v.p))


/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
#define MAXMISS		10



/* special status to close upvalues preserving the top of the stack */
#define CLOSEKTOP	(LUA_ERRERR + 1)


/**
 * @brief Creates a new function prototype.
 *
 * @param L The Lua state.
 * @return The new prototype.
 */
LUAI_FUNC Proto *luaF_newproto (lua_State *L);

/**
 * @brief Creates a new C closure.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new C closure.
 */
LUAI_FUNC CClosure *luaF_newCclosure (lua_State *L, int nupvals);

/**
 * @brief Creates a new Lua closure.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new Lua closure.
 */
LUAI_FUNC LClosure *luaF_newLclosure (lua_State *L, int nupvals);

/**
 * @brief Creates a new Concept.
 *
 * @param L The Lua state.
 * @param nupvals Number of upvalues.
 * @return The new Concept.
 */
LUAI_FUNC Concept *luaF_newconcept (lua_State *L, int nupvals);

/**
 * @brief Initializes the upvalues of a Lua closure.
 *
 * @param L The Lua state.
 * @param cl The Lua closure.
 */
LUAI_FUNC void luaF_initupvals (lua_State *L, LClosure *cl);

/**
 * @brief Finds an existing upvalue pointing to the given stack level, or creates a new one.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @return The upvalue.
 */
LUAI_FUNC UpVal *luaF_findupval (lua_State *L, StkId level);

/**
 * @brief Creates a new to-be-closed upvalue.
 *
 * @param L The Lua state.
 * @param level The stack level.
 */
LUAI_FUNC void luaF_newtbcupval (lua_State *L, StkId level);

/**
 * @brief Closes the upvalue at the given level.
 *
 * @param L The Lua state.
 * @param level The stack level.
 */
LUAI_FUNC void luaF_closeupval (lua_State *L, StkId level);

/**
 * @brief Closes upvalues up to the given level.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @param status The close status.
 * @param yy Yield flag.
 * @return The new top of the stack.
 */
LUAI_FUNC StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy);

/**
 * @brief Unlinks an upvalue from the open list.
 *
 * @param uv The upvalue.
 */
LUAI_FUNC void luaF_unlinkupval (UpVal *uv);

/**
 * @brief Calculates the memory size of a prototype.
 *
 * @param p The prototype.
 * @return The size in bytes.
 */
LUAI_FUNC lu_mem luaF_protosize (Proto *p);

/**
 * @brief Frees a prototype.
 *
 * @param L The Lua state.
 * @param f The prototype.
 */
LUAI_FUNC void luaF_freeproto (lua_State *L, Proto *f);

/**
 * @brief Gets the name of a local variable.
 *
 * @param func The function prototype.
 * @param local_number The index of the local variable.
 * @param pc The program counter.
 * @return The name of the variable, or NULL if not found.
 */
LUAI_FUNC const char *luaF_getlocalname (const Proto *func, int local_number,
                                         int pc);

/**
 * @brief Creates a new call queue.
 *
 * @param L The Lua state.
 * @return The new call queue.
 */
LUAI_FUNC CallQueue *luaF_newcallqueue (lua_State *L);

/**
 * @brief Frees a call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 */
LUAI_FUNC void luaF_freecallqueue (lua_State *L, CallQueue *q);

/**
 * @brief Pushes arguments to the call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 * @param nargs Number of arguments.
 */
LUAI_FUNC void luaF_callqueuepush (lua_State *L, CallQueue *q, int nargs);

/**
 * @brief Pops arguments from the call queue.
 *
 * @param L The Lua state.
 * @param q The call queue.
 * @param nargs Output pointer for number of arguments.
 * @param args Output array for arguments.
 * @return 1 if successful, 0 if empty.
 */
LUAI_FUNC int luaF_callqueuepop (lua_State *L, CallQueue *q, int *nargs, TValue *args);

/**
 * @brief Performs hot replacement of a function prototype.
 *
 * @param L The Lua state.
 * @param cl The closure.
 * @param newproto The new prototype.
 */
LUAI_FUNC void luaF_hotreplace (lua_State *L, GCObject *cl, Proto *newproto);

/**
 * @brief Calculates the hash code of a function's bytecode.
 *
 * @param p The prototype.
 * @return The hash code.
 */
LUAI_FUNC uint64_t luaF_hashcode (const Proto *p);


#endif
