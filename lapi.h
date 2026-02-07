/**
 * @file lapi.h
 * @brief Internal Lua API functions and macros.
 */

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


#ifndef api_check
#if defined(LUA_USE_APICHECK)
#include <assert.h>
#define api_check(l,e,msg)    assert(e)
#else	/* for testing */
#define api_check(l,e,msg)    ((void)(l), lua_assert((e) && msg))
#endif
#endif



/* Increments 'L->top.p', checking for stack overflows */
#define api_incr_top(L)	{L->top.p++; \
			 api_check(L, L->top.p <= L->ci->top.p, \
					"stack overflow");}


/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this macro
** increases its stack space ('L->ci->top.p').
*/
#define adjustresults(L,nres) \
    { if ((nres) <= LUA_MULTRET && L->ci->top.p < L->top.p) \
	L->ci->top.p = L->top.p; }


/* Ensure the stack has at least 'n' elements */
#define api_checknelems(L,n) \
       api_check(L, (n) < (L->top.p - L->ci->func.p), \
                         "not enough elements in the stack")


/* Ensure the stack has at least 'n' elements to be popped. (Some
** functions only update a slot after checking it for popping, but that
** is only an optimization for a pop followed by a push.)
*/

#define hastocloseCfunc(n)	((n) < LUA_MULTRET)

/* Map [-1, inf) (range of 'nresults') into (-inf, -2] */
#define codeNresults(n)		(-(n) - 3)
#define decodeNresults(n)	(-(n) - 3)
#define api_checkpop(L,n) \
	api_check(L, (n) < L->top.p - L->ci->func.p &&  \
                     L->tbclist.p < L->top.p - (n), \
			  "not enough free elements in the stack")
#endif
