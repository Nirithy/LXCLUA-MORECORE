/*
** $Id: lstruct.c $
** Struct support
*/

#define lstruct_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>

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
#include <stdio.h>

/* Field types */
#define ST_INT 0
#define ST_FLOAT 1
#define ST_BOOL 2
#define ST_STRUCT 3
#define ST_STRING 4

/* Keys for StructDef table */
#define KEY_SIZE "__size"
#define KEY_FIELDS "__fields"
#define KEY_NAME "__name"
#define KEY_GC_OFFSETS "__gc_offsets"

/* Keys for Field Info table */
#define F_OFFSET "offset"
#define F_TYPE "type"
#define F_SIZE "size"
#define F_DEFAULT "default"
#define F_DEF "def" /* For nested structs */

/**
 * @brief Array structure for contiguous memory storage.
 */
typedef struct {
    size_t len;     /**< Number of elements in the array. */
    size_t size;    /**< Size of each element in bytes. */
    Table *def;     /**< Struct definition table. */
    lu_byte data[]; /**< Raw data buffer. */
} Array;

/**
 * @brief Retrieves an integer field from a table.
 *
 * @param L The Lua state.
 * @param table_idx Index of the table.
 * @param key The key string.
 * @return The integer value of the field.
 */
static int get_int_field(lua_State *L, int table_idx, const char *key) {
    int res;
    lua_pushstring(L, key);
    lua_rawget(L, table_idx);
    res = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return res;
}

/**
 * @brief Retrieves the GC offsets array from a struct definition.
 *
 * @param L The Lua state.
 * @param def The struct definition table.
 * @param[out] offsets Pointer to the array of offsets.
 * @param[out] count Number of offsets.
 */
static void get_gc_offsets(lua_State *L, Table *def, int **offsets, int *count) {
    *offsets = NULL;
    *count = 0;
    lua_pushstring(L, KEY_GC_OFFSETS);
    const TValue *v = luaH_getstr(def, tsvalue(s2v(L->top.p - 1)));
    lua_pop(L, 1);
    if (ttisfulluserdata(v)) {
        Udata *u = uvalue(v);
        *offsets = (int *)getudatamem(u);
        *count = (int)(u->len / sizeof(int));
    }
}

/**
 * @brief Retrieves information about a struct field.
 *
 * @param L The Lua state.
 * @param fields The fields table.
 * @param key The field name.
 * @param[out] offset Field offset.
 * @param[out] type Field type.
 * @param[out] size Field size.
 * @param[out] nested_def Nested struct definition (if applicable).
 */
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

/**
 * @brief Copies a struct object.
 *
 * @param L The Lua state.
 * @param dest Destination value.
 * @param src Source value.
 */
void luaS_copystruct (lua_State *L, TValue *dest, const TValue *src) {
    Struct *s_src = structvalue(src);
    size_t size = s_src->data_size;
    Struct *s_dest;

    if (s_src->data != s_src->inline_data.d) {
        /* Source is a View -> Create a View */
        s_dest = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data), 0);
        s_dest->parent = s_src->parent;
        s_dest->data = s_src->data;
    } else {
        /* Source is an Owner -> Create an Owner (Deep Copy) */
        s_dest = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data) + size, 0);
        s_dest->parent = NULL;
        s_dest->data = s_dest->inline_data.d;
        memcpy(s_dest->data, s_src->data, size);
    }

    s_dest->def = s_src->def;
    s_dest->data_size = size;
    s_dest->gc_offsets = s_src->gc_offsets;
    s_dest->n_gc_offsets = s_src->n_gc_offsets;

    TValue *v = dest;
    v->value_.struct_ = s_dest;
    v->tt_ = ctb(LUA_VSTRUCT);
    checkliveness(L, v);
}

