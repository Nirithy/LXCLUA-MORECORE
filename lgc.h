/*
** $Id: lgc.h $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"
#include "llimits.h"

/**
 * @brief Collectable objects may have one of three colors: white, gray, or black.
 *
 * - **White**: The object is not marked.
 * - **Gray**: The object is marked, but its references may not be marked.
 * - **Black**: The object and all its references are marked.
 *
 * The main invariant of the garbage collector, while marking objects, is that a black object can
 * never point to a white one. Moreover, any gray object must be in a "gray list" (gray, grayagain,
 * weak, allweak, ephemeron) so that it can be visited again before finishing the collection cycle.
 * (Open upvalues are an exception to this rule.) These lists have no meaning when the invariant
 * is not being enforced (e.g., sweep phase).
 */


/**
 * @name Garbage Collector States
 * @{
 */
#define GCSpropagate	0
#define GCSenteratomic	1
#define GCSatomic	2
#define GCSswpallgc	3
#define GCSswpfinobj	4
#define GCSswptobefnz	5
#define GCSswpend	6
#define GCScallfin	7
#define GCSpause	8
/** @} */


/**
 * @brief Checks if the GC is in the sweep phase.
 *
 * @param g The global state.
 */
#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/**
 * @brief Macro to tell when main invariant (white objects cannot point to black ones) must be kept.
 *
 * During a collection, the sweep phase may break the invariant, as objects turned white may point to
 * still-black objects. The invariant is restored when sweep ends and all objects are white again.
 *
 * @param g The global state.
 */
#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast_byte(~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))


/**
 * @name Marked Field Bits
 * Layout for bit use in 'marked' field. First three bits are used for object "age" in generational mode.
 * Last bit is used by tests.
 * @{
 */
#define WHITE0BIT	3  /**< Object is white (type 0) */
#define WHITE1BIT	4  /**< Object is white (type 1) */
#define BLACKBIT	5  /**< Object is black */
#define FINALIZEDBIT	6  /**< Object has been marked for finalization */

#define TESTBIT		7
/** @} */


#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


/**
 * @brief Checks if an object is white.
 * @param x The object.
 */
#define iswhite(x)      testbits((x)->marked, WHITEBITS)

/**
 * @brief Checks if an object is black.
 * @param x The object.
 */
#define isblack(x)      testbit((x)->marked, BLACKBIT)

/**
 * @brief Checks if an object is gray (neither white nor black).
 * @param x The object.
 */
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))

/**
 * @brief Checks if an object is marked for finalization.
 * @param x The object.
 */
#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)

#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)
#define isdeadm(ow,m)	((m) & (ow))

/**
 * @brief Checks if an object is dead (white and not current white).
 * @param g The global state.
 * @param v The object.
 */
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked)

#define changewhite(x)	((x)->marked ^= WHITEBITS)
#define nw2black(x)  \
	check_exp(!iswhite(x), l_setbit((x)->marked, BLACKBIT))

#define luaC_white(g)	cast_byte((g)->currentwhite & WHITEBITS)


/**
 * @name Generational Mode Object Ages
 * @{
 */
#define G_NEW		0	/**< Created in current cycle */
#define G_SURVIVAL	1	/**< Created in previous cycle */
#define G_OLD0		2	/**< Marked old by forward barrier in this cycle */
#define G_OLD1		3	/**< First full cycle as old */
#define G_OLD		4	/**< Really old object (not to be visited) */
#define G_TOUCHED1	5	/**< Old object touched this cycle */
#define G_TOUCHED2	6	/**< Old object touched in previous cycle */
/** @} */

#define AGEBITS		7  /* all age bits (111) */

#define getage(o)	((o)->marked & AGEBITS)
#define setage(o,a)  ((o)->marked = cast_byte(((o)->marked & (~AGEBITS)) | a))
#define isold(o)	(getage(o) > G_SURVIVAL)

#define changeage(o,f,t)  \
	check_exp(getage(o) == (f), (o)->marked ^= ((f)^(t)))


/* Default Values for GC parameters */
#define LUAI_GENMAJORMUL         100
#define LUAI_GENMINORMUL         20

