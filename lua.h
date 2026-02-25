/*
** $Id: lua.h $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>


#include "luaconf.h"

/**
 * @name Lua Version
 * @{
 */
#define LUA_VERSION_MAJOR	"5"
#define LUA_VERSION_MINOR	"5"
#define LUA_VERSION_RELEASE	"0"

#define LUA_VERSION_NUM			505
#define LUA_VERSION_RELEASE_NUM		(LUA_VERSION_NUM * 100 + 8)

#define LUA_VERSION	"Lua " LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_RELEASE	LUA_VERSION "." LUA_VERSION_RELEASE
#define LUA_COPYRIGHT	LUA_RELEASE "  Copyright (C) 2026-2099 XCLUA"
#define LUA_AUTHORS	"DifierLine"
/** @} */


/**
 * @brief Mark for precompiled code ('<esc>Lua').
 */
#define LUA_SIGNATURE	"\x1bLua"

/**
 * @brief Option for multiple returns in 'lua_pcall' and 'lua_call'.
 */
#define LUA_MULTRET	(-1)


/**
 * @name Pseudo-indices
 * (-LUAI_MAXSTACK is the minimum valid index; we keep some free empty
 * space after that to help overflow detection)
 * @{
 */
#define LUA_REGISTRYINDEX	(-LUAI_MAXSTACK - 1000)
#define lua_upvalueindex(i)	(LUA_REGISTRYINDEX - (i))
/** @} */


/**
 * @name Thread Status
 * @{
 */
#define LUA_OK		0
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRERR	5
/** @} */


/**
 * @brief Opaque structure representing a Lua state.
 *
 * All Lua API functions involve a state pointer. This structure defines
 * the state of the Lua interpreter, including the stack, global variables,
 * and registry.
 */
typedef struct lua_State lua_State;


/**
 * @name Basic Types
 * @{
 */
#define LUA_TNONE		(-1)

#define LUA_TNIL		0
#define LUA_TBOOLEAN		1
#define LUA_TLIGHTUSERDATA	2
#define LUA_TNUMBER		3
#define LUA_TSTRING		4
#define LUA_TTABLE		5
#define LUA_TFUNCTION		6
#define LUA_TUSERDATA		7
#define LUA_TTHREAD		8
#define LUA_TSTRUCT		9
#define LUA_TPOINTER		10
#define LUA_TCONCEPT		11
#define LUA_TNAMESPACE		12
#define LUA_TSUPERSTRUCT	13

#define LUA_NUMTYPES		14
/** @} */


/**
 * @brief Minimum Lua stack available to a C function.
 */
#define LUA_MINSTACK	20


/**
 * @name Registry Predefined Values
 * @{
 */
#define LUA_RIDX_MAINTHREAD	1
#define LUA_RIDX_GLOBALS	2
#define LUA_RIDX_LAST		LUA_RIDX_GLOBALS
/** @} */


/**
 * @brief Type of numbers in Lua.
 */
typedef LUA_NUMBER lua_Number;


/**
 * @brief Type for integer functions.
 */
typedef LUA_INTEGER lua_Integer;

/**
 * @brief Unsigned integer type.
 */
typedef LUA_UNSIGNED lua_Unsigned;

/**
 * @brief Type for continuation-function contexts.
 */
typedef LUA_KCONTEXT lua_KContext;


/**
 * @brief Type for C functions registered with Lua.
 *
 * @param L The Lua state.
 * @return The number of results pushed to the stack.
 */
typedef int (*lua_CFunction) (lua_State *L);

/**
 * @brief Type for continuation functions.
 *
 * @param L The Lua state.
 * @param status The status code.
 * @param ctx The continuation context.
 * @return The result of the continuation.
 */
typedef int (*lua_KFunction) (lua_State *L, int status, lua_KContext ctx);


/**
 * @brief Type for functions that read blocks when loading Lua chunks.
 *
 * @param L The Lua state.
 * @param ud User data passed to lua_load.
 * @param sz Pointer to size of the returned block.
 * @return Pointer to the block of memory, or NULL if end of chunk.
 */
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);

/**
 * @brief Type for functions that write blocks when dumping Lua chunks.
 *
 * @param L The Lua state.
 * @param p Pointer to the buffer to write.
 * @param sz Size of the buffer.
 * @param ud User data passed to lua_dump.
 * @return 0 on success, non-zero on error.
 */
typedef int (*lua_Writer) (lua_State *L, const void *p, size_t sz, void *ud);


/**
 * @brief Type for memory-allocation functions.
 *
 * @param ud User data.
 * @param ptr Pointer to the memory block being allocated/reallocated/freed.
 * @param osize Original size of the block.
 * @param nsize New size of the block.
 * @return Pointer to the new block, or NULL if allocation fails.
 */
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);


/**
 * @brief Type for warning functions.
 *
 * @param ud User data.
 * @param msg The warning message.
 * @param tocont Whether the message is a continuation of the previous one.
 */
typedef void (*lua_WarnFunction) (void *ud, const char *msg, int tocont);


/**
 * @brief Type used by the debug API to collect debug information.
 */
typedef struct lua_Debug lua_Debug;


/**
 * @brief Functions to be called by the debugger in specific events.
 *
 * @param L The Lua state.
 * @param ar Debug information.
 */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);


/*
** generic extra include file
*/
#if defined(LUA_USER_H)
#include LUA_USER_H
#endif


/** RCS ident string */
extern const char lua_ident[];


/*
** state manipulation
*/