/**
 * @brief Handles struct field access (indexing).
 *
 * @param L The Lua state.
 * @param t The struct value.
 * @param key The field key.
 * @param val The stack slot to store the result.
 */
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
            /* Create new struct View wrapping the data */
            Struct *new_s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data), 0);
            new_s->def = nested_def;
            new_s->data_size = size;
            get_gc_offsets(L, nested_def, &new_s->gc_offsets, &new_s->n_gc_offsets);
            new_s->parent = obj2gco(s);
            new_s->data = p;

            /* We need to set val to new_s */
            TValue *v = s2v(val);
            v->value_.struct_ = new_s;
            v->tt_ = ctb(LUA_VSTRUCT);
            checkliveness(L, v);
            break;
        }
        case ST_STRING: {
            TString *ts;
            memcpy(&ts, p, sizeof(TString *));
            if (ts) {
                setsvalue(L, s2v(val), ts);
            } else {
                setnilvalue(s2v(val));
            }
            break;
        }
        default:
            setnilvalue(s2v(val));
            break;
    }
}

/**
 * @brief Handles struct field assignment (newindex).
 *
 * @param L The Lua state.
 * @param t The struct value.
 * @param key The field key.
 * @param val The value to assign.
 */
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
        case ST_STRING: {
            if (!ttisstring(val)) {
                luaG_runerror(L, "expected string for field '%s'", getstr(tsvalue(key)));
            }
            TString *ts = tsvalue(val);
            memcpy(p, &ts, sizeof(TString *));
            break;
        }
    }
}

/**
 * @brief Checks if two struct objects are equal.
 *
 * @param t1 First struct.
 * @param t2 Second struct.
 * @return 1 if equal, 0 otherwise.
 */
int luaS_structeq (const TValue *t1, const TValue *t2) {
    Struct *s1 = structvalue(t1);
    Struct *s2 = structvalue(t2);
    if (s1->def != s2->def) return 0;
    if (s1->data_size != s2->data_size) return 0;
    return memcmp(s1->data, s2->data, s1->data_size) == 0;
}

/* __call metamethod for StructDef */
/**
 * @brief Creates a new instance of a struct (metamethod).
 *
 * @param L The Lua state.
 * @return 1 (the new struct).
 */