/* wait memory to double before starting new cycle */
#define LUAI_GCPAUSE    200

/*
** some gc parameters are stored divided by 4 to allow a maximum value
** up to 1023 in a 'lu_byte'.
*/
#define getgcparam(p)	((p) * 4)
#define setgcparam(p,v)	((p) = (v) / 4)

#define LUAI_GCMUL      100

/* how much to allocate before next GC step (log2) */
#define LUAI_GCSTEPSIZE 13      /* 8 KB */


/**
 * @brief Check whether the declared GC mode is generational.
 *
 * While in generational mode, the collector can go temporarily to incremental
 * mode to improve performance. This is signaled by 'g->lastatomic != 0'.
 *
 * @param g The global state.
 */
#define isdecGCmodegen(g)	(g->gckind == KGC_GENH || g->lastatomic != 0)


/**
 * @name GC Control Flags
 * @{
 */
#define GCSTPUSR	1  /**< Bit true when GC stopped by user */
#define GCSTPGC		2  /**< Bit true when GC stopped by itself */
#define GCSTPCLS	4  /**< Bit true when closing Lua state */
/** @} */

/**
 * @brief Checks if GC is running.
 * @param g The global state.
 */
#define gcrunning(g)	((g)->gcstp == 0)


/**
 * @brief Does one step of collection when debt becomes positive.
 *
 * 'pre'/'pos' allows some adjustments to be done only when needed. macro
 * 'condchangemem' is used only for heavy tests (forcing a full
 * GC cycle on every opportunity).
 *
 * @param L The Lua state.
 * @param pre Pre-action.
 * @param pos Post-action.
 */
#define luaC_condGC(L,pre,pos) \
	{ if (l_atomic_load(&G(L)->GCdebt) > 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos); }

/* more often than not, 'pre'/'pos' are empty */
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)


#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

#define luaC_barrier(L,p,v) (  \
	iscollectable(v) ? luaC_objbarrier(L,p,gcvalue(v)) : cast_void(0))

#define luaC_objbarrierback(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? luaC_barrierback_(L,p) : cast_void(0))

#define luaC_barrierback(L,p,v) (  \
	iscollectable(v) ? luaC_objbarrierback(L, p, gcvalue(v)) : cast_void(0))

/**
 * @brief Fixes an object (marks it as fixed).
 *
 * @param L The Lua state.
 * @param o The object.
 */
LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o);

/**
 * @brief Frees all collectable objects.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaC_freeallobjects (lua_State *L);

/**
 * @brief Performs a GC step.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaC_step (lua_State *L);

/**
 * @brief Runs the GC until it reaches a specific state.
 *
 * @param L The Lua state.
 * @param statesmask Mask of target states.
 */
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);

/**
 * @brief Performs a full GC cycle.
 *
 * @param L The Lua state.
 * @param isemergency Emergency flag.
 */
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);

/**
 * @brief Creates a new collectable object.
 *
 * @param L The Lua state.
 * @param tt Object tag (type).
 * @param sz Size of the object.
 * @return The new object.
 */
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);

/**
 * @brief Creates a new collectable object with a specific offset for user data.
 *
 * @param L The Lua state.
 * @param tt Object tag.
 * @param sz Size of the object.
 * @param offset Offset.
 * @return The new object.
 */
LUAI_FUNC GCObject *luaC_newobjdt (lua_State *L, int tt, size_t sz,
                                                 size_t offset);

/**
 * @brief Barrier for object modification.
 *
 * @param L The Lua state.
 * @param o The parent object.
 * @param v The value object being assigned.
 */
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);

/**
 * @brief Backward barrier for object modification.
 *
 * @param L The Lua state.
 * @param o The parent object.
 */
LUAI_FUNC void luaC_barrierback_ (lua_State *L, GCObject *o);

/**
 * @brief Checks a finalizer for an object.
 *
 * @param L The Lua state.
 * @param o The object.
 * @param mt The metatable.
 */
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, GCObject *mt);

/**
 * @brief Changes the GC mode (incremental or generational).
 *
 * @param L The Lua state.
 * @param newmode The new mode.
 */
LUAI_FUNC void luaC_changemode (lua_State *L, int newmode);


#endif
