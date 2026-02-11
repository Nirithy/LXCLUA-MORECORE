/*
** $Id: lauxlib.h $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "luaconf.h"
#include "lua.h"


/** Name of global table */
#define LUA_GNAME	"_G"


/**
 * @brief Opaque structure for a Lua auxiliary buffer.
 */
typedef struct luaL_Buffer luaL_Buffer;


/** Extra error code for 'luaL_loadfilex' */
#define LUA_ERRFILE     (LUA_ERRERR+1)


/** Key, in the registry, for table of loaded modules */
#define LUA_LOADED_TABLE	"_LOADED"


/** Key, in the registry, for table of preloaded loaders */
#define LUA_PRELOAD_TABLE	"_PRELOAD"


/**
 * @brief Type for arrays of functions to be registered by luaL_setfuncs.
 *
 * name is the function name and func is a pointer to the function.
 * Any array of luaL_Reg must end with a sentinel entry in which both name and func are NULL.
 */
typedef struct luaL_Reg {
  const char *name;  /**< Function name. */
  lua_CFunction func; /**< Function pointer. */
} luaL_Reg;


/** Number of sizes to check for version compatibility */
#define LUAL_NUMSIZES	(sizeof(lua_Integer)*16 + sizeof(lua_Number))

/**
 * @brief Checks whether the core running the call, the core that created the Lua state, and the code making the call are all using the same version of Lua.
 *
 * @param L The Lua state.
 * @param ver Version number.
 * @param sz Size check value.
 */
LUALIB_API void (luaL_checkversion_) (lua_State *L, lua_Number ver, size_t sz);

/**
 * @brief Macro to check version with current values.
 *
 * @param L The Lua state.
 */
#define luaL_checkversion(L)  \
	  luaL_checkversion_(L, LUA_VERSION_NUM, LUAL_NUMSIZES)

/**
 * @brief Pushes onto the stack the field e from the metatable of the object at index obj and returns the type of the pushed value.
 *
 * @param L The Lua state.
 * @param obj Index of the object.
 * @param e Name of the metafield.
 * @return Type of the pushed value, or LUA_TNIL if no metatable/metafield.
 */
LUALIB_API int (luaL_getmetafield) (lua_State *L, int obj, const char *e);

/**
 * @brief Calls a metamethod.
 *
 * @param L The Lua state.
 * @param obj Index of the object.
 * @param e Name of the metamethod.
 * @return 1 if call successful, 0 otherwise.
 */
LUALIB_API int (luaL_callmeta) (lua_State *L, int obj, const char *e);

/**
 * @brief Converts the value at the given index to a C string.
 *
 * @param L The Lua state.
 * @param idx Index of the value.
 * @param len Optional output for string length.
 * @return The C string.
 */
LUALIB_API const char *(luaL_tolstring) (lua_State *L, int idx, size_t *len);

/**
 * @brief Raises an error reporting a problem with argument arg of the C function that called it.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param extramsg Extra message.
 * @return Never returns.
 */
LUALIB_API int (luaL_argerror) (lua_State *L, int arg, const char *extramsg);

/**
 * @brief Raises a type error for the argument arg of the C function that called it.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param tname Expected type name.
 * @return Never returns.
 */
LUALIB_API int (luaL_typeerror) (lua_State *L, int arg, const char *tname);

/**
 * @brief Checks whether the function argument arg is a string and returns this string.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param l Optional output for string length.
 * @return The string.
 */
LUALIB_API const char *(luaL_checklstring) (lua_State *L, int arg,
                                                          size_t *l);

/**
 * @brief If the function argument arg is a string, returns this string. If this argument is absent or is nil, returns d.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param def Default string.
 * @param l Optional output for string length.
 * @return The string.
 */
LUALIB_API const char *(luaL_optlstring) (lua_State *L, int arg,
                                          const char *def, size_t *l);

/**
 * @brief Checks whether the function argument arg is a number and returns this number.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @return The number.
 */
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, int arg);

/**
 * @brief If the function argument arg is a number, returns this number. If this argument is absent or is nil, returns d.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param def Default number.
 * @return The number.
 */
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, int arg, lua_Number def);