static int struct_call (lua_State *L) {
    /* Stack: Def, args... */
    if (!lua_istable(L, 1)) {
        luaL_error(L, "struct_call expected table");
        return 0;
    }
    Table *def = hvalue(s2v(L->top.p - lua_gettop(L))); /* 1st arg */

    int size = get_int_field(L, 1, KEY_SIZE);

    /* Allocate struct */
    Struct *s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data) + size, 0);
    s->def = def;
    s->data_size = size;
    get_gc_offsets(L, def, &s->gc_offsets, &s->n_gc_offsets);
    s->parent = NULL;
    s->data = s->inline_data.d;
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
                        case ST_STRING: {
                            if (ttisstring(v_def)) {
                                TString *ts = tsvalue(v_def);
                                memcpy(p, &ts, sizeof(TString *));
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

/**
 * @brief Defines a new struct type.
 *
 * @param L The Lua state.
 * @return 1 (the struct definition table).
 */
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
    int *gc_offsets_arr = NULL;
    int n_gc_offsets = 0;
    int cap_gc_offsets = 0;

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
        } else if (lua_isstring(L, -1)) {
            type = ST_STRING;
            size = sizeof(TString *);
            align = sizeof(TString *);
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

            /* Recursively add GC offsets */
            int *nested_offsets;
            int nested_count;
            get_gc_offsets(L, s->def, &nested_offsets, &nested_count);
            if (nested_count > 0) {
                 while (n_gc_offsets + nested_count > cap_gc_offsets) {
                     cap_gc_offsets = cap_gc_offsets ? cap_gc_offsets * 2 : 4 + nested_count;
                     gc_offsets_arr = realloc(gc_offsets_arr, cap_gc_offsets * sizeof(int));
                 }
                 for (int k = 0; k < nested_count; k++) {
                     gc_offsets_arr[n_gc_offsets++] = (int)(current_offset + nested_offsets[k]);
                 }
            }
        } else if (type == ST_STRING) {
             if (n_gc_offsets >= cap_gc_offsets) {
                 cap_gc_offsets = cap_gc_offsets ? cap_gc_offsets * 2 : 4;
                 gc_offsets_arr = realloc(gc_offsets_arr, cap_gc_offsets * sizeof(int));
             }
             gc_offsets_arr[n_gc_offsets++] = (int)current_offset;
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

    /* Store GC offsets */
    if (n_gc_offsets > 0) {
        int *ud_offsets = (int *)lua_newuserdatauv(L, n_gc_offsets * sizeof(int), 0);
        memcpy(ud_offsets, gc_offsets_arr, n_gc_offsets * sizeof(int));
        lua_pushstring(L, KEY_GC_OFFSETS);
        lua_pushvalue(L, -2);
        lua_rawset(L, def_idx);
        lua_pop(L, 1);
    }
    if (gc_offsets_arr) free(gc_offsets_arr);

    lua_pop(L, 1); /* Pop fields table */

    /* Set metatable for Def */
    lua_newtable(L);
    lua_pushstring(L, "__call");
    lua_pushcfunction(L, struct_call);
    lua_rawset(L, -3);
    lua_setmetatable(L, def_idx);

    return 1; /* Return Def table */
}

/* Array Implementation */

/**
 * @brief Returns the length of the array.
 *
 * @param L The Lua state.
 * @return 1 (length).
 */
static int array_len(lua_State *L) {
    Array *arr = (Array *)lua_touserdata(L, 1);
    lua_pushinteger(L, arr->len);
    return 1;
}

/**
 * @brief Indexes into the array (reads a value).
 *
 * @param L The Lua state.
 * @return 1 (the value) or error.
 */
static int array_index(lua_State *L) {
    Array *arr = (Array *)lua_touserdata(L, 1);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > (int)arr->len) {
        if (lua_type(L, 2) == LUA_TSTRING) {
             lua_getmetatable(L, 1);
             lua_pushvalue(L, 2);
             lua_rawget(L, -2);
             if (!lua_isnil(L, -1)) return 1;
        }
        return luaL_error(L, "array index out of bounds");
    }

    /* Return a VIEW of the struct at index */
    Struct *s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data), 0);
    s->def = arr->def;
    s->data_size = arr->size;
    get_gc_offsets(L, arr->def, &s->gc_offsets, &s->n_gc_offsets);
    s->parent = obj2gco((Udata *)((char *)arr - udatamemoffset(1)));
    s->data = arr->data + (idx - 1) * arr->size;

    TValue *v = s2v(L->top.p);
    v->value_.struct_ = s;
    v->tt_ = ctb(LUA_VSTRUCT);
    api_incr_top(L);

    return 1;
}

/**
 * @brief Writes a value to the array.
 *
 * @param L The Lua state.
 * @return 0 or error.
 */
static int array_newindex(lua_State *L) {
    Array *arr = (Array *)lua_touserdata(L, 1);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 1 || idx > (int)arr->len) return luaL_error(L, "array index out of bounds");

    if (!ttisstruct(s2v(L->top.p - 1))) {
        return luaL_error(L, "expected struct value");
    }
    Struct *s = structvalue(s2v(L->top.p - 1));

    if (s->def != arr->def) {
        return luaL_error(L, "struct type mismatch");
    }

    lu_byte *dst = arr->data + (idx - 1) * arr->size;
    memcpy(dst, s->data, arr->size);

    return 0;
}

/**
 * @brief Creates a new struct array.
 *
 * @param L The Lua state.
 * @param def_idx Index of the struct definition.
 * @param count Number of elements.
 * @return 1 (the array).
 */