/**
 * @brief Creates a new independent state.
 *
 * @param f The allocator function.
 * @param ud User data for the allocator.
 * @param seed Random seed.
 * @return A pointer to the new state, or NULL if memory allocation fails.
 */
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud, unsigned seed);

/**
 * @brief Destroys all objects in the given Lua state and frees all dynamic memory used by this state.
 *
 * @param L The Lua state to close.
 */
LUA_API void       (lua_close) (lua_State *L);

/**
 * @brief Creates a new thread, pushes it on the stack, and returns a pointer to a lua_State that represents this new thread.
 *
 * @param L The main Lua state.
 * @return The new thread state.
 */
LUA_API lua_State *(lua_newthread) (lua_State *L);

/**
 * @brief Resets a thread, cleaning its call stack and closing all pending to-be-closed variables.
 *
 * @param L The thread to reset.
 * @param from The thread performing the reset.
 * @return LUA_OK on success.
 */
LUA_API int        (lua_closethread) (lua_State *L, lua_State *from);

/**
 * @deprecated Use lua_closethread instead.
 * @brief Resets a thread.
 *
 * @param L The thread to reset.
 * @return LUA_OK on success.
 */
LUA_API int        (lua_resetthread) (lua_State *L);

/**
 * @brief Sets a new panic function.
 *
 * @param L The Lua state.
 * @param panicf The new panic function.
 * @return The old panic function.
 */
LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);


/**
 * @brief Returns the version number of this Lua core.
 *
 * @param L The Lua state.
 * @return The version number.
 */
LUA_API lua_Number (lua_version) (lua_State *L);


/*
** basic stack manipulation
*/

/**
 * @brief Converts the acceptable index idx into an equivalent absolute index.
 *
 * @param L The Lua state.
 * @param idx The index to convert.
 * @return The absolute index.
 */
LUA_API int   (lua_absindex) (lua_State *L, int idx);

/**
 * @brief Returns the index of the top element in the stack.
 *
 * @param L The Lua state.
 * @return The index of the top element (stack size).
 */
LUA_API int   (lua_gettop) (lua_State *L);

/**
 * @brief Accepts any index, or 0, and sets the stack top to this index.
 *
 * @param L The Lua state.
 * @param idx The new top index.
 */
LUA_API void  (lua_settop) (lua_State *L, int idx);

/**
 * @brief Pushes a copy of the element at the given index onto the stack.
 *
 * @param L The Lua state.
 * @param idx The index of the element to copy.
 */
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);

/**
 * @brief Rotates the stack elements between the valid index idx and the top of the stack.
 *
 * @param L The Lua state.
 * @param idx The starting index for rotation.
 * @param n The number of positions to rotate.
 */
LUA_API void  (lua_rotate) (lua_State *L, int idx, int n);

/**
 * @brief Copies the element at index fromidx into the valid index toidx, replacing the value at that position.
 *
 * @param L The Lua state.
 * @param fromidx The source index.
 * @param toidx The destination index.
 */
LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);

/**
 * @brief Ensures that the stack has space for at least n extra slots.
 *
 * @param L The Lua state.
 * @param n The number of extra slots needed.
 * @return True if the stack was grown or already had space, False on failure.
 */
LUA_API int   (lua_checkstack) (lua_State *L, int n);

/**
 * @brief Performs a multi-rotation on the stack.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param n The rotation count.
 */
LUA_API void  (lua_rotate_multi) (lua_State *L, int idx, int n);

/**
 * @brief Moves values from one thread to another.
 *
 * @param from The source thread.
 * @param to The destination thread.
 * @param n The number of values to move.
 */
LUA_API void  (lua_xmove) (lua_State *from, lua_State *to, int n);


/*
** get functions (Lua -> stack)
*/

/**
 * @brief Pushes onto the stack the value of the global name.
 *
 * @param L The Lua state.
 * @param name The global variable name.
 * @return The type of the pushed value.
 */
LUA_API int (lua_getglobal) (lua_State *L, const char *name);

/**
 * @brief Pushes onto the stack the value t[k], where t is the value at the given index and k is the value at the top of the stack.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @return The type of the pushed value.
 */
LUA_API int (lua_gettable) (lua_State *L, int idx);

/**
 * @brief Pushes onto the stack the value t[k], where t is the value at the given index.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param k The key string.
 * @return The type of the pushed value.
 */
LUA_API int (lua_getfield) (lua_State *L, int idx, const char *k);

/**
 * @brief Pushes onto the stack the value t[n], where t is the value at the given index.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param n The integer key.
 * @return The type of the pushed value.
 */
LUA_API int (lua_geti) (lua_State *L, int idx, lua_Integer n);

/**
 * @brief Similar to lua_gettable, but does a raw access (i.e., without metamethods).
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @return The type of the pushed value.
 */
LUA_API int (lua_rawget) (lua_State *L, int idx);

/**
 * @brief Similar to lua_geti, but does a raw access.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param n The integer key.
 * @return The type of the pushed value.
 */
LUA_API int (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);

/**
 * @brief Pushes onto the stack the value t[k], where t is the value at the given index and k is the pointer p.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param p The pointer key.
 * @return The type of the pushed value.
 */
LUA_API int (lua_rawgetp) (lua_State *L, int idx, const void *p);


/*
** access functions (stack -> C)
*/

/**
 * @brief Returns 1 if the value at the given index is a number or a string convertible to a number, and 0 otherwise.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return 1 if number, 0 otherwise.
 */
LUA_API int             (lua_isnumber) (lua_State *L, int idx);

/**
 * @brief Returns 1 if the value at the given index is a string or a number (which is always convertible to a string), and 0 otherwise.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return 1 if string, 0 otherwise.
 */