/**
 * @brief Checks whether the function argument arg is an integer and returns this integer.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @return The integer.
 */
LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int arg);

/**
 * @brief If the function argument arg is an integer, returns this integer. If this argument is absent or is nil, returns d.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param def Default integer.
 * @return The integer.
 */
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int arg,
                                          lua_Integer def);

/**
 * @brief Checks whether the stack has space for more sz elements.
 *
 * @param L The Lua state.
 * @param sz Number of elements.
 * @param msg Error message if check fails.
 */
LUALIB_API void (luaL_checkstack) (lua_State *L, int sz, const char *msg);

/**
 * @brief Checks whether the function argument arg has type t.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param t Expected type.
 */
LUALIB_API void (luaL_checktype) (lua_State *L, int arg, int t);

/**
 * @brief Checks whether the function has an argument of any type (including nil) at position arg.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 */
LUALIB_API void (luaL_checkany) (lua_State *L, int arg);

/**
 * @brief If the registry already has the key tname, returns 0. Otherwise, creates a new table to be used as a metatable for userdata.
 *
 * @param L The Lua state.
 * @param tname Metatable name.
 * @return 1 if new, 0 if already exists.
 */
LUALIB_API int   (luaL_newmetatable) (lua_State *L, const char *tname);

/**
 * @brief Sets the metatable of the object at the top of the stack as the metatable associated with name tname in the registry.
 *
 * @param L The Lua state.
 * @param tname Metatable name.
 */
LUALIB_API void  (luaL_setmetatable) (lua_State *L, const char *tname);

/**
 * @brief This function works like luaL_checkudata, except that, when the test fails, it returns NULL instead of raising an error.
 *
 * @param L The Lua state.
 * @param ud Userdata index.
 * @param tname Metatable name.
 * @return Pointer to userdata or NULL.
 */
LUALIB_API void *(luaL_testudata) (lua_State *L, int ud, const char *tname);

/**
 * @brief Checks whether the function argument arg is a userdata of the type tname.
 *
 * @param L The Lua state.
 * @param ud Userdata index.
 * @param tname Metatable name.
 * @return Pointer to userdata.
 */
LUALIB_API void *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

/**
 * @brief Pushes onto the stack a string identifying the current position of the control at level lvl in the call stack.
 *
 * @param L The Lua state.
 * @param lvl Stack level.
 */
LUALIB_API void (luaL_where) (lua_State *L, int lvl);

/**
 * @brief Raises an error.
 *
 * @param L The Lua state.
 * @param fmt Format string.
 * @param ... Arguments.
 * @return Never returns.
 */
LUALIB_API int (luaL_error) (lua_State *L, const char *fmt, ...);

/**
 * @brief Checks whether the function argument arg is a string and searches for this string in the array lst.
 *
 * @param L The Lua state.
 * @param arg Argument index.
 * @param def Default string (if NULL, arg is mandatory).
 * @param lst Null-terminated array of strings.
 * @return Index in the array.
 */
LUALIB_API int (luaL_checkoption) (lua_State *L, int arg, const char *def,
                                   const char *const lst[]);

/**
 * @brief Produces the return values for file-related functions in the standard library (io.open, os.rename, etc.).
 *
 * @param L The Lua state.
 * @param stat Status (non-zero for success).
 * @param fname Filename (for error message).
 * @return 1 on success, multiple values on failure.
 */
LUALIB_API int (luaL_fileresult) (lua_State *L, int stat, const char *fname);

/**
 * @brief Produces the return values for process-related functions in the standard library (os.execute).
 *
 * @param L The Lua state.
 * @param stat Status code.
 * @return Return values.
 */
LUALIB_API int (luaL_execresult) (lua_State *L, int stat);


/* predefined references */
#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

/**
 * @brief Creates and returns a reference, in the table at index t, for the object at the top of the stack (and pops the object).
 *
 * @param L The Lua state.
 * @param t Table index.
 * @return The reference.
 */
LUALIB_API int (luaL_ref) (lua_State *L, int t);

/**
 * @brief Releases the reference ref from the table at index t.
 *
 * @param L The Lua state.
 * @param t Table index.
 * @param ref Reference to unref.
 */
LUALIB_API void (luaL_unref) (lua_State *L, int t, int ref);

