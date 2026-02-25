/*
** ltcc_api_list.h
** List of Lua APIs used by tcc-transpiled code.
** Format: X(name, return_type, args)
*/

/* Base Stack Manipulation */
X(lua_pushvalue, void, (lua_State *L, int idx))
X(lua_copy, void, (lua_State *L, int fromidx, int toidx))
X(lua_pushnil, void, (lua_State *L))
X(lua_pushboolean, void, (lua_State *L, int b))
X(lua_pushinteger, void, (lua_State *L, lua_Integer n))
X(lua_pushnumber, void, (lua_State *L, lua_Number n))
X(lua_pushlstring, const char *, (lua_State *L, const char *s, size_t len))
X(lua_tolstring, const char *, (lua_State *L, int idx, size_t *len))
X(lua_tonumberx, lua_Number, (lua_State *L, int idx, int *isnum))
X(lua_tointegerx, lua_Integer, (lua_State *L, int idx, int *isnum))
X(lua_toboolean, int, (lua_State *L, int idx))
X(lua_type, int, (lua_State *L, int idx))
X(lua_settop, void, (lua_State *L, int idx))
X(lua_gettop, int, (lua_State *L))
X(lua_checkstack, int, (lua_State *L, int n))

/* Table Manipulation */
X(lua_createtable, void, (lua_State *L, int narr, int nrec))
X(lua_gettable, int, (lua_State *L, int idx))
X(lua_settable, void, (lua_State *L, int idx))
X(lua_getfield, int, (lua_State *L, int idx, const char *k))
X(lua_setfield, void, (lua_State *L, int idx, const char *k))
X(lua_geti, int, (lua_State *L, int idx, lua_Integer n))
X(lua_seti, void, (lua_State *L, int idx, lua_Integer n))
X(lua_rawgeti, int, (lua_State *L, int idx, lua_Integer n))
X(lua_rawseti, void, (lua_State *L, int idx, lua_Integer n))
X(lua_rawlen, lua_Unsigned, (lua_State *L, int idx))

/* Call & Ops */
X(lua_callk, void, (lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k))
X(lua_arith, void, (lua_State *L, int op))
X(lua_compare, int, (lua_State *L, int idx1, int idx2, int op))
X(lua_pushcclosure, void, (lua_State *L, lua_CFunction fn, int n))
X(lua_toclose, void, (lua_State *L, int idx))
X(lua_closeslot, void, (lua_State *L, int idx))
X(lua_getglobal, int, (lua_State *L, const char *name))
X(lua_isinteger, int, (lua_State *L, int idx)) /* LUA_API */

/* Extended API (DifierLine extensions) */
X(lua_newclass, void, (lua_State *L, const char *name))
X(lua_inherit, void, (lua_State *L, int child_idx, int parent_idx))
X(lua_setmethod, void, (lua_State *L, int class_idx, const char *name, int func_idx))
X(lua_setstatic, void, (lua_State *L, int class_idx, const char *name, int value_idx))
X(lua_getsuper, void, (lua_State *L, int obj_idx, const char *name))
X(lua_newobject, void, (lua_State *L, int class_idx, int nargs))
X(lua_getprop, void, (lua_State *L, int obj_idx, const char *key))
X(lua_setprop, void, (lua_State *L, int obj_idx, const char *key, int value_idx))
X(lua_instanceof, int, (lua_State *L, int obj_idx, int class_idx))
X(lua_implement, void, (lua_State *L, int class_idx, int interface_idx))
X(lua_checktype, void, (lua_State *L, int idx, const char *type_name))
X(lua_spaceship, int, (lua_State *L, int idx1, int idx2))
X(lua_is, int, (lua_State *L, int idx, const char *type_name))
X(lua_newnamespace, void, (lua_State *L, const char *name))
X(lua_linknamespace, void, (lua_State *L, int idx1, int idx2))
X(lua_newsuperstruct, void, (lua_State *L, const char *name))
X(lua_setsuper, void, (lua_State *L, int idx, int key_idx, int val_idx))
X(lua_slice, void, (lua_State *L, int idx, int start_idx, int end_idx, int step_idx))
X(lua_setifaceflag, void, (lua_State *L, int idx))
X(lua_addmethod, void, (lua_State *L, int idx, const char *name, int nparams))
X(lua_getcmds, void, (lua_State *L))
X(lua_getops, void, (lua_State *L))
X(lua_errnnil, void, (lua_State *L, int idx, const char *msg))
X(lua_len, void, (lua_State *L, int idx))
X(lua_concat, void, (lua_State *L, int n))

/* Aux Lib */
X(luaL_error, int, (lua_State *L, const char *fmt, ...))

/* TCC Helpers */
X(lua_tcc_prologue, void, (lua_State *L, int nparams, int maxstack))
X(lua_tcc_gettabup, void, (lua_State *L, int upval, const char *k, int dest))
X(lua_tcc_settabup, void, (lua_State *L, int upval, const char *k, int val_idx))
X(lua_tcc_loadk_str, void, (lua_State *L, int dest, const char *s))
X(lua_tcc_loadk_int, void, (lua_State *L, int dest, lua_Integer v))
X(lua_tcc_loadk_flt, void, (lua_State *L, int dest, lua_Number v))
X(lua_tcc_in, int, (lua_State *L, int val_idx, int container_idx))
X(lua_tcc_push_args, void, (lua_State *L, int start_reg, int count))
X(lua_tcc_store_results, void, (lua_State *L, int start_reg, int count))