LUA_API int             (lua_isstring) (lua_State *L, int idx);

/**
 * @brief Returns 1 if the value at the given index is a C function, and 0 otherwise.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return 1 if C function, 0 otherwise.
 */
LUA_API int             (lua_iscfunction) (lua_State *L, int idx);

/**
 * @brief Returns 1 if the value at the given index is an integer (or convertible to one), and 0 otherwise.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return 1 if integer, 0 otherwise.
 */
LUA_API int             (lua_isinteger) (lua_State *L, int idx);

/**
 * @brief Returns 1 if the value at the given index is a userdata (either full or light), and 0 otherwise.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return 1 if userdata, 0 otherwise.
 */
LUA_API int             (lua_isuserdata) (lua_State *L, int idx);

/**
 * @brief Returns the type of the value in the given valid index, or LUA_TNONE for a non-valid index.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The type code.
 */
LUA_API int             (lua_type) (lua_State *L, int idx);

/**
 * @brief Returns the name of the type encoded by the value tp.
 *
 * @param L The Lua state.
 * @param tp The type code.
 * @return The type name.
 */
LUA_API const char     *(lua_typename) (lua_State *L, int tp);

/**
 * @brief Converts the Lua value at the given index to the C type lua_Number.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param isnum Optional output pointer to boolean success flag.
 * @return The number value.
 */
LUA_API lua_Number      (lua_tonumberx) (lua_State *L, int idx, int *isnum);

/**
 * @brief Converts the Lua value at the given index to the C type lua_Integer.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param isnum Optional output pointer to boolean success flag.
 * @return The integer value.
 */
LUA_API lua_Integer     (lua_tointegerx) (lua_State *L, int idx, int *isnum);

/**
 * @brief Converts the Lua value at the given index to a C boolean value (0 or 1).
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The boolean value.
 */
LUA_API int             (lua_toboolean) (lua_State *L, int idx);

/**
 * @brief Converts the Lua value at the given index to a C string.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param len Optional output pointer to the string length.
 * @return The string value.
 */
LUA_API const char     *(lua_tolstring) (lua_State *L, int idx, size_t *len);

/**
 * @brief Returns the raw "length" of the value at the given index.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The raw length.
 */
LUA_API lua_Unsigned    (lua_rawlen) (lua_State *L, int idx);

/**
 * @brief Converts a value at the given index to a C function.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The C function, or NULL if not a C function.
 */
LUA_API lua_CFunction   (lua_tocfunction) (lua_State *L, int idx);

/**
 * @brief If the value at the given index is a full userdata, returns its block address. If it is a light userdata, returns its pointer. Otherwise, returns NULL.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The userdata pointer.
 */
LUA_API void	       *(lua_touserdata) (lua_State *L, int idx);

/**
 * @brief Converts the value at the given index to a Lua thread (represented as lua_State*).
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The thread state, or NULL if not a thread.
 */
LUA_API lua_State      *(lua_tothread) (lua_State *L, int idx);

/**
 * @brief Converts the value at the given index to a generic C pointer.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @return The pointer value.
 */
LUA_API const void     *(lua_topointer) (lua_State *L, int idx);

/**
 * @brief Pushes a raw pointer value onto the stack.
 *
 * @param L The Lua state.
 * @param p The pointer value.
 */
LUA_API void (lua_pushpointer) (lua_State *L, void *p);


/*
** Comparison and arithmetic functions
*/

/**
 * @name Arithmetic Operators
 * @{
 */
#define LUA_OPADD	0	/* ORDER TM, ORDER OP */
#define LUA_OPSUB	1
#define LUA_OPMUL	2
#define LUA_OPMOD	3
#define LUA_OPPOW	4
#define LUA_OPDIV	5
#define LUA_OPIDIV	6
#define LUA_OPBAND	7
#define LUA_OPBOR	8
#define LUA_OPBXOR	9
#define LUA_OPSHL	10
#define LUA_OPSHR	11
#define LUA_OPUNM	12
#define LUA_OPBNOT	13
/** @} */

/**
 * @brief Performs an arithmetic or bitwise operation over the two values (or one, in the case of negations) at the top of the stack, with the value at the top being the second operand, pops these values, and pushes the result of the operation.
 *
 * @param L The Lua state.
 * @param op The operation code.
 */
LUA_API void  (lua_arith) (lua_State *L, int op);

/**
 * @name Comparison Operators
 * @{
 */
#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2
/** @} */

/**
 * @brief Returns 1 if the two values in indices idx1 and idx2 are primitively equal (that is, without calling the __eq metamethod).
 *
 * @param L The Lua state.
 * @param idx1 First index.
 * @param idx2 Second index.
 * @return 1 if equal, 0 otherwise.
 */
LUA_API int   (lua_rawequal) (lua_State *L, int idx1, int idx2);

/**
 * @brief Compares two Lua values.
 *
 * @param L The Lua state.
 * @param idx1 First index.
 * @param idx2 Second index.
 * @param op Comparison operator.
 * @return 1 if true, 0 if false.
 */
LUA_API int   (lua_compare) (lua_State *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/

/**
 * @brief Pushes a nil value onto the stack.
 *
 * @param L The Lua state.
 */
LUA_API void        (lua_pushnil) (lua_State *L);

/**
 * @brief Pushes a float with value n onto the stack.
 *
 * @param L The Lua state.
 * @param n The number.
 */
LUA_API void        (lua_pushnumber) (lua_State *L, lua_Number n);

/**
 * @brief Pushes an integer with value n onto the stack.
 *
 * @param L The Lua state.
 * @param n The integer.
 */
LUA_API void        (lua_pushinteger) (lua_State *L, lua_Integer n);

/**
 * @brief Pushes the string pointed to by s with size len onto the stack.
 *
 * @param L The Lua state.
 * @param s The string buffer.
 * @param len The string length.
 * @return Pointer to the internal copy of the string.
 */
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);