/**
 * @brief Loads a file as a Lua chunk.
 *
 * @param L The Lua state.
 * @param filename File path.
 * @param mode Loading mode.
 * @return Status code.
 */
LUALIB_API int (luaL_loadfilex) (lua_State *L, const char *filename,
                                               const char *mode);

/**
 * @brief Macro to load file with default mode.
 *
 * @param L The Lua state.
 * @param f Filename.
 */
#define luaL_loadfile(L,f)	luaL_loadfilex(L,f,NULL)

/**
 * @brief Loads a buffer as a Lua chunk.
 *
 * @param L The Lua state.
 * @param buff Buffer.
 * @param sz Size of buffer.
 * @param name Chunk name.
 * @param mode Loading mode.
 * @return Status code.
 */
LUALIB_API int (luaL_loadbufferx) (lua_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);

/**
 * @brief Loads a string as a Lua chunk.
 *
 * @param L The Lua state.
 * @param s String.
 * @return Status code.
 */
LUALIB_API int (luaL_loadstring) (lua_State *L, const char *s);

/**
 * @brief Creates a new Lua state.
 *
 * @return The new state.
 */
LUALIB_API lua_State *(luaL_newstate) (void);

/**
 * @brief Returns the "length" of the value at the given index as a number.
 *
 * @param L The Lua state.
 * @param idx Index.
 * @return Length.
 */
LUALIB_API lua_Integer (luaL_len) (lua_State *L, int idx);

/**
 * @brief Adds a copy of string s to the buffer b, replacing any occurrence of the string p with the string r.
 *
 * @param b The buffer.
 * @param s Source string.
 * @param p Pattern to replace.
 * @param r Replacement string.
 */
LUALIB_API void (luaL_addgsub) (luaL_Buffer *b, const char *s,
                                     const char *p, const char *r);

/**
 * @brief Creates a copy of string s, replacing any occurrence of the string p with the string r.
 *
 * @param L The Lua state.
 * @param s Source string.
 * @param p Pattern to replace.
 * @param r Replacement string.
 * @return The new string.
 */
LUALIB_API const char *(luaL_gsub) (lua_State *L, const char *s,
                                    const char *p, const char *r);

/**
 * @brief Registers all functions in the array l into the table on the top of the stack.
 *
 * @param L The Lua state.
 * @param l Array of luaL_Reg.
 * @param nup Number of upvalues.
 */
LUALIB_API void (luaL_setfuncs) (lua_State *L, const luaL_Reg *l, int nup);

/**
 * @brief Ensures that the value t[fname], where t is the value at index idx, is a table, and pushes that table onto the stack.
 *
 * @param L The Lua state.
 * @param idx Table index.
 * @param fname Field name.
 * @return 1 if found/created, 0 otherwise.
 */
LUALIB_API int (luaL_getsubtable) (lua_State *L, int idx, const char *fname);

/**
 * @brief Creates and pushes a traceback of the stack L1.
 *
 * @param L The Lua state where the trace is pushed.
 * @param L1 The Lua state to trace.
 * @param msg Optional message.
 * @param level Starting level.
 */
LUALIB_API void (luaL_traceback) (lua_State *L, lua_State *L1,
                                  const char *msg, int level);

/**
 * @brief If modname is not already present in package.loaded, calls function openf with string modname as an argument and sets the call result in package.loaded[modname].
 *
 * @param L The Lua state.
 * @param modname Module name.
 * @param openf Open function.
 * @param glb If true, also sets the module into the global variable modname.
 */
LUALIB_API void (luaL_requiref) (lua_State *L, const char *modname,
                                 lua_CFunction openf, int glb);

/*
** ====================================================
** some useful macros
** ====================================================
*/

/**
 * @brief Creates a new table with a size hint for the given library.
 *
 * @param L The Lua state.
 * @param l The library array.
 */
#define luaL_newlibtable(L,l)	\
  lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

/**
 * @brief Registers a library in the new table.
 *
 * @param L The Lua state.
 * @param l The library array.
 */
