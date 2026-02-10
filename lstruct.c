/*
** $Id: lstruct.c $
** Struct support
*/

#define lstruct_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "ltable.h"
#include "lstruct.h"
#include "lvm.h"
#include "lstring.h"
#include "lapi.h"
#include "ldebug.h"

/* Field types */
#define ST_INT 0
#define ST_FLOAT 1
#define ST_BOOL 2
#define ST_STRUCT 3

/* Keys for StructDef table */
#define KEY_SIZE "__size"
#define KEY_FIELDS "__fields"
#define KEY_NAME "__name"

/* Keys for Field Info table */
#define F_OFFSET "offset"
#define F_TYPE "type"
#define F_SIZE "size"
#define F_DEFAULT "default"
#define F_DEF "def" /* For nested structs */

static int get_int_field(lua_State *L, int table_idx, const char *key) {
    int res;
    lua_pushstring(L, key);
    lua_rawget(L, table_idx);
    res = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return res;
}

static void get_field_info(lua_State *L, Table *fields, TString *key, int *offset, int *type, int *size, Table **nested_def) {
    const TValue *v = luaH_getstr(fields, key);
    if (ttistable(v)) {
        Table *info = hvalue(v);

        lua_pushstring(L, F_OFFSET);
        TValue *vo = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));
        if (!ttisnil(vo)) *offset = (int)ivalue(vo);
        lua_pop(L, 1);

        lua_pushstring(L, F_TYPE);
        TValue *vt = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));
        if (!ttisnil(vt)) *type = (int)ivalue(vt);
        lua_pop(L, 1);

        lua_pushstring(L, F_SIZE);
        TValue *vs = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));
        if (!ttisnil(vs)) *size = (int)ivalue(vs);
        lua_pop(L, 1);

        if (*type == ST_STRUCT) {
            lua_pushstring(L, F_DEF);
            TValue *vd = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));
            if (!ttisnil(vd) && ttistable(vd)) *nested_def = hvalue(vd);
            lua_pop(L, 1);
        }
    } else {
        *type = -1;
    }
}

void luaS_copystruct (lua_State *L, TValue *dest, const TValue *src) {
    Struct *s_src = structvalue(src);
    size_t size = s_src->data_size;
    Struct *s_dest = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, sizeof(Struct) + size - 1, 0);
    s_dest->def = s_src->def;
    s_dest->data_size = size;
    memcpy(s_dest->data, s_src->data, size);

    TValue *v = dest;
    v->value_.struct_ = s_dest;
    v->tt_ = ctb(LUA_VSTRUCT);
    checkliveness(L, v);
}

void luaS_structindex (lua_State *L, const TValue *t, TValue *key, StkId val) {
    if (!ttisstring(key)) {
        setnilvalue(s2v(val));
        return;
    }
    Struct *s = structvalue(t);
    Table *def = s->def;

    /* Get fields table */
    lua_pushstring(L, KEY_FIELDS);
    const TValue *vf = luaH_getstr(def, tsvalue(s2v(L->top.p - 1)));
    lua_pop(L, 1);
    if (!ttistable(vf)) {
        setnilvalue(s2v(val));
        return;
    }
    Table *fields = hvalue(vf);

    int offset, type, size;
    Table *nested_def = NULL;
    get_field_info(L, fields, tsvalue(key), &offset, &type, &size, &nested_def);

    if (type == -1) {
        setnilvalue(s2v(val));
        return;
    }

    lu_byte *p = s->data + offset;

    switch (type) {
        case ST_INT: {
            lua_Integer i;
            memcpy(&i, p, sizeof(i));
            setivalue(s2v(val), i);
            break;
        }
        case ST_FLOAT: {
            lua_Number n;
            memcpy(&n, p, sizeof(n));
            setfltvalue(s2v(val), n);
            break;
        }
        case ST_BOOL:
            if (*p) setbtvalue(s2v(val)); else setbfvalue(s2v(val));
            break;
        case ST_STRUCT: {
            /* Create new struct wrapping the data */
            Struct *new_s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, sizeof(Struct) + size - 1, 0);
            new_s->def = nested_def;
            new_s->data_size = size;
            memcpy(new_s->data, p, size);

            /* We need to set val to new_s */
            TValue *v = s2v(val);
            v->value_.struct_ = new_s;
            v->tt_ = ctb(LUA_VSTRUCT);
            checkliveness(L, v);
            break;
        }
        default:
            setnilvalue(s2v(val));
            break;
    }
}