static int create_struct_array(lua_State *L, int def_idx, int count) {
    if (count < 0) return luaL_error(L, "size must be non-negative");

    lua_pushvalue(L, def_idx); /* push def table to top for get_int_field */
    int def_top = lua_gettop(L);
    int size = get_int_field(L, def_top, KEY_SIZE);
    lua_pop(L, 1); /* pop def table */

    size_t total_size = sizeof(Array) + (size_t)count * size;
    Array *arr = (Array *)lua_newuserdatauv(L, total_size, 1);
    arr->len = count;
    arr->size = size;
    arr->def = (Table*)lua_topointer(L, def_idx);

    /* Anchor def table */
    lua_pushvalue(L, def_idx);
    lua_setiuservalue(L, -2, 1);

    memset(arr->data, 0, (size_t)count * size);

    if (luaL_newmetatable(L, "struct.array")) {
        lua_pushcfunction(L, array_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, array_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, array_len);
        lua_setfield(L, -2, "__len");
    }
    lua_setmetatable(L, -2);

    return 1;
}

/* Proxy array implementation for basic types */
static int array_typed_newindex(lua_State *L) {
    /* upvalue 1: type (string) */
    /* upvalue 2: storage table */
    const char *expected = lua_tostring(L, lua_upvalueindex(1));

    if (!lua_isnil(L, 3)) {
        int valid = 0;
        int t = lua_type(L, 3);
        if (strcmp(expected, "number") == 0) valid = (t == LUA_TNUMBER);
        else if (strcmp(expected, "string") == 0) valid = (t == LUA_TSTRING);
        else if (strcmp(expected, "boolean") == 0) valid = (t == LUA_TBOOLEAN);
        else if (strcmp(expected, "table") == 0) valid = (t == LUA_TTABLE);
        else if (strcmp(expected, "function") == 0) valid = (t == LUA_TFUNCTION);
        else if (strcmp(expected, "thread") == 0) valid = (t == LUA_TTHREAD);
        else if (strcmp(expected, "userdata") == 0) valid = (t == LUA_TUSERDATA);
        else if (strcmp(expected, "nil_type") == 0) valid = (t == LUA_TNIL);

        if (!valid) {
            return luaL_error(L, "invalid type: expected %s, got %s", expected, luaL_typename(L, 3));
        }
    }

    lua_pushvalue(L, 2); /* key */
    lua_pushvalue(L, 3); /* value */
    lua_settable(L, lua_upvalueindex(2));
    return 0;
}

static int array_typed_index(lua_State *L) {
    lua_pushvalue(L, 2);
    lua_gettable(L, lua_upvalueindex(2));
    return 1;
}

static int array_typed_len(lua_State *L) {
    lua_len(L, lua_upvalueindex(2));
    return 1;
}

static int create_proxy_array(lua_State *L, int type_idx, int size) {
    lua_createtable(L, size, 0);
    int storage_idx = lua_gettop(L);

    lua_newtable(L); /* proxy */
    int proxy_idx = lua_gettop(L);

    lua_newtable(L); /* metatable */

    lua_pushvalue(L, type_idx);
    lua_pushvalue(L, storage_idx);
    lua_pushcclosure(L, array_typed_newindex, 2);
    lua_setfield(L, -2, "__newindex");

    lua_pushvalue(L, type_idx); /* dummy */
    lua_pushvalue(L, storage_idx);
    lua_pushcclosure(L, array_typed_index, 2);
    lua_setfield(L, -2, "__index");

    lua_pushvalue(L, type_idx); /* dummy */
    lua_pushvalue(L, storage_idx);
    lua_pushcclosure(L, array_typed_len, 2);
    lua_setfield(L, -2, "__len");

    lua_setmetatable(L, proxy_idx);

    lua_remove(L, storage_idx); /* remove storage from stack, kept in closures */
    return 1;
}