#define luaL_newlib(L,l)  \
  (luaL_checkversion(L), luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

/**
 * @brief Checks a condition and raises an error if it fails.
 *
 * @param L The Lua state.
 * @param cond The condition.
 * @param arg The argument index.
 * @param extramsg The error message.
 */
#define luaL_argcheck(L, cond,arg,extramsg)	\
	((void)(luai_likely(cond) || luaL_argerror(L, (arg), (extramsg))))

/**
 * @brief Checks if a condition is met, otherwise raises a type error.
 *
 * @param L The Lua state.
 * @param cond The condition.
 * @param arg The argument index.
 * @param tname The expected type name.
 */
#define luaL_argexpected(L,cond,arg,tname)	\
	((void)(luai_likely(cond) || luaL_typeerror(L, (arg), (tname))))

/**
 * @brief Checks if the argument is a string.
 *
 * @param L The Lua state.
 * @param n The argument index.
 */
#define luaL_checkstring(L,n)	(luaL_checklstring(L, (n), NULL))

/**
 * @brief Checks if the argument is a string (optional).
 *
 * @param L The Lua state.
 * @param n The argument index.
 * @param d Default string.
 */
#define luaL_optstring(L,n,d)	(luaL_optlstring(L, (n), (d), NULL))

/**
 * @brief Returns the type name of the value at the given index.
 *
 * @param L The Lua state.
 * @param i The index.
 */
#define luaL_typename(L,i)	lua_typename(L, lua_type(L,(i)))

/**
 * @brief Loads and runs the given file.
 *
 * @param L The Lua state.
 * @param fn The filename.
 */
#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

/**
 * @brief Loads and runs the given string.
 *
 * @param L The Lua state.
 * @param s The string.
 */
#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

/**
 * @brief Gets the metatable of the given object.
 *
 * @param L The Lua state.
 * @param n The object name.
 */
#define luaL_getmetatable(L,n)	(lua_getfield(L, LUA_REGISTRYINDEX, (n)))

/**
 * @brief Optional parameter helper.
 *
 * @param L The Lua state.
 * @param f Check function.
 * @param n The argument index.
 * @param d Default value.
 */
#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

/**
 * @brief Loads a buffer.
 *
 * @param L The Lua state.
 * @param s The buffer.
 * @param sz Size.
 * @param n Name.
 */
#define luaL_loadbuffer(L,s,sz,n)	luaL_loadbufferx(L,s,sz,n,NULL)


/*
** Perform arithmetic operations on lua_Integer values with wrap-around
** semantics, as the Lua core does.
*/
#define luaL_intop(op,v1,v2)  \
	((lua_Integer)((lua_Unsigned)(v1) op (lua_Unsigned)(v2)))


/* push the value used to represent failure/error */
#define luaL_pushfail(L)	lua_pushnil(L)


/*
** Internal assertions for in-house debugging
*/
#if !defined(lua_assert)

#if defined LUAI_ASSERT
  #include <assert.h>
  #define lua_assert(c)		assert(c)
#else
  #define lua_assert(c)		((void)0)
#endif

#endif



/*
** {===========================================
** Generic Buffer manipulation
** ============================================
*/

/**
 * @brief String buffer for building Lua strings.
 */
struct luaL_Buffer {
  char *b;  /**< buffer address */
  size_t size;  /**< buffer size */
  size_t n;  /**< number of characters in buffer */
  lua_State *L; /**< Lua state */
  union {
    LUAI_MAXALIGN;  /**< ensure maximum alignment for buffer */
    char b[LUAL_BUFFERSIZE];  /**< initial buffer */
  } init;
};


#define luaL_bufflen(bf)	((bf)->n)
#define luaL_buffaddr(bf)	((bf)->b)


#define luaL_addchar(B,c) \
  ((void)((B)->n < (B)->size || luaL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define luaL_addsize(B,s)	((B)->n += (s))

#define luaL_buffsub(B,s)	((B)->n -= (s))

/**
 * @brief Initializes a buffer B.
 *
 * @param L The Lua state.
 * @param B The buffer.
 */
LUALIB_API void (luaL_buffinit) (lua_State *L, luaL_Buffer *B);

/**
 * @brief Returns an address to a space of size sz where you can copy a string to be added to buffer B.
 *
 * @param B The buffer.
 * @param sz Size needed.
 * @return Pointer to the space.
 */
LUALIB_API char *(luaL_prepbuffsize) (luaL_Buffer *B, size_t sz);

/**
 * @brief Adds the string pointed to by s with length l to the buffer B.
 *
 * @param B The buffer.
 * @param s String.
 * @param l Length.
 */
LUALIB_API void (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);

/**
 * @brief Adds the zero-terminated string pointed to by s to the buffer B.
 *
 * @param B The buffer.
 * @param s String.
 */
LUALIB_API void (luaL_addstring) (luaL_Buffer *B, const char *s);

/**
 * @brief Adds the value at the top of the stack to the buffer B.
 *
 * @param B The buffer.
 */
LUALIB_API void (luaL_addvalue) (luaL_Buffer *B);

/**
 * @brief Finishes the use of buffer B leaving the final string on the top of the stack.
 *
 * @param B The buffer.
 */
LUALIB_API void (luaL_pushresult) (luaL_Buffer *B);

/**
 * @brief Equivalent to luaL_pushresult, but assuming the buffer was initialized with luaL_buffinitsize.
 *
 * @param B The buffer.
 * @param sz Final size.
 */
LUALIB_API void (luaL_pushresultsize) (luaL_Buffer *B, size_t sz);

/**
 * @brief Initializes a buffer B with a pre-allocated size.
 *
 * @param L The Lua state.
 * @param B The buffer.
 * @param sz Size hint.
 * @return Pointer to the buffer's string area.
 */
LUALIB_API char *(luaL_buffinitsize) (lua_State *L, luaL_Buffer *B, size_t sz);

/* compatibility with old module system */
#if defined(LUA_COMPAT_MODULE)
//mod DifierLine
LUALIB_API const char *luaL_findtable (lua_State *L, int idx,
                                       const char *fname, int szhint);
LUALIB_API void (luaL_pushmodule) (lua_State *L, const char *modname,
                                   int sizehint);
LUALIB_API void (luaL_openlib) (lua_State *L, const char *libname,
                                const luaL_Reg *l, int nup);

#define luaL_register(L,n,l)	(luaL_openlib(L,(n),(l),0))

#endif

#define luaL_prepbuffer(B)	luaL_prepbuffsize(B, LUAL_BUFFERSIZE)

/* }=========================================== */



/*
** {===========================================
** File handles for IO library
** ============================================
*/

/*
** A file handle is a userdata with metatable 'LUA_FILEHANDLE' and
** initial structure 'luaL_Stream' (it may contain other fields
** after that initial structure).
*/

#define LUA_FILEHANDLE          "FILE*"


/**
 * @brief Standard file handle structure used by the I/O library.
 */
typedef struct luaL_Stream {
  FILE *f;  /**< stream (NULL for incompletely created streams) */
  lua_CFunction closef;  /**< to close stream (NULL for closed streams) */
} luaL_Stream;

/* }=========================================== */

/*
** {=======================================================
** "Abstraction Layer" for basic report of messages and errors
** ========================================================
*/

/* print a string */
#if !defined(lua_writestring)
#define lua_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#endif

/* print a newline and flush the output */
#if !defined(lua_writeline)
#define lua_writeline()        (lua_writestring("\n", 1), fflush(stdout))
#endif

/* print an error message */
#if !defined(lua_writestringerror)
#define lua_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

/* }======================================================= */


/*
** {=================================================
** Compatibility with deprecated conversions
** ==================================================
*/
#if defined(LUA_COMPAT_APIINTCASTS)

#define luaL_checkunsigned(L,a)	((lua_Unsigned)luaL_checkinteger(L,a))
#define luaL_optunsigned(L,a,d)	\
	((lua_Unsigned)luaL_optinteger(L,a,(lua_Integer)(d)))

#define luaL_checkint(L,n)	((int)luaL_checkinteger(L, (n)))
#define luaL_optint(L,n,d)	((int)luaL_optinteger(L, (n), (d)))

#define luaL_checklong(L,n)	((long)luaL_checkinteger(L, (n)))
#define luaL_optlong(L,n,d)	((long)luaL_optinteger(L, (n), (d)))

#endif
/* }================================================= */



#endif