void luaS_structnewindex (lua_State *L, const TValue *t, TValue *key, TValue *val) {
    if (!ttisstring(key)) {
        luaG_runerror(L, "struct key must be string");
        return;
    }
    Struct *s = structvalue(t);
    Table *def = s->def;

    lua_pushstring(L, KEY_FIELDS);
    const TValue *vf = luaH_getstr(def, tsvalue(s2v(L->top.p - 1)));
    lua_pop(L, 1);
    if (!ttistable(vf)) {
        luaG_runerror(L, "invalid struct definition");
        return;
    }
    Table *fields = hvalue(vf);

    int offset, type, size;
    Table *nested_def = NULL;
    get_field_info(L, fields, tsvalue(key), &offset, &type, &size, &nested_def);

    if (type == -1) {
        luaG_runerror(L, "field '%s' does not exist in struct", getstr(tsvalue(key)));
        return;
    }

    lu_byte *p = s->data + offset;

    switch (type) {
        case ST_INT: {
            lua_Integer i;
            if (!ttisinteger(val)) {
                if (!luaV_tointeger(val, &i, 0)) {
                     luaG_runerror(L, "expected integer for field '%s'", getstr(tsvalue(key)));
                }
            } else {
                i = ivalue(val);
            }
            memcpy(p, &i, sizeof(i));
            break;
        }
        case ST_FLOAT: {
            lua_Number n;
            if (!tonumber(val, &n)) {
                luaG_runerror(L, "expected number for field '%s'", getstr(tsvalue(key)));
            }
            memcpy(p, &n, sizeof(n));
            break;
        }
        case ST_BOOL:
            *p = !l_isfalse(val);
            break;
        case ST_STRUCT: {
            if (!ttisstruct(val)) {
                luaG_runerror(L, "expected struct for field '%s'", getstr(tsvalue(key)));
            }
            Struct *s_val = structvalue(val);
            if (s_val->def != nested_def) {
                 luaG_runerror(L, "struct type mismatch for field '%s'", getstr(tsvalue(key)));
            }
            memcpy(p, s_val->data, size);
            break;
        }
    }
}

int luaS_structeq (const TValue *t1, const TValue *t2) {
    Struct *s1 = structvalue(t1);
    Struct *s2 = structvalue(t2);
    if (s1->def != s2->def) return 0;
    if (s1->data_size != s2->data_size) return 0;
    return memcmp(s1->data, s2->data, s1->data_size) == 0;
}

/* __call metamethod for StructDef */
static int struct_call (lua_State *L) {
    /* Stack: Def, args... */
    if (!lua_istable(L, 1)) {
        luaL_error(L, "struct_call expected table");
        return 0;
    }
    Table *def = hvalue(s2v(L->top.p - lua_gettop(L))); /* 1st arg */

    int size = get_int_field(L, 1, KEY_SIZE);

    /* Allocate struct */
    Struct *s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, sizeof(Struct) + size - 1, 0);
    s->def = def;
    s->data_size = size;
    memset(s->data, 0, size);

    /* Initialize with defaults */
    lua_pushstring(L, KEY_FIELDS);
    lua_rawget(L, 1);
    if (lua_istable(L, -1)) {
        int fields_idx = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, fields_idx) != 0) {
            /* key, val (info) */
            if (ttistable(s2v(L->top.p - 1))) {
                Table *info = hvalue(s2v(L->top.p - 1));

                lua_pushstring(L, F_OFFSET);
                TValue *v_off = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));

                lua_pushstring(L, F_TYPE);
                TValue *v_type = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));

                lua_pushstring(L, F_DEFAULT);
                TValue *v_def = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));

                lua_pushstring(L, F_SIZE);
                TValue *v_size = (TValue*)luaH_getstr(info, tsvalue(s2v(L->top.p - 1)));

                if (!ttisnil(v_off) && !ttisnil(v_type) && !ttisnil(v_def) && !ttisnil(v_size)) {
                    /* Pop keys */
                    lua_pop(L, 4);
                    int offset = (int)ivalue(v_off);
                    int type = (int)ivalue(v_type);
                    int sz = (int)ivalue(v_size);
                    lu_byte *p = s->data + offset;

                    switch (type) {
                        case ST_INT: {
                            lua_Integer i = ivalue(v_def);
                            memcpy(p, &i, sizeof(i));
                            break;
                        }
                        case ST_FLOAT: {
                            lua_Number n = fltvalue(v_def);
                            memcpy(p, &n, sizeof(n));
                            break;
                        }
                        case ST_BOOL: *p = !l_isfalse(v_def); break;
                        case ST_STRUCT: {
                            /* Default for struct? Should be a struct instance */
                            if (ttisstruct(v_def)) {
                                Struct *def_s = structvalue(v_def);
                                memcpy(p, def_s->data, sz);
                            }
                            break;
                        }
                    }
                } else {
                    lua_pop(L, 4); /* Pop keys if nil check failed */
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1); /* pop fields table */

    /* Apply arguments (if any) */
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        /* Iterate table and set fields */
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            /* key, val */
            TValue *key = s2v(L->top.p - 2);
            TValue *val = s2v(L->top.p - 1);
            /* Set field */
            TValue temp_s;
            temp_s.value_.struct_ = s;
            temp_s.tt_ = ctb(LUA_VSTRUCT);

            luaS_structnewindex(L, &temp_s, key, val);

            lua_pop(L, 1);
        }
    }

    /* Push result */
    TValue *ret = s2v(L->top.p);
    ret->value_.struct_ = s;
    ret->tt_ = ctb(LUA_VSTRUCT);
    api_incr_top(L);

    return 1;
}

