/*
** $Id: lstate.h $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"


/* Some header files included here need this definition */
typedef struct CallInfo CallInfo;


#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/**
 * @brief Notes about garbage-collected objects.
 *
 * All objects in Lua must be kept accessible until being freed.
 * Objects belong to one of these lists, linked by 'next' field:
 *
 * - 'allgc': all objects not marked for finalization.
 * - 'finobj': all objects marked for finalization.
 * - 'tobefnz': all objects ready to be finalized.
 * - 'fixedgc': objects not to be collected (e.g., reserved words).
 *
 * Generational collector lists:
 * - 'survival': new objects.
 * - 'old': objects that survived one collection.
 * - 'reallyold': objects old for more than one cycle.
 */

/*
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray (with two exceptions explained below):
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
**
** The exceptions to that "gray rule" are:
** - TOUCHED2 objects in generational mode stay in a gray list (because
** they must be visited again at the end of the cycle), but they are
** marked black because assignments to them must activate barriers (to
** move them back to TOUCHED1).
** - Open upvalues are kept gray to avoid barriers, but they stay out
** of gray lists. (They don't even have a 'gclist' field.)
*/



/*
** About 'nCcalls':  This count has two parts: the lower 16 bits counts
** the number of recursive invocations in the C stack; the higher
** 16 bits counts the number of non-yieldable calls in the stack.
** (They are together so that we can change and save both with one
** instruction.)
*/


/**
 * @brief Checks if this thread does not have non-yieldable calls in the stack.
 *
 * @param L The Lua state.
 */
#define yieldable(L)		(((L)->nCcalls & 0xffff0000) == 0)

/**
 * @brief Gets the real number of C calls.
 *
 * @param L The Lua state.
 */
#define getCcalls(L)	((L)->nCcalls & 0xffff)


/**
 * @brief Increments the number of non-yieldable calls.
 *
 * @param L The Lua state.
 */
#define incnny(L)	((L)->nCcalls += 0x10000)

/**
 * @brief Decrements the number of non-yieldable calls.
 *
 * @param L The Lua state.
 */
#define decnny(L)	((L)->nCcalls -= 0x10000)

/* Non-yieldable call increment */
#define nyci	(0x10000 | 1)




struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/*
** Extra stack space to handle TM calls and some other extras. This
** space is not included in 'stack_last'. It is used only to avoid stack
** checks, either because the element will be promptly popped or because
** there will be a stack check soon after the push. Function frames
** never use this extra space, so it does not need to be kept clean.
*/
#define EXTRA_STACK   5


/*
** Size of cache for strings in the API. 'N' is the number of
** sets (better be a prime) and "M" is the size of each set.
** (M == 1 makes a direct cache.)
*/
#if !defined(STRCACHE_N)
#define STRCACHE_N              53
#define STRCACHE_M              2
#endif


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)

#define stacksize(th)	cast_int((th)->stack_last.p - (th)->stack.p)


/**
 * @name GC Kinds
 * @{
 */
#define KGC_INC		0	/**< Incremental GC */
#define KGC_GENH		1	/**< Generational GC */
#define KGC_GENJ	2	/**< Generational in major mode */
/** @} */


/**
 * @brief String table (hash table for strings).
 */
typedef struct stringtable {
  TString **hash;  /**< Array of buckets (linked lists of strings). */
  int nuse;  /**< Number of elements. */
  int size;  /**< Number of buckets. */
} stringtable;


/**
 * @brief Information about a function call.
 */