/* Safe array for structs with pointers (using table storage but struct value semantics) */
static int safe_array_index(lua_State *L) {
    /* upvalue 1: storage table */
    lua_pushvalue(L, lua_upvalueindex(1));
    Table *h = hvalue(s2v(L->top.p - 1));
    lua_pop(L, 1);
    int idx = (int)luaL_checkinteger(L, 2);

    l_rwlock_rdlock(&h->lock);
    const TValue *res = luaH_getint(h, idx);

    if (!ttisnil(res) && ttisstruct(res)) {
        Struct *s = structvalue(res);

        /* Anchor 's' to stack to prevent GC collection after unlock */
        setgcovalue(L, s2v(L->top.p), obj2gco(s));
        L->top.p++;

        Table *def = s->def;
        size_t size = s->data_size;
        int *gc_offsets = s->gc_offsets;
        int n_gc_offsets = s->n_gc_offsets;
        GCObject *parent = obj2gco(s);
        lu_byte *data = s->data;
        l_rwlock_unlock(&h->lock);

        /* Create View */
        Struct *new_s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data), 0);
        new_s->def = def;
        new_s->data_size = size;
        new_s->gc_offsets = gc_offsets;
        new_s->n_gc_offsets = n_gc_offsets;
        new_s->parent = parent;
        new_s->data = data;

        /* Pop anchor */
        L->top.p--;

        TValue *v = s2v(L->top.p);
        v->value_.struct_ = new_s;
        v->tt_ = ctb(LUA_VSTRUCT);
        api_incr_top(L);
        return 1;
    }
    l_rwlock_unlock(&h->lock);
    lua_pushnil(L);
    return 1;
}

static int safe_array_newindex(lua_State *L) {
    /* upvalue 1: storage table */
    /* upvalue 2: def table */
    int idx = (int)luaL_checkinteger(L, 2);

    /* Check bounds */
    if (idx < 1 || idx > (int)lua_rawlen(L, lua_upvalueindex(1)))
        return luaL_error(L, "array index out of bounds");

    if (!ttisstruct(s2v(L->top.p - 1))) {
        return luaL_error(L, "expected struct value");
    }
    Struct *s_src = structvalue(s2v(L->top.p - 1));
    Table *def = (Table *)lua_topointer(L, lua_upvalueindex(2));

    if (s_src->def != def) {
        return luaL_error(L, "struct type mismatch");
    }

    /* Get destination struct from storage */
    lua_rawgeti(L, lua_upvalueindex(1), idx);
    Struct *s_dst = structvalue(s2v(L->top.p - 1));

    /* Copy data */
    memcpy(s_dst->data, s_src->data, s_src->data_size);
    lua_pop(L, 1);

    return 0;
}

static int safe_array_len(lua_State *L) {
    lua_len(L, lua_upvalueindex(1));
    return 1;
}

static int create_safe_struct_array(lua_State *L, int def_idx, int count) {
    if (count < 0) return luaL_error(L, "size must be non-negative");

    lua_pushvalue(L, def_idx); /* push def to top for get_int_field */
    int def_top = lua_gettop(L);
    int size = get_int_field(L, def_top, KEY_SIZE);

    int *gc_offsets;
    int n_gc_offsets;
    get_gc_offsets(L, (Table*)lua_topointer(L, def_idx), &gc_offsets, &n_gc_offsets);

    lua_pop(L, 1); /* pop def */

    lua_createtable(L, count, 0);
    int storage_idx = lua_gettop(L);

    /* Initialize with default structs */
    for (int i = 1; i <= count; i++) {
        Struct *s = (Struct *)luaC_newobjdt(L, LUA_TSTRUCT, offsetof(Struct, inline_data) + size, 0);
        s->def = (Table*)lua_topointer(L, def_idx);
        s->data_size = size;
        s->gc_offsets = gc_offsets;
        s->n_gc_offsets = n_gc_offsets;
        s->parent = NULL;
        s->data = s->inline_data.d;
        memset(s->data, 0, size);

        /* Note: Using 0-init. Ideally should use default values from def but this matches current create_struct_array behavior */

        TValue *v = s2v(L->top.p);
        v->value_.struct_ = s;
        v->tt_ = ctb(LUA_VSTRUCT);
        api_incr_top(L);

        lua_rawseti(L, storage_idx, i);
    }

    lua_newtable(L); /* proxy */
    int proxy_idx = lua_gettop(L);
    lua_newtable(L); /* metatable */

    lua_pushvalue(L, storage_idx);
    lua_pushcclosure(L, safe_array_index, 1);
    lua_setfield(L, -2, "__index");

    lua_pushvalue(L, storage_idx);
    lua_pushvalue(L, def_idx);
    lua_pushcclosure(L, safe_array_newindex, 2);
    lua_setfield(L, -2, "__newindex");

    lua_pushvalue(L, storage_idx);
    lua_pushcclosure(L, safe_array_len, 1);
    lua_setfield(L, -2, "__len");

    lua_setmetatable(L, proxy_idx);

    lua_remove(L, storage_idx);
    return 1;
}