static int struct_define (lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    lua_newtable(L); /* Def table */
    int def_idx = lua_gettop(L);

    lua_pushstring(L, KEY_NAME);
    lua_pushvalue(L, 1);
    lua_rawset(L, def_idx);

    lua_newtable(L); /* Fields table */
    int fields_idx = lua_gettop(L);

    size_t current_offset = 0;

    /* Iterate array part of input table: { "name", val, "name2", val2 } */
    int n = lua_rawlen(L, 2);
    for (int i = 1; i <= n; i += 2) {
        lua_rawgeti(L, 2, i); /* Name */
        lua_rawgeti(L, 2, i+1); /* Default value */

        if (!lua_isstring(L, -2)) {
            return luaL_error(L, "field name must be string at index %d", i);
        }

        const char *fname = lua_tostring(L, -2);
        int type = -1;
        int size = 0;
        int align = 1;

        if (lua_isinteger(L, -1)) {
            type = ST_INT;
            size = sizeof(lua_Integer);
            align = sizeof(lua_Integer);
        } else if (lua_isnumber(L, -1)) {
            type = ST_FLOAT;
            size = sizeof(lua_Number);
            align = sizeof(lua_Number);
        } else if (lua_isboolean(L, -1)) {
            type = ST_BOOL;
            size = sizeof(lu_byte);
            align = sizeof(lu_byte);
        } else if (lua_isuserdata(L, -1) || ttisstruct(s2v(L->top.p - 1))) {
             /* Check if it is a Struct instance */
             TValue *v = s2v(L->top.p - 1);
             if (ttisstruct(v)) {
                 type = ST_STRUCT;
                 Struct *s = structvalue(v);
                 size = (int)s->data_size;
                 align = 8; /* Assume max align for structs for safety */
             } else {
                 return luaL_error(L, "unsupported default value type for field '%s'", fname);
             }
        } else {
            return luaL_error(L, "unsupported type for field '%s'", fname);
        }

        /* Align offset */
        while (current_offset % align != 0) current_offset++;

        /* Create field info */
        lua_newtable(L);

        lua_pushstring(L, F_OFFSET);
        lua_pushinteger(L, current_offset);
        lua_rawset(L, -3);

        lua_pushstring(L, F_TYPE);
        lua_pushinteger(L, type);
        lua_rawset(L, -3);

        lua_pushstring(L, F_SIZE);
        lua_pushinteger(L, size);
        lua_rawset(L, -3);

        lua_pushstring(L, F_DEFAULT);
        lua_pushvalue(L, -3); /* Copy default value */
        lua_rawset(L, -3);

        if (type == ST_STRUCT) {
            TValue *v = s2v(L->top.p - 2);
            Struct *s = structvalue(v);
            lua_pushstring(L, F_DEF);
            sethvalue(L, s2v(L->top.p), s->def); /* Push definition table */
            api_incr_top(L);
            lua_rawset(L, -3);
        }

        /* fields[fname] = info */
        lua_pushvalue(L, -3); /* Name */
        lua_pushvalue(L, -2); /* Info */
        lua_rawset(L, fields_idx);

        lua_pop(L, 3); /* Info, Default, Name */

        current_offset += size;
    }

    lua_pushstring(L, KEY_FIELDS);
    lua_pushvalue(L, fields_idx);
    lua_rawset(L, def_idx);

    lua_pushstring(L, KEY_SIZE);
    lua_pushinteger(L, current_offset);
    lua_rawset(L, def_idx);

    lua_pop(L, 1); /* Pop fields table */

    /* Set metatable for Def */
    lua_newtable(L);
    lua_pushstring(L, "__call");
    lua_pushcfunction(L, struct_call);
    lua_rawset(L, -3);
    lua_setmetatable(L, def_idx);

    return 1; /* Return Def table */
}

static const luaL_Reg struct_funcs[] = {
  {"define", struct_define},
  {NULL, NULL}
};

int luaopen_struct (lua_State *L) {
  luaL_newlib(L, struct_funcs);

  /* Register __struct_define globally for syntax support */
  lua_pushcfunction(L, struct_define);
  lua_setglobal(L, "__struct_define");

  return 1;
}