struct CallInfo {
  StkIdRel func;  /**< Function index in the stack. */
  StkIdRel top;  /**< Top for this function. */
  struct CallInfo *previous, *next;  /**< Dynamic call link. */
  union {
    struct {  /* only for Lua functions */
      const Instruction *savedpc; /**< Saved program counter. */
      volatile l_signalT trap;  /**< Function is tracing lines/counts. */
      int nextraargs;  /**< # of extra arguments in vararg functions. */
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /**< Continuation in case of yields. */
      ptrdiff_t old_errfunc; /**< Old error handler. */
      lua_KContext ctx;  /**< Context info. in case of yields. */
    } c;
  } u;
  union {
    int funcidx;  /**< Called-function index. */
    int nyield;  /**< Number of values yielded. */
    int nres;  /**< Number of values returned. */
    struct {  /* info about transferred values (for call/return hooks) */
      unsigned short ftransfer;  /**< Offset of first value transferred. */
      unsigned short ntransfer;  /**< Number of values transferred. */
    } transferinfo;
  } u2;
  short nresults;  /**< Expected number of results from this function. */
  unsigned short callstatus; /**< Status of the call. */
};


/*
** Maximum expected number of results from a function
** (must fit in CIST_NRESULTS).
*/
#define MAXRESULTS	250


/**
 * @name CallInfo Status Bits
 * @{
 */
#define CIST_OAH	(1<<0)	/**< Original value of 'allowhook'. */
#define CIST_C		(1<<1)	/**< Call is running a C function. */
#define CIST_FRESH	(1<<2)	/**< Call is on a fresh "luaV_execute" frame. */
#define CIST_HOOKED	(1<<3)	/**< Call is running a debug hook. */
#define CIST_YPCALL	(1<<4)	/**< Doing a yieldable protected call. */
#define CIST_TAIL	(1<<5)	/**< Call was tail called. */
#define CIST_HOOKYIELD	(1<<6)	/**< Last hook called yielded. */
#define CIST_FIN	(1<<7)	/**< Function "called" a finalizer. */
#define CIST_TRAN	(1<<8)	/**< 'ci' has transfer information. */
#define CIST_CLSRET	(1<<9)  /**< Function is closing tbc variables. */
/** @} */

/* Bits 10-12 are used for CIST_RECST (see below) */
#define CIST_RECST	10
#if defined(LUA_COMPAT_LT_LE)
#define CIST_LEQ	(1<<13)  /* using __lt for __le */
#endif

/* bits 8-12 count call metamethods (and their extra arguments) */
#define CIST_CCMT	8  /* the offset, not the mask */
#define MAX_CCMT	(0x1fu << CIST_CCMT) /* 31个，从0开始计数，所以最大30个对象 */

/*
** Field CIST_RECST stores the "recover status", used to keep the error
** status while closing to-be-closed variables in coroutines, so that
** Lua can correctly resume after an yield from a __close method called
** because of an error.  (Three bits are enough for error status.)
*/
#define getcistrecst(ci)     (((ci)->callstatus >> CIST_RECST) & 7)
#define setcistrecst(ci,st)  \
  check_exp(((st) & 7) == (st),   /* status must fit in three bits */  \
            ((ci)->callstatus = ((ci)->callstatus & ~(7u << CIST_RECST))  \
                                | (cast(l_uint32, st) << CIST_RECST)))


/**
 * @brief Checks if the active function is a Lua function.
 * @param ci Call info.
 */
#define isLua(ci)	(!((ci)->callstatus & CIST_C))

/**
 * @brief Checks if the call is running Lua code (not a hook).
 * @param ci Call info.
 */
#define isLuacode(ci)	(!((ci)->callstatus & (CIST_C | CIST_HOOKED)))

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
*/
/*
** Memory pool for small objects. Each pool manages objects of a specific
** size class, using a simple free-list for quick allocation/deallocation.
*/
#define NUM_SIZE_CLASSES    12

/**
 * @brief Memory pool for small objects.
 */
typedef struct {
  void *free_list;       /**< Free object list (LIFO stack). */
  size_t object_size;    /**< Size of objects in this pool. */
  int max_cache;         /**< Maximum cache size. */
  int current_count;     /**< Current cached object count. */
  size_t total_alloc;    /**< Total allocations. */
  size_t total_hit;      /**< Cache hits. */
} MemPool;

/**
 * @brief Memory pool arena.
 */
