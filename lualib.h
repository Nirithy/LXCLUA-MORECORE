/*
** $Id: lualib.h $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef lualib_h
#define lualib_h

#include "lua.h"


/**
 * @brief Version suffix for environment variable names.
 */
#define LUA_VERSUFFIX          "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR


/**
 * @brief Opens the base library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_base) (lua_State *L);

/**
 * @brief Name of the coroutine library.
 */
#define LUA_COLIBNAME	"coroutine"

/**
 * @brief Opens the coroutine library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_coroutine) (lua_State *L);

/**
 * @brief Name of the table library.
 */
#define LUA_TABLIBNAME	"table"

/**
 * @brief Opens the table library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_table) (lua_State *L);

/**
 * @brief Name of the I/O library.
 */
#define LUA_IOLIBNAME	"io"

/**
 * @brief Opens the I/O library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_io) (lua_State *L);

/**
 * @brief Name of the OS library.
 */
#define LUA_OSLIBNAME	"os"

/**
 * @brief Opens the OS library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_os) (lua_State *L);

/**
 * @brief Name of the string library.
 */
#define LUA_STRLIBNAME	"string"

/**
 * @brief Opens the string library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_string) (lua_State *L);

/**
 * @brief Name of the UTF-8 library.
 */
#define LUA_UTF8LIBNAME	"utf8"

/**
 * @brief Opens the UTF-8 library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_utf8) (lua_State *L);

/**
 * @brief Name of the math library.
 */
#define LUA_MATHLIBNAME	"math"

/**
 * @brief Opens the math library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_math) (lua_State *L);

/**
 * @brief Name of the debug library.
 */
#define LUA_DBLIBNAME	"debug"

/**
 * @brief Opens the debug library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_debug) (lua_State *L);

/**
 * @brief Name of the bitwise operations library.
 */
#define LUA_BITLIBNAME	"bit"

/**
 * @brief Opens the bitwise operations library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_bit) (lua_State *L);

/**
 * @brief Name of the boolean library.
 */
#define LUA_BOOLIBNAME	"bool"

/**
 * @brief Opens the boolean library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_bool) (lua_State *L);

/**
 * @brief Name of the userdata library.
 */
#define LUA_UDATALIBNAME	"userdata"

/**
 * @brief Opens the userdata library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_userdata) (lua_State *L);

/**
 * @brief Name of the VM library.
 */
#define LUA_VMLIBNAME	"vm"

/**
 * @brief Opens the VM library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_vm) (lua_State *L);

/**
 * @brief Name of the pointer library.
 */
#define LUA_PTRLIBNAME	"ptr"

/**
 * @brief Opens the pointer library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_ptr) (lua_State *L);

/**
 * @brief Name of the struct library.
 */
#define LUA_STRUCTLIBNAME	"struct"

/**
 * @brief Opens the struct library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_struct) (lua_State *L);

/**
 * @brief Name of the filesystem library.
 */
#define LUA_FSLIBNAME	"fs"

/**
 * @brief Opens the filesystem library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_fs) (lua_State *L);

/**
 * @brief Name of the translator library.
 */
#define LUA_TRANSLATORLIBNAME	"translator"

/**
 * @brief Opens the translator library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_translator) (lua_State *L);

/**
 * @brief Name of the service manager library.
 */
#define LUA_SMGRNAME	"smgr"

/**
 * @brief Opens the service manager library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_smgr) (lua_State *L);

/**
 * @brief Name of the package library.
 */
#define LUA_LOADLIBNAME	"package"

/**
 * @brief Opens the package library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
LUAMOD_API int (luaopen_package) (lua_State *L);


/**
 * @brief Opens all standard libraries.
 *
 * @param L The Lua state.
 */
LUALIB_API void (luaL_openlibs) (lua_State *L);


#endif