/**
 * @brief Pushes an external string.
 *
 * @param L The Lua state.
 * @param s The string buffer.
 * @param len The string length.
 * @param falloc Allocation function for freeing.
 * @param ud User data for allocator.
 * @return Pointer to the string.
 */
LUA_API const char *(lua_pushexternalstring) (lua_State *L,
		const char *s, size_t len, lua_Alloc falloc, void *ud);

/**
 * @brief Pushes the zero-terminated string pointed to by s onto the stack.
 *
 * @param L The Lua state.
 * @param s The string.
 * @return Pointer to the internal copy of the string.
 */
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);

/**
 * @brief Pushes onto the stack a formatted string and returns a pointer to this string.
 *
 * @param L The Lua state.
 * @param fmt Format string.
 * @param argp Variable argument list.
 * @return Pointer to the formatted string.
 */
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt,
                                                      va_list argp);

/**
 * @brief Pushes onto the stack a formatted string and returns a pointer to this string.
 *
 * @param L The Lua state.
 * @param fmt Format string.
 * @param ... Arguments.
 * @return Pointer to the formatted string.
 */
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);

/**
 * @brief Pushes a new C closure onto the stack.
 *
 * @param L The Lua state.
 * @param fn The C function.
 * @param n The number of upvalues.
 */
LUA_API void  (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);

/**
 * @brief Pushes a boolean value with value b onto the stack.
 *
 * @param L The Lua state.
 * @param b Boolean value (0 or 1).
 */
LUA_API void  (lua_pushboolean) (lua_State *L, int b);

/**
 * @brief Pushes a light userdata onto the stack.
 *
 * @param L The Lua state.
 * @param p The pointer.
 */
LUA_API void  (lua_pushlightuserdata) (lua_State *L, void *p);

/**
 * @brief Pushes the thread represented by L onto the stack.
 *
 * @param L The Lua state.
 * @return 1 if this thread is the main thread of its state.
 */
LUA_API int   (lua_pushthread) (lua_State *L);


/*
** get functions (Lua -> stack)
*/
/* Note: lua_getglobal, lua_gettable etc. are declared earlier */

/**
 * @brief Creates a new empty table and pushes it onto the stack.
 *
 * @param L The Lua state.
 * @param narr Expected number of elements in the array part.
 * @param nrec Expected number of elements in the hash part.
 */
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);

/**
 * @brief Creates and pushes on the stack a new full userdata.
 *
 * @param L The Lua state.
 * @param sz The size of the userdata block.
 * @param nuvalue Number of user values.
 * @return Pointer to the block.
 */
LUA_API void *(lua_newuserdatauv) (lua_State *L, size_t sz, int nuvalue);

/**
 * @brief If the object at the given index has a metatable, pushes that metatable onto the stack and returns 1. Otherwise, returns 0 and pushes nothing.
 *
 * @param L The Lua state.
 * @param objindex The index of the object.
 * @return 1 if metatable exists, 0 otherwise.
 */
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);

/**
 * @brief Pushes onto the stack the n-th user value associated with the full userdata at the given index.
 *
 * @param L The Lua state.
 * @param idx The index of the userdata.
 * @param n The user value index.
 * @return Type of the pushed value.
 */
LUA_API int  (lua_getiuservalue) (lua_State *L, int idx, int n);


/*
** set functions (stack -> Lua)
*/

/**
 * @brief Pops a value from the stack and sets it as the new value of global name.
 *
 * @param L The Lua state.
 * @param name The global variable name.
 */
LUA_API void  (lua_setglobal) (lua_State *L, const char *name);

/**
 * @brief Does the equivalent to t[k] = v, where t is the value at the given index, v is the value at the top of the stack, and k is the value just below the top.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 */
LUA_API void  (lua_settable) (lua_State *L, int idx);

/**
 * @brief Does the equivalent to t[k] = v, where t is the value at the given index and v is the value at the top of the stack.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param k The key string.
 */
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);

/**
 * @brief Does the equivalent to t[n] = v, where t is the value at the given index and v is the value at the top of the stack.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param n The integer key.
 */
LUA_API void  (lua_seti) (lua_State *L, int idx, lua_Integer n);

/**
 * @brief Similar to lua_settable, but does a raw assignment (i.e., without metamethods).
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 */
LUA_API void  (lua_rawset) (lua_State *L, int idx);

/**
 * @brief Similar to lua_seti, but does a raw assignment.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param n The integer key.
 */
LUA_API void  (lua_rawseti) (lua_State *L, int idx, lua_Integer n);

/**
 * @brief Similar to lua_settable, but does a raw assignment with a pointer key.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param p The pointer key.
 */
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);

/**
 * @brief Pops a table from the stack and sets it as the new metatable for the value at the given index.
 *
 * @param L The Lua state.
 * @param objindex The index of the object.
 * @return 1 on success.
 */
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);

/**
 * @brief Pops a value from the stack and sets it as the new n-th user value associated with the full userdata at the given index.
 *
 * @param L The Lua state.
 * @param idx The index of the userdata.
 * @param n The user value index.
 * @return 1 on success.
 */
LUA_API int   (lua_setiuservalue) (lua_State *L, int idx, int n);


/*
** 'load' and 'call' functions (load and run Lua code)
*/