typedef struct {
  MemPool pools[NUM_SIZE_CLASSES];  /**< Array of small object pools. */
  size_t threshold;                  /**< Threshold for small vs large objects. */
  lua_Alloc fallback_alloc;          /**< Fallback system allocator. */
  void *fallback_ud;                 /**< User data for fallback allocator. */
  int enabled;                       /**< Whether memory pool is enabled. */
  size_t small_limit;                /**< Upper limit for small objects. */
  l_mutex_t lock;                    /**< Lock for memory pool access. */
} MemPoolArena;

/**
 * @brief Global state structure.
 *
 * Shared by all threads of this state.
 */
typedef struct global_State {
  lua_Alloc frealloc;  /**< Function to reallocate memory. */
  void *ud;         /**< Auxiliary data to 'frealloc'. */
  l_mem GCtotalbytes;  /**< Number of bytes currently allocated - GCdebt. */
  _Atomic l_mem GCdebt;  /**< Bytes allocated not yet compensated by the collector. */
  lu_mem GCestimate;  /**< An estimate of the non-garbage memory in use. */
  l_mutex_t lock;       /**< Global lock for shared resources (strings, registry). */
  lu_mem lastatomic;  /**< See function 'genstep' in file 'lgc.c'. */
  stringtable strt;  /**< Hash table for strings. */
  TValue l_registry; /**< Registry table. */
  TValue nilvalue;  /**< A nil value. */
  unsigned int seed;  /**< Randomized seed for hashes. */
  lu_byte gcparams[LUA_GCPN]; /**< Garbage collection parameters. */
  lu_byte currentwhite; /**< Current white color. */
  lu_byte gcstate;  /**< State of garbage collector. */
  lu_byte gckind;  /**< Kind of GC running. */
  lu_byte gcstopem;  /**< Stops emergency collections. */
  lu_byte genminormul;  /**< Control for minor generational collections. */
  lu_byte genmajormul;  /**< Control for major generational collections. */
  lu_byte gcstp;  /**< Control whether GC is running. */
  lu_byte gcemergency;  /**< True if this is an emergency collection. */
  lu_byte gcpause;  /**< Size of pause between successive GCs. */
  lu_byte gcstepmul;  /**< GC "speed". */
  lu_byte gcstepsize;  /**< (log2 of) GC granularity. */
  GCObject *allgc;  /**< List of all collectable objects. */
  GCObject **sweepgc;  /**< Current position of sweep in list. */
  GCObject *finobj;  /**< List of collectable objects with finalizers. */
  GCObject *gray;  /**< List of gray objects. */
  GCObject *grayagain;  /**< List of objects to be traversed atomically. */
  GCObject *weak;  /**< List of tables with weak values. */
  GCObject *ephemeron;  /**< List of ephemeron tables (weak keys). */
  GCObject *allweak;  /**< List of all-weak tables. */
  GCObject *tobefnz;  /**< List of userdata to be GC. */
  GCObject *fixedgc;  /**< List of objects not to be collected. */
  /* fields for generational collector */
  GCObject *survival;  /**< Start of objects that survived one GC cycle. */
  GCObject *old1;  /**< Start of old1 objects. */
  GCObject *reallyold;  /**< Objects more than one cycle old ("really old"). */
  GCObject *firstold1;  /**< First OLD1 object in the list (if any). */
  GCObject *finobjsur;  /**< List of survival objects with finalizers. */
  GCObject *finobjold1;  /**< List of old1 objects with finalizers. */
  GCObject *finobjrold;  /**< List of really old objects with finalizers. */
  struct lua_State *twups;  /**< List of threads with open upvalues. */
  lua_CFunction panic;  /**< To be called in unprotected errors. */
  struct lua_State *mainthread; /**< Main thread. */
  TString *memerrmsg;  /**< Message for memory-allocation errors. */
  TString *tmname[TM_N];  /**< Array with tag-method names. */
  struct GCObject *mt[LUA_NUMTYPES];  /**< Metatables for basic types. */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /**< Cache for strings in API. */
  lua_WarnFunction warnf;  /**< Warning function. */
  void *ud_warn;         /**< Auxiliary data to 'warnf'. */
  /* Memory pool manager */
  MemPoolArena mempool;  /**< Memory pool manager. */
  /* VM protection code table list */
  struct VMCodeTable *vm_code_list;  /**< VM protection code table list head. */
} global_State;