static int array_factory_index(lua_State *L) {
    lua_getfield(L, 1, "__type");
    int type_idx = lua_gettop(L);
    int size = (int)luaL_checkinteger(L, 2);

    if (lua_istable(L, type_idx)) {
        /* Check if it is a struct definition (has __size) */
        lua_pushstring(L, KEY_SIZE);
        lua_rawget(L, type_idx);
        int is_struct = !lua_isnil(L, -1);
        lua_pop(L, 1);

        if (is_struct) {
            int *gc_offsets;
            int n_gc_offsets;
            get_gc_offsets(L, (Table*)lua_topointer(L, type_idx), &gc_offsets, &n_gc_offsets);
            if (n_gc_offsets > 0) {
                return create_safe_struct_array(L, type_idx, size);
            }
            return create_struct_array(L, type_idx, size);
        }

        /* Check standard libraries as types */
        const char *type_name = NULL;
        lua_getglobal(L, "string");
        if (lua_rawequal(L, -1, type_idx)) type_name = "string";
        lua_pop(L, 1);

        if (!type_name) {
            lua_getglobal(L, "table");
            if (lua_rawequal(L, -1, type_idx)) type_name = "table";
            lua_pop(L, 1);
        }

        if (type_name) {
            lua_pushstring(L, type_name);
            lua_replace(L, type_idx);
            return create_proxy_array(L, type_idx, size);
        }

        return luaL_error(L, "invalid type for array");
    } else {
        return create_proxy_array(L, type_idx, size);
    }
}

/**
 * @brief Factory function for creating arrays.
 * Usage: array(type)[size]
 *
 * @param L The Lua state.
 * @return 1 (the array factory table).
 */
static int array_call(lua_State *L) {
    /* array(type) */
    lua_newtable(L);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "__type");

    lua_newtable(L);
    lua_pushcfunction(L, array_factory_index);
    lua_setfield(L, -2, "__index");

    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief Creates a global array instance directly.
 * Usage: array[size] -> creates array of nil (using table).
 *
 * @param L The Lua state.
 * @return 1 (the new table).
 */
static int array_global_index(lua_State *L) {
    /* array[size] */
    int size = (int)luaL_checkinteger(L, 2);
    lua_createtable(L, size, 0);
    return 1;
}

static const luaL_Reg struct_funcs[] = {
  {"define", struct_define},
  {NULL, NULL}
};

/**
 * @brief Registers the struct library.
 *
 * @param L The Lua state.
 * @return 1 (the library table).
 */
int luaopen_struct (lua_State *L) {
  luaL_newlib(L, struct_funcs);

  /* Register __struct_define globally for syntax support */
  // lua_pushcfunction(L, struct_define);
  // lua_setglobal(L, "__struct_define");

  /* Register array global */
  lua_newtable(L); /* array table */
  lua_newtable(L); /* metatable */
  lua_pushcfunction(L, array_call);
  lua_setfield(L, -2, "__call");
  lua_pushcfunction(L, array_global_index);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  lua_setglobal(L, "array");

  return 1;
}