/**
 * @brief Calls a function in protected mode.
 *
 * @param L The Lua state.
 * @param nargs Number of arguments.
 * @param nresults Number of results.
 * @param ctx Continuation context.
 * @param k Continuation function.
 */
LUA_API void  (lua_callk) (lua_State *L, int nargs, int nresults,
                           lua_KContext ctx, lua_KFunction k);

/** Calls a function. */
#define lua_call(L,n,r)		lua_callk(L, (n), (r), 0, NULL)

/**
 * @brief Calls a function in protected mode.
 *
 * @param L The Lua state.
 * @param nargs Number of arguments.
 * @param nresults Number of results.
 * @param errfunc Index of error handling function.
 * @param ctx Continuation context.
 * @param k Continuation function.
 * @return Status code.
 */
LUA_API int   (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc,
                            lua_KContext ctx, lua_KFunction k);

/** Calls a function in protected mode. */
#define lua_pcall(L,n,r,f)	lua_pcallk(L, (n), (r), (f), 0, NULL)

/**
 * @brief Loads a Lua chunk without running it.
 *
 * @param L The Lua state.
 * @param reader Reader function.
 * @param dt User data for reader.
 * @param chunkname Chunk name.
 * @param mode Loading mode ("b", "t", "bt").
 * @return Status code.
 */
LUA_API int   (lua_load) (lua_State *L, lua_Reader reader, void *dt,
                          const char *chunkname, const char *mode);

/**
 * @brief Dumps a function as a binary chunk.
 *
 * @param L The Lua state.
 * @param writer Writer function.
 * @param data User data for writer.
 * @param strip Whether to strip debug information.
 * @return Status code.
 */
LUA_API int (lua_dump) (lua_State *L, lua_Writer writer, void *data, int strip);

/**
 * @name Obfuscation Flags
 * @{
 */
#define LUA_OBFUSCATE_NONE          0       /**< No obfuscation */
#define LUA_OBFUSCATE_CFF           (1<<0)  /**< Control Flow Flattening */
#define LUA_OBFUSCATE_BLOCK_SHUFFLE (1<<1)  /**< Basic Block Shuffle */
#define LUA_OBFUSCATE_BOGUS_BLOCKS  (1<<2)  /**< Insert Bogus Blocks */
#define LUA_OBFUSCATE_STATE_ENCODE  (1<<3)  /**< State Encoding */
/** @} */

/**
 * @brief Dumps a function with obfuscation.
 *
 * @param L The Lua state.
 * @param writer Writer function.
 * @param data User data for writer.
 * @param strip Whether to strip debug information.
 * @param obfuscate_flags Flags for obfuscation.
 * @param seed Random seed.
 * @param log_path Path for logging obfuscation details.
 * @return Status code.
 */
LUA_API int (lua_dump_obfuscated) (lua_State *L, lua_Writer writer, void *data,
                                   int strip, int obfuscate_flags, unsigned int seed,
                                   const char *log_path);


/*
** coroutine functions
*/

/**
 * @brief Yields a coroutine.
 *
 * @param L The Lua state.
 * @param nresults Number of results.
 * @param ctx Continuation context.
 * @param k Continuation function.
 * @return Status code.
 */
LUA_API int  (lua_yieldk)     (lua_State *L, int nresults, lua_KContext ctx,
                               lua_KFunction k);

/**
 * @brief Resumes a coroutine.
 *
 * @param L The Lua state.
 * @param from The thread that is resuming this one.
 * @param narg Number of arguments.
 * @param nres Output pointer for number of results.
 * @return Status code.
 */
LUA_API int  (lua_resume)     (lua_State *L, lua_State *from, int narg,
                               int *nres);

/**
 * @brief Returns the status of the thread L.
 *
 * @param L The Lua state.
 * @return Status code.
 */
LUA_API int  (lua_status)     (lua_State *L);

/**
 * @brief Returns 1 if the given coroutine can yield, and 0 otherwise.
 *
 * @param L The Lua state.
 * @return 1 if yieldable, 0 otherwise.
 */
LUA_API int (lua_isyieldable) (lua_State *L);

/** Yields a coroutine. */
#define lua_yield(L,n)		lua_yieldk(L, (n), 0, NULL)


/*
** Warning-related functions
*/

/**
 * @brief Sets the warning function to be used by Lua.
 *
 * @param L The Lua state.
 * @param f The warning function.
 * @param ud User data.
 */
LUA_API void (lua_setwarnf) (lua_State *L, lua_WarnFunction f, void *ud);

/**
 * @brief Emits a warning.
 *
 * @param L The Lua state.
 * @param msg Warning message.
 * @param tocont Continuation flag.
 */
LUA_API void (lua_warning)  (lua_State *L, const char *msg, int tocont);


/*
** garbage-collection options
*/

/**
 * @name Garbage Collection Options
 * @{
 */
#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7

#define LUA_GCISRUNNING		9
#define LUA_GCGEN		10
#define LUA_GCINC		11
#define LUA_GCPARAM		12
/** @} */

/*
** garbage-collection parameters
*/
/**
 * @name Garbage Collection Parameters
 * @{
 */
/* parameters for generational mode */
#define LUA_GCPMINORMUL		0  /* control minor collections */
#define LUA_GCPMAJORMINOR	1  /* control shift major->minor */
#define LUA_GCPMINORMAJOR	2  /* control shift minor->major */

/* parameters for incremental mode */
#define LUA_GCPPAUSE		3  /* size of pause between successive GCs */
#define LUA_GCPSTEPMUL		4  /* GC "speed" */
#define LUA_GCPSTEPSIZE		5  /* GC granularity */