/*
** 'per thread' state
*/
/**
 * @brief Per-thread state structure.
 */
struct lua_State {
  CommonHeader;
  lu_byte status; /**< Thread status. */
  lu_byte allowhook; /**< Allow hooks. */
  unsigned short nci;  /**< Number of items in 'ci' list. */
  StkIdRel top;  /**< First free slot in the stack. */
  global_State *l_G; /**< Pointer to global state. */
  CallInfo *ci;  /**< Call info for current function. */
  StkIdRel stack_last;  /**< End of stack (last element + 1). */
  StkIdRel stack;  /**< Stack base. */
  UpVal *openupval;  /**< List of open upvalues in this stack. */
  StkIdRel tbclist;  /**< List of to-be-closed variables. */
  GCObject *gclist; /**< List of gray objects. */
  struct lua_State *twups;  /**< List of threads with open upvalues. */
  struct lua_longjmp *errorJmp;  /**< Current error recover point. */
  CallInfo base_ci;  /**< CallInfo for first level (C calling Lua). */
  volatile lua_Hook hook; /**< Hook function. */
  ptrdiff_t errfunc;  /**< Current error handling function (stack index). */
  l_uint32 nCcalls;  /**< Number of nested (non-yieldable | C)  calls. */
  int oldpc;  /**< Last pc traced. */
  int basehookcount; /**< Base hook count. */
  int hookcount; /**< Current hook count. */
  volatile l_signalT hookmask; /**< Hook mask. */
};


#define G(L)	(L->l_G)

/*
** 'g->nilvalue' being a nil value flags that the state was completely
** build.
*/
#define completestate(g)	ttisnil(&g->nilvalue)


/*
** Union of all collectable objects (only for conversions)
** ISO C99, 6.5.2.3 p.5:
** "if a union contains several structures that share a common initial
** sequence [...], and if the union object currently contains one
** of these structures, it is permitted to inspect the common initial
** part of any of them anywhere that a declaration of the complete type
** of the union is visible."
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
  struct UpVal upv;
  struct Struct struct_;
  struct SuperStruct superstruct;
  struct Concept concept;
  struct Namespace ns;
};


/*
** ISO C99, 6.7.2.1 p.14:
** "A pointer to a union object, suitably converted, points to each of
** its members [...], and vice versa."
*/
#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == LUA_VUSERDATA, &((cast_u(o))->u))
#define gco2concept(o) check_exp((o)->tt == LUA_VCONCEPT, &((cast_u(o))->concept))
#define gco2ns(o) check_exp((o)->tt == LUA_VNAMESPACE, &((cast_u(o))->ns))
#define gco2lcl(o)  check_exp((o)->tt == LUA_VLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == LUA_VCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_VTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_VPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_VTHREAD, &((cast_u(o))->th))
#define gco2upv(o)	check_exp((o)->tt == LUA_VUPVAL, &((cast_u(o))->upv))


/*
** macro to convert a Lua object into a GCObject
** (The access to 'tt' tries to ensure that 'v' is actually a Lua object.)
*/
#define obj2gco(v)	check_exp((v)->tt >= LUA_TSTRING, &(cast_u(v)->gc))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->GCtotalbytes + l_atomic_load(&(g)->GCdebt))



LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);
LUAI_FUNC void luaE_checkcstack (lua_State *L);
LUAI_FUNC void luaE_incCstack (lua_State *L);
LUAI_FUNC void luaE_warning (lua_State *L, const char *msg, int tocont);
LUAI_FUNC void luaE_warnerror (lua_State *L, const char *where);
LUAI_FUNC int luaE_resetthread (lua_State *L, int status);


#endif
