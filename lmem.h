/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lua.h"


/**
 * @brief Raises a memory allocation error.
 *
 * @param L The Lua state.
 */
#define luaM_error(L)	luaD_throw(L, LUA_ERRMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define luaM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

/**
 * @brief Checks if a size is valid, raising an error if it's too big.
 *
 * @param L The Lua state.
 * @param n Number of elements.
 * @param e Size of each element.
 */
#define luaM_checksize(L,n,e)  \
	(luaM_testsize(n,e) ? luaM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' or 'unsigned int' and that 'int' is not larger than 'size_t'.)
*/
#define luaM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_uint((MAX_SIZET/sizeof(t))))


/*
** Arrays of chars do not need any test
*/
#define luaM_reallocvchar(L,b,on,n)  \
  cast_charp(luaM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

/**
 * @brief Frees a block of memory of a given size.
 *
 * @param L The Lua state.
 * @param b The block pointer.
 * @param s The size of the block.
 */
#define luaM_freemem(L, b, s)	luaM_free_(L, (b), (s))

/**
 * @brief Frees a block of memory corresponding to the size of the type.
 *
 * @param L The Lua state.
 * @param b The block pointer (typed).
 */
#define luaM_free(L, b)		luaM_free_(L, (b), sizeof(*(b)))

/**
 * @brief Frees an array of elements.
 *
 * @param L The Lua state.
 * @param b The array pointer.
 * @param n The number of elements.
 */
#define luaM_freearray(L, b, n)   luaM_free_(L, (b), (n)*sizeof(*(b)))

/**
 * @brief Allocates a new object of type `t`.
 *
 * @param L The Lua state.
 * @param t The type.
 */
#define luaM_new(L,t)		cast(t*, luaM_malloc_(L, sizeof(t), 0))

/**
 * @brief Allocates a new vector of `n` elements of type `t`.
 *
 * @param L The Lua state.
 * @param n The number of elements.
 * @param t The type.
 */
#define luaM_newvector(L,n,t)	cast(t*, luaM_malloc_(L, (n)*sizeof(t), 0))

/**
 * @brief Allocates a new vector with overflow check.
 *
 * @param L The Lua state.
 * @param n The number of elements.
 * @param t The type.
 */
#define luaM_newvectorchecked(L,n,t) \
  (luaM_checksize(L,n,sizeof(t)), luaM_newvector(L,n,t))

/**
 * @brief Allocates a new raw object with a GC tag.
 *
 * @param L The Lua state.
 * @param tag The tag.
 * @param s The size.
 */
#define luaM_newobject(L,tag,s)	luaM_malloc_(L, (s), tag)

/**
 * @brief Allocates a new block of chars.
 *
 * @param L The Lua state.
 * @param size The size.
 */
#define luaM_newblock(L, size)	luaM_newvector(L, size, char)

/**
 * @brief Grows a vector.
 *
 * @param L The Lua state.
 * @param v The vector pointer.
 * @param nelems The number of elements needed.
 * @param size Current size (input/output).
 * @param t The type.
 * @param limit The maximum limit.
 * @param e Error message.
 */
#define luaM_growvector(L,v,nelems,size,t,limit,e) \
	((v)=cast(t *, luaM_growaux_(L,v,nelems,&(size),sizeof(t), \
                         luaM_limitN(limit,t),e)))

/**
 * @brief Reallocates a vector.
 *
 * @param L The Lua state.
 * @param v The vector pointer.
 * @param oldn Old number of elements.
 * @param n New number of elements.
 * @param t The type.
 */
#define luaM_reallocvector(L, v,oldn,n,t) \
   (cast(t *, luaM_realloc_(L, v, cast_sizet(oldn) * sizeof(t), \
                                  cast_sizet(n) * sizeof(t))))

/**
 * @brief Shrinks a vector to match usage.
 *
 * @param L The Lua state.
 * @param v The vector pointer.
 * @param size Current size (input/output).
 * @param fs Final size needed.
 * @param t The type.
 */
#define luaM_shrinkvector(L,v,size,fs,t) \
   ((v)=cast(t *, luaM_shrinkvector_(L, v, &(size), fs, sizeof(t))))

/**
 * @brief Raises a "memory too big" error.
 *
 * @param L The Lua state.
 */
LUAI_FUNC l_noret luaM_toobig (lua_State *L);

/*
** Memory Pool Functions
*/

/**
 * @brief Allocates memory from the memory pool.
 *
 * @param L The Lua state.
 * @param size The size to allocate.
 * @return Pointer to the allocated memory.
 */
LUAI_FUNC void *luaM_poolalloc (lua_State *L, size_t size);

/**
 * @brief Frees memory to the memory pool.
 *
 * @param L The Lua state.
 * @param block The block to free.
 * @param size The size of the block.
 */
LUAI_FUNC void luaM_poolfree (lua_State *L, void *block, size_t size);

/**
 * @brief Shrinks the memory pool.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaM_poolshrink (lua_State *L);

/**
 * @brief Runs garbage collection on the memory pool.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaM_poolgc (lua_State *L);

/**
 * @brief Gets the current usage of the memory pool.
 *
 * @param L The Lua state.
 * @return usage in bytes.
 */
LUAI_FUNC size_t luaM_poolgetusage (lua_State *L);

/**
 * @brief Initializes the memory pool.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaM_poolinit (lua_State *L);

/**
 * @brief Shuts down the memory pool.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaM_poolshutdown (lua_State *L);

/* not to be called directly */
/**
 * @brief Internal reallocation function.
 *
 * @param L The Lua state.
 * @param block The block to reallocate.
 * @param oldsize Old size.
 * @param size New size.
 * @return Pointer to the new block.
 */
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);

/**
 * @brief Internal safe reallocation function (raises error on failure).
 *
 * @param L The Lua state.
 * @param block The block to reallocate.
 * @param oldsize Old size.
 * @param size New size.
 * @return Pointer to the new block.
 */
LUAI_FUNC void *luaM_saferealloc_ (lua_State *L, void *block, size_t oldsize,
                                                              size_t size);

/**
 * @brief Internal free function.
 *
 * @param L The Lua state.
 * @param block The block to free.
 * @param osize Original size.
 */
LUAI_FUNC void luaM_free_ (lua_State *L, void *block, size_t osize);

/**
 * @brief Internal auxiliary function for growing arrays.
 *
 * @param L The Lua state.
 * @param block The block to resize.
 * @param nelems Number of elements needed.
 * @param size Current size (pointer).
 * @param size_elem Size of one element.
 * @param limit Maximum number of elements.
 * @param what Error message description.
 * @return Pointer to the new block.
 */
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int nelems,
                               int *size, int size_elem, int limit,
                               const char *what);

/**
 * @brief Internal auxiliary function for shrinking arrays.
 *
 * @param L The Lua state.
 * @param block The block to resize.
 * @param nelem Current number of elements (pointer).
 * @param final_n Final number of elements.
 * @param size_elem Size of one element.
 * @return Pointer to the new block.
 */
LUAI_FUNC void *luaM_shrinkvector_ (lua_State *L, void *block, int *nelem,
                                    int final_n, int size_elem);

/**
 * @brief Internal malloc wrapper.
 *
 * @param L The Lua state.
 * @param size Size to allocate.
 * @param tag GC tag.
 * @return Pointer to the allocated block.
 */
LUAI_FUNC void *luaM_malloc_ (lua_State *L, size_t size, int tag);

#endif