/* number of parameters */
#define LUA_GCPN		6
/** @} */


/**
 * @brief Controls the garbage collector.
 *
 * @param L The Lua state.
 * @param what What operation to perform.
 * @param ... Additional arguments.
 * @return Dependent on 'what'.
 */
LUA_API int (lua_gc) (lua_State *L, int what, ...);


/*
** miscellaneous functions
*/

/**
 * @brief Generates a Lua error, using the value at the top of the stack as the error object.
 *
 * @param L The Lua state.
 * @return Never returns.
 */
LUA_API int   (lua_error) (lua_State *L);

/**
 * @brief Pops a key from the stack, and pushes a key-value pair from the table at the given index.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @return 1 if next element exists, 0 otherwise.
 */
LUA_API int   (lua_next) (lua_State *L, int idx);

/**
 * @brief Concatenates the n values at the top of the stack, pops them, and leaves the result at the top.
 *
 * @param L The Lua state.
 * @param n Number of elements to concatenate.
 */
LUA_API void  (lua_concat) (lua_State *L, int n);

/**
 * @brief Returns the length of the value at the given index.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
LUA_API void  (lua_len)    (lua_State *L, int idx);

/*
** Memory Management Enhanced API
*/

/**
 * @brief Returns the memory usage of the Lua state.
 *
 * @param L The Lua state.
 * @return Memory usage in bytes.
 */
LUA_API size_t (lua_getmemoryusage) (lua_State *L);

/**
 * @brief Forces a full garbage collection cycle.
 *
 * @param L The Lua state.
 */
LUA_API void   (lua_gc_force) (lua_State *L);

/*
** Numeric Operation Enhanced API
*/

/**
 * @brief Converts the value at the given index to an integer, with safety checks.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param isnum Optional output flag for number validity.
 * @param overflow Optional output flag for overflow detection.
 * @return The integer value.
 */
LUA_API lua_Integer (lua_tointeger_safe) (lua_State *L, int idx, int *isnum, int *overflow);

/*
** Table Operation Enhanced API
*/

/**
 * @brief Pre-extends the table at the given index to accommodate n elements.
 *
 * @param L The Lua state.
 * @param idx The index of the table.
 * @param n Number of elements.
 */
LUA_API void (lua_table_iextend) (lua_State *L, int idx, int n);

#define LUA_N2SBUFFSZ	64

/**
 * @brief Converts the number at the given index to a string in the buffer.
 *
 * @param L The Lua state.
 * @param idx The index.
 * @param buff Buffer to hold the string.
 * @return Length of the string.
 */
LUA_API unsigned  (lua_numbertocstring) (lua_State *L, int idx, char *buff);

/**
 * @brief Converts a string to a number.
 *
 * @param L The Lua state.
 * @param s The string.
 * @return Length of the string consumed.
 */
LUA_API size_t  (lua_stringtonumber) (lua_State *L, const char *s);

/**
 * @brief Returns the memory allocation function of a given state.
 *
 * @param L The Lua state.
 * @param ud User data output pointer.
 * @return The allocator function.
 */
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);

/**
 * @brief Changes the collector function of a given state to f with user data ud.
 *
 * @param L The Lua state.
 * @param f The allocator function.
 * @param ud User data.
 */
LUA_API void      (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);

/**
 * @brief Marks the value at the given index as to-be-closed.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
LUA_API void (lua_toclose) (lua_State *L, int idx);

/**
 * @brief Closes the slot at the given index.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
LUA_API void (lua_closeslot) (lua_State *L, int idx);

/**
 * @brief Performs a hotfix operation.
 *
 * @param L The Lua state.
 * @param oldidx Index of old value.
 * @param newidx Index of new value.
 */
LUA_API void (luaB_hotfix) (lua_State *L, int oldidx, int newidx);

/*
** tcc support functions
*/
LUA_API void  (lua_tcc_prologue) (lua_State *L, int nparams, int maxstack);
LUA_API void  (lua_tcc_gettabup) (lua_State *L, int upval, const char *k, int dest);
LUA_API void  (lua_tcc_settabup) (lua_State *L, int upval, const char *k, int val_idx);
LUA_API void  (lua_tcc_loadk_str) (lua_State *L, int dest, const char *s);
LUA_API void  (lua_tcc_loadk_int) (lua_State *L, int dest, lua_Integer v);
LUA_API void  (lua_tcc_loadk_flt) (lua_State *L, int dest, lua_Number v);
LUA_API int   (lua_tcc_in) (lua_State *L, int val_idx, int container_idx);
LUA_API void  (lua_tcc_push_args) (lua_State *L, int start_reg, int count);
LUA_API void  (lua_tcc_store_results) (lua_State *L, int start_reg, int count);


/*
** {===================================================
** some useful macros
** ====================================================
*/

/**
 * @brief Returns the pointer to the extra space associated with the Lua state.
 *
 * @param L The Lua state.
 */
#define lua_getextraspace(L)	((void *)((char *)(L) - LUA_EXTRASPACE))

/**
 * @brief Converts the Lua value at the given index to a number.
 *
 * @param L The Lua state.
 * @param i The index.
 */
#define lua_tonumber(L,i)	lua_tonumberx(L,(i),NULL)

/**
 * @brief Converts the Lua value at the given index to an integer.
 *
 * @param L The Lua state.
 * @param i The index.
 */
#define lua_tointeger(L,i)	lua_tointegerx(L,(i),NULL)

/**
 * @brief Pops n elements from the stack.
 *
 * @param L The Lua state.
 * @param n Number of elements to pop.
 */
#define lua_pop(L,n)		lua_settop(L, -(n)-1)

