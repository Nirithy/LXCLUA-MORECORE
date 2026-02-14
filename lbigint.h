#ifndef lbigint_h
#define lbigint_h

#include "lobject.h"

LUAI_FUNC TBigInt *luaB_new(lua_State *L, unsigned int len);
LUAI_FUNC TBigFloat *luaB_newbigfloat(lua_State *L, unsigned int len);
LUAI_FUNC void luaB_str2bigfloat(lua_State *L, const char *s, TValue *res);
LUAI_FUNC void luaB_fromint(lua_State *L, lua_Integer i, TValue *res);
LUAI_FUNC void luaB_add(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC void luaB_sub(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC void luaB_mul(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC void luaB_div(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC void luaB_mod(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC void luaB_pow(lua_State *L, TValue *v1, TValue *v2, TValue *res);
LUAI_FUNC int luaB_compare(lua_State *L, TValue *v1, TValue *v2);
LUAI_FUNC void luaB_tostring(lua_State *L, TValue *obj);
LUAI_FUNC void luaB_tostring_prec(lua_State *L, const TValue *obj, int precision, TValue *res);

#endif
