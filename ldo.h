/*
** $Id: ldo.h $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "llimits.h"
#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


/*
** Macro to check stack size and grow stack if needed.  Parameters
** 'pre'/'pos' allow the macro to preserve a pointer into the
** stack across reallocations, doing the work only when needed.
** It also allows the running of one GC step when the stack is
** reallocated.
** 'condmovestack' is used in heavy tests to force a stack reallocation
** at every check.
*/
#define luaD_checkstackaux(L,n,pre,pos)  \
	if (l_unlikely(L->stack_last.p - L->top.p <= (n))) \
	  { pre; luaD_growstack(L, n, 1); pos; } \
        else { condmovestack(L,pre,pos); }

/* In general, 'pre'/'pos' are empty (nothing to save) */
#define luaD_checkstack(L,n)	luaD_checkstackaux(L,n,(void)0,(void)0)



#define savestack(L,pt)		(cast_charp(pt) - cast_charp(L->stack.p))
#define restorestack(L,n)	cast(StkId, cast_charp(L->stack.p) + (n))


/* macro to check stack size, preserving 'p' */
#define checkstackp(L,n,p)  \
  luaD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p),  /* save 'p' */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/* macro to check stack size and GC, preserving 'p' */
#define checkstackGCp(L,n,p)  \
  luaD_checkstackaux(L, n, \
    ptrdiff_t t__ = savestack(L, p);  /* save 'p' */ \
    luaC_checkGC(L),  /* stack grow uses memory */ \
    p = restorestack(L, t__))  /* 'pos' part: restore 'p' */


/* macro to check stack size and GC */
#define checkstackGC(L,fsize)  \
	luaD_checkstackaux(L, (fsize), luaC_checkGC(L), (void)0)


/**
 * @brief Type of protected functions, to be ran by 'runprotected'.
 *
 * @param L The Lua state.
 * @param ud User data.
 */
typedef void (*Pfunc) (lua_State *L, void *ud);

/**
 * @brief Signals an error in the error handling function.
 *
 * @param L The Lua state.
 */
LUAI_FUNC l_noret luaD_errerr (lua_State *L);

/**
 * @brief Sets the error object on the stack.
 *
 * @param L The Lua state.
 * @param errcode The error code.
 * @param oldtop The old stack top.
 */
LUAI_FUNC void luaD_seterrorobj (lua_State *L, int errcode, StkId oldtop);

/**
 * @brief Protected parser function.
 *
 * @param L The Lua state.
 * @param z The input stream.
 * @param name The chunk name.
 * @param mode The loading mode.
 * @return The status code.
 */
LUAI_FUNC int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                                  const char *mode);

/**
 * @brief Calls a debug hook.
 *
 * @param L The Lua state.
 * @param event The event code.
 * @param line The current line.
 * @param fTransfer Offset of first value transferred.
 * @param nTransfer Number of values transferred.
 */
LUAI_FUNC void luaD_hook (lua_State *L, int event, int line,
                                        int fTransfer, int nTransfer);

/**
 * @brief Calls a hook for a function call.
 *
 * @param L The Lua state.
 * @param ci The call info.
 */
LUAI_FUNC void luaD_hookcall (lua_State *L, CallInfo *ci);

/**
 * @brief Prepares for a tail call.
 *
 * @param L The Lua state.
 * @param ci The call info.
 * @param func The function to call.
 * @param narg1 Number of arguments + 1.
 * @param delta Stack delta.
 * @return 1 if successful.
 */
LUAI_FUNC int luaD_pretailcall (lua_State *L, CallInfo *ci, StkId func,
                                              int narg1, int delta);

/**
 * @brief Prepares for a function call.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param nResults Number of expected results.
 * @return The new CallInfo.
 */
LUAI_FUNC CallInfo *luaD_precall (lua_State *L, StkId func, int nResults);

/**
 * @brief Calls a function.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param nResults Number of expected results.
 */
LUAI_FUNC void luaD_call (lua_State *L, StkId func, int nResults);

/**
 * @brief Calls a function without yielding.
 *
 * @param L The Lua state.
 * @param func The function to call.
 * @param nResults Number of expected results.
 */
LUAI_FUNC void luaD_callnoyield (lua_State *L, StkId func, int nResults);

/**
 * @brief Closes a protected call.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @param status The status code.
 * @return The status code.
 */
LUAI_FUNC int luaD_closeprotected (lua_State *L, ptrdiff_t level, int status);

/**
 * @brief Calls a function in protected mode.
 *
 * @param L The Lua state.
 * @param func The protected function.
 * @param u User data.
 * @param oldtop Old stack top.
 * @param ef Error function index.
 * @return The status code.
 */
LUAI_FUNC int luaD_pcall (lua_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);

/**
 * @brief Finishes a function call.
 *
 * @param L The Lua state.
 * @param ci The call info.
 * @param nres Number of results.
 */
LUAI_FUNC void luaD_poscall (lua_State *L, CallInfo *ci, int nres);

/**
 * @brief Reallocates the stack.
 *
 * @param L The Lua state.
 * @param newsize The new size.
 * @param raiseerror Whether to raise an error on failure.
 * @return 1 on success, 0 on failure.
 */
LUAI_FUNC int luaD_reallocstack (lua_State *L, int newsize, int raiseerror);

/**
 * @brief Grows the stack.
 *
 * @param L The Lua state.
 * @param n Number of slots needed.
 * @param raiseerror Whether to raise an error on failure.
 * @return 1 on success, 0 on failure.
 */
LUAI_FUNC int luaD_growstack (lua_State *L, int n, int raiseerror);

/**
 * @brief Shrinks the stack.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaD_shrinkstack (lua_State *L);

/**
 * @brief Increments the stack top.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaD_inctop (lua_State *L);

/**
 * @brief Raises a Lua error.
 *
 * @param L The Lua state.
 * @param errcode The error code.
 */
LUAI_FUNC l_noret luaD_throw (lua_State *L, int errcode);

/**
 * @brief Runs a protected function (raw).
 *
 * @param L The Lua state.
 * @param f The protected function.
 * @param ud User data.
 * @return The status code.
 */
LUAI_FUNC int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud);

#endif