/**
 * @brief Creates a new empty table and pushes it onto the stack.
 *
 * @param L The Lua state.
 */
#define lua_newtable(L)		lua_createtable(L, 0, 0)

/**
 * @brief Registers a C function as a global variable.
 *
 * @param L The Lua state.
 * @param n The name of the global variable.
 * @param f The C function.
 */
#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

/**
 * @brief Pushes a C function onto the stack.
 *
 * @param L The Lua state.
 * @param f The C function.
 */
#define lua_pushcfunction(L,f)	lua_pushcclosure(L, (f), 0)

/**
 * @brief Returns true if the value at the given index is a function.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isfunction(L,n)	(lua_type(L, (n)) == LUA_TFUNCTION)

/**
 * @brief Returns true if the value at the given index is a table.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE)

/**
 * @brief Returns true if the value at the given index is a light userdata.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TLIGHTUSERDATA)

/**
 * @brief Returns true if the value at the given index is a raw pointer.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_ispointer(L,n)	(lua_type(L, (n)) == LUA_TPOINTER)

/**
 * @brief Returns true if the value at the given index is nil.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isnil(L,n)		(lua_type(L, (n)) == LUA_TNIL)

/**
 * @brief Returns true if the value at the given index is a boolean.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)

/**
 * @brief Returns true if the value at the given index is a thread.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)

/**
 * @brief Returns true if the value at the given index is none (invalid).
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isnone(L,n)		(lua_type(L, (n)) == LUA_TNONE)

/**
 * @brief Returns true if the value at the given index is none or nil.
 *
 * @param L The Lua state.
 * @param n The index.
 */
#define lua_isnoneornil(L, n)	(lua_type(L, (n)) <= 0)

/**
 * @brief Pushes a literal string onto the stack.
 *
 * @param L The Lua state.
 * @param s The string literal.
 */
#define lua_pushliteral(L, s)	lua_pushstring(L, "" s)

/**
 * @brief Pushes the global table onto the stack.
 *
 * @param L The Lua state.
 */
#define lua_pushglobaltable(L)  \
	((void)lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS))

/**
 * @brief Converts the Lua value at the given index to a string.
 *
 * @param L The Lua state.
 * @param i The index.
 */
#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)


/**
 * @brief Inserts the element at the top into the given index.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
#define lua_insert(L,idx)	lua_rotate(L, (idx), 1)

/**
 * @brief Removes the element at the given index.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
#define lua_remove(L,idx)	(lua_rotate(L, (idx), -1), lua_pop(L, 1))

/**
 * @brief Replaces the element at the given index with the top element.
 *
 * @param L The Lua state.
 * @param idx The index.
 */
#define lua_replace(L,idx)	(lua_copy(L, -1, (idx)), lua_pop(L, 1))

/* }=================================================== */


/*
** {===================================================
** compatibility macros
** ====================================================
*/
#if defined(LUA_COMPAT_APIINTCASTS)

#define lua_pushunsigned(L,n)	lua_pushinteger(L, (lua_Integer)(n))
#define lua_tounsignedx(L,i,is)	((lua_Unsigned)lua_tointegerx(L,i,is))
#define lua_tounsigned(L,i)	lua_tounsignedx(L,(i),NULL)

#endif

#define lua_newuserdata(L,s)	lua_newuserdatauv(L,s,1)
#define lua_getuservalue(L,idx)	lua_getiuservalue(L,idx,1)
#define lua_setuservalue(L,idx)	lua_setiuservalue(L,idx,1)
#define luaL_typerror(L, narg, tname) \
    (luaL_argerror((L), (narg), lua_pushfstring((L), "%s expected, got %s", \
                                                (tname), luaL_typename((L), (narg)))))

#define LUA_NUMTAGS		LUA_NUMTYPES

/* }=================================================== */

/*
** {===========================================================
** Debug API
** ============================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)


/**
 * @brief Gets information about a specific level of the stack.
 *
 * @param L The Lua state.
 * @param level The stack level.
 * @param ar Debug structure to fill.
 * @return 1 on success, 0 on failure.
 */
LUA_API int (lua_getstack) (lua_State *L, int level, lua_Debug *ar);

/**
 * @brief Gets information about a specific function or function invocation.
 *
 * @param L The Lua state.
 * @param what String describing what information to retrieve.
 * @param ar Debug structure to fill.
 * @return 1 on success, 0 on failure.
 */
LUA_API int (lua_getinfo) (lua_State *L, const char *what, lua_Debug *ar);

/**
 * @brief Gets the name and value of a local variable of a given activation record or function.
 *
 * @param L The Lua state.
 * @param ar Debug structure.
 * @param n Index of the local variable.
 * @return Name of the variable.
 */
LUA_API const char *(lua_getlocal) (lua_State *L, const lua_Debug *ar, int n);

/**
 * @brief Sets the value of a local variable.
 *
 * @param L The Lua state.
 * @param ar Debug structure.
 * @param n Index of the local variable.
 * @return Name of the variable.
 */
LUA_API const char *(lua_setlocal) (lua_State *L, const lua_Debug *ar, int n);

/**
 * @brief Gets information about a closure's upvalue.
 *
 * @param L The Lua state.
 * @param funcindex Index of the function.
 * @param n Index of the upvalue.
 * @return Name of the upvalue.
 */
LUA_API const char *(lua_getupvalue) (lua_State *L, int funcindex, int n);

/**
 * @brief Sets the value of a closure's upvalue.
 *
 * @param L The Lua state.
 * @param funcindex Index of the function.
 * @param n Index of the upvalue.
 * @return Name of the upvalue.
 */
LUA_API const char *(lua_setupvalue) (lua_State *L, int funcindex, int n);

/**
 * @brief Returns a unique identifier for the upvalue numbered n from the closure at index fidx.
 *
 * @param L The Lua state.
 * @param fidx Index of the function.
 * @param n Index of the upvalue.
 * @return Unique identifier.
 */
LUA_API void *(lua_upvalueid) (lua_State *L, int fidx, int n);

/**
 * @brief Makes the n1-th upvalue of the Lua closure at index fidx1 refer to the n2-th upvalue of the Lua closure at index fidx2.
 *
 * @param L The Lua state.
 * @param fidx1 Index of first function.
 * @param n1 Index of first upvalue.
 * @param fidx2 Index of second function.
 * @param n2 Index of second upvalue.
 */
LUA_API void  (lua_upvaluejoin) (lua_State *L, int fidx1, int n1,
                                               int fidx2, int n2);

/**
 * @brief Sets the debugging hook function.
 *
 * @param L The Lua state.
 * @param func Hook function.
 * @param mask Event mask.
 * @param count Instruction count.
 */
LUA_API void (lua_sethook) (lua_State *L, lua_Hook func, int mask, int count);

/**
 * @brief Returns the current hook function.
 *
 * @param L The Lua state.
 * @return The hook function.
 */
LUA_API lua_Hook (lua_gethook) (lua_State *L);

/**
 * @brief Returns the current hook mask.
 *
 * @param L The Lua state.
 * @return The hook mask.
 */
LUA_API int (lua_gethookmask) (lua_State *L);

/**
 * @brief Returns the current hook count.
 *
 * @param L The Lua state.
 * @return The hook count.
 */
LUA_API int (lua_gethookcount) (lua_State *L);

/**
 * @brief Sets the C stack limit.
 *
 * @param L The Lua state.
 * @param limit The new limit.
 * @return The old limit.
 */
LUA_API int (lua_setcstacklimit) (lua_State *L, unsigned int limit);

/**
 * @brief Debug information structure.
 */
struct lua_Debug {
  int event; /**< Event code. */
  const char *name;	/**< (n) Name of the function. */
  const char *namewhat;	/**< (n) 'global', 'local', 'field', 'method' */
  const char *what;	/**< (S) 'Lua', 'C', 'main', 'tail' */
  const char *source;	/**< (S) Source code name (e.g. file name) */
  size_t srclen;	/**< (S) Source length */
  int currentline;	/**< (l) Current line number */
  int linedefined;	/**< (S) Line where function is defined */
  int lastlinedefined;	/**< (S) Last line of function definition */
  unsigned char nups;	/**< (u) number of upvalues */
  unsigned char nparams;/**< (u) number of parameters */
  char isvararg;        /**< (u) is vararg */
  unsigned char extraargs;  /**< (t) number of extra arguments */
  char istailcall;	/**< (t) is tail call */
  int ftransfer;   /**< (r) index of first value transferred */
  int ntransfer;   /**< (r) number of transferred values */
  char short_src[LUA_IDSIZE]; /**< (S) Short source name */
  char ishotfixed;  /**< (h) whether function was hotfixed */
  
  /* private part */
  struct CallInfo *i_ci;  /**< active function */
};

/* }=========================================================== */


#define LUAI_TOSTRAUX(x)	#x
#define LUAI_TOSTR(x)		LUAI_TOSTRAUX(x)


/*
** Object-Oriented API
*/
LUA_API void  (lua_newclass) (lua_State *L, const char *name);
LUA_API void  (lua_inherit) (lua_State *L, int child_idx, int parent_idx);
LUA_API void  (lua_newobject) (lua_State *L, int class_idx, int nargs);
LUA_API void  (lua_setmethod) (lua_State *L, int class_idx, const char *name, int func_idx);
LUA_API void  (lua_setstatic) (lua_State *L, int class_idx, const char *name, int value_idx);
LUA_API void  (lua_getprop) (lua_State *L, int obj_idx, const char *key);
LUA_API void  (lua_setprop) (lua_State *L, int obj_idx, const char *key, int value_idx);
LUA_API int   (lua_instanceof) (lua_State *L, int obj_idx, int class_idx);
LUA_API void  (lua_implement) (lua_State *L, int class_idx, int interface_idx);
LUA_API void  (lua_getsuper) (lua_State *L, int obj_idx, const char *name);

LUA_API int   (lua_spaceship) (lua_State *L, int idx1, int idx2);
LUA_API int   (lua_is) (lua_State *L, int idx, const char *type_name);
LUA_API void  (lua_checktype) (lua_State *L, int idx, const char *type_name);
LUA_API void  (lua_newnamespace) (lua_State *L, const char *name);
LUA_API void  (lua_linknamespace) (lua_State *L, int idx1, int idx2);
LUA_API void  (lua_newsuperstruct) (lua_State *L, const char *name);
LUA_API void  (lua_setsuper) (lua_State *L, int idx, int key_idx, int val_idx);
LUA_API void  (lua_slice) (lua_State *L, int idx, int start_idx, int end_idx, int step_idx);
LUA_API void  (lua_setifaceflag) (lua_State *L, int idx);
LUA_API void  (lua_addmethod) (lua_State *L, int idx, const char *name, int nparams);
LUA_API void  (lua_getcmds) (lua_State *L);
LUA_API void  (lua_getops) (lua_State *L);
LUA_API void  (lua_errnnil) (lua_State *L, int idx, const char *msg);


/******************************************************************************
* Copyright (C) 1994-2025 Lua.org, PUC-Rio.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
