#define ltcc_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lundump.h"
#include "ltcc.h"
#include "lopnames.h"
#include "lobfuscate.h"

/*
** tcc support functions (Library API)
*/

LUA_API void lua_tcc_prologue(lua_State *L, int nparams, int maxstack) {
    int nargs = lua_gettop(L);
    lua_createtable(L, (nargs > nparams) ? nargs - nparams : 0, 0);
    if (nargs > nparams) {
        for (int i = nparams + 1; i <= nargs; i++) {
            lua_pushvalue(L, i);
            lua_rawseti(L, -2, i - nparams);
        }
    }
    int table_pos = lua_gettop(L);
    int target = maxstack + 1;
    if (table_pos >= target) {
        lua_replace(L, target);
        lua_settop(L, target);
    } else {
        lua_settop(L, target);
        lua_pushvalue(L, table_pos);
        lua_replace(L, target);
        lua_pushnil(L);
        lua_replace(L, table_pos);
    }
}

LUA_API void lua_tcc_gettabup(lua_State *L, int upval, const char *k, int dest) {
    lua_getfield(L, lua_upvalueindex(upval), k);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_settabup(lua_State *L, int upval, const char *k, int val_idx) {
    lua_pushvalue(L, val_idx);
    lua_setfield(L, lua_upvalueindex(upval), k);
    lua_pop(L, 1);
}

LUA_API void lua_tcc_loadk_str(lua_State *L, int dest, const char *s) {
    lua_pushstring(L, s);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_loadk_int(lua_State *L, int dest, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_replace(L, dest);
}

LUA_API void lua_tcc_loadk_flt(lua_State *L, int dest, lua_Number v) {
    lua_pushnumber(L, v);
    lua_replace(L, dest);
}

LUA_API int lua_tcc_in(lua_State *L, int val_idx, int container_idx) {
    int res = 0;
    if (lua_type(L, container_idx) == LUA_TTABLE) {
        lua_pushvalue(L, val_idx);
        lua_gettable(L, container_idx);
        if (!lua_isnil(L, -1)) res = 1;
        lua_pop(L, 1);
    } else if (lua_isstring(L, container_idx) && lua_isstring(L, val_idx)) {
        const char *s = lua_tostring(L, container_idx);
        const char *sub = lua_tostring(L, val_idx);
        if (strstr(s, sub)) res = 1;
    }
    return res;
}

LUA_API void lua_tcc_push_args(lua_State *L, int start_reg, int count) {
    lua_checkstack(L, count);
    for (int i = 0; i < count; i++) {
        lua_pushvalue(L, start_reg + i);
    }
}

LUA_API void lua_tcc_store_results(lua_State *L, int start_reg, int count) {
    for (int i = count - 1; i >= 0; i--) {
        lua_replace(L, start_reg + i);
    }
}

/*
** Interface Obfuscation Support
*/

/* Helper X-macros to generate API lists */
#define X(name, ret, args) name,
static const void *tcc_api_funcs[] = {
#include "ltcc_api_list.h"
};
#undef X

#define X(name, ret, args) #name,
static const char *tcc_api_names[] = {
#include "ltcc_api_list.h"
};
#undef X

static const int tcc_api_count = sizeof(tcc_api_funcs) / sizeof(tcc_api_funcs[0]);

/* Simple LCG for deterministic shuffling */
static int my_rand(unsigned int *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return *seed;
}

LUA_API void *lua_tcc_get_interface(lua_State *L, int seed) {
    /* Allocate array of function pointers */
    void **iface = (void **)lua_newuserdatauv(L, tcc_api_count * sizeof(void *), 0);

    /* Create indices array */
    int *indices = (int *)malloc(tcc_api_count * sizeof(int));
    if (!indices) return NULL;
    for (int i = 0; i < tcc_api_count; i++) indices[i] = i;

    /* Shuffle indices using the seed */
    unsigned int useed = (unsigned int)seed;
    for (int i = tcc_api_count - 1; i > 0; i--) {
        int j = my_rand(&useed) % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    /* Populate interface */
    for (int i = 0; i < tcc_api_count; i++) {
        iface[indices[i]] = (void *)tcc_api_funcs[i];
    }

    free(indices);
    return iface;
}


/* Helper to format string and add to buffer */
static void add_fmt(luaL_Buffer *B, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    luaL_addstring(B, buffer);
}

/* Recursive function to collect all protos and assign IDs */
typedef struct ProtoInfo {
    Proto *p;
    int id;
} ProtoInfo;

static void collect_protos(Proto *p, int *count, ProtoInfo **list, int *capacity) {
    if (*count >= *capacity) {
        *capacity *= 2;
        *list = (ProtoInfo *)realloc(*list, *capacity * sizeof(ProtoInfo));
    }
    (*list)[*count].p = p;
    (*list)[*count].id = *count;
    (*count)++;

    for (int i = 0; i < p->sizep; i++) {
        collect_protos(p->p[i], count, list, capacity);
    }
}

static int get_proto_id(Proto *p, ProtoInfo *list, int count) {
    for (int i = 0; i < count; i++) {
        if (list[i].p == p) return list[i].id;
    }
    return -1;
}

/* Helper to emit encrypted string push */
static void emit_encrypted_string_push(luaL_Buffer *B, const char *s, size_t len, int seed) {
    if (len == 0) {
        add_fmt(B, "    lua_pushlstring(L, \"\", 0);\n");
        return;
    }
    // Derive a timestamp/key deterministically
    unsigned int timestamp = (unsigned int)seed ^ (unsigned int)len ^ 0x5A5A5A5A;
    for(size_t i=0; i<len; i++) timestamp = (timestamp * 1664525) + (unsigned char)s[i] + 1013904223;

    // Encrypt
    unsigned char *cipher = (unsigned char *)malloc(len);
    for (size_t i = 0; i < len; i++) {
        cipher[i] = s[i] ^ ((timestamp + i) & 0xFF);
    }

    // Emit C code
    add_fmt(B, "    {\n");
    add_fmt(B, "        static const unsigned char cipher[] = {");
    for (size_t i = 0; i < len; i++) {
        add_fmt(B, "0x%02x,", cipher[i]);
    }
    add_fmt(B, "};\n");
    add_fmt(B, "        lua_tcc_decrypt_string(L, cipher, %llu, %u);\n", (unsigned long long)len, timestamp);
    add_fmt(B, "    }\n");

    free(cipher);
}

/* Helper to escape and emit a string literal */
static void emit_quoted_string(luaL_Buffer *B, const char *s, size_t len) {
    add_fmt(B, "\"");
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
            add_fmt(B, "\\%c", c == '\n' ? 'n' : (c == '\r' ? 'r' : (c == '\t' ? 't' : c)));
        } else if (c < 32 || c > 126) {
            add_fmt(B, "\\x%02x", c);
        } else {
            luaL_addchar(B, c);
        }
    }
    add_fmt(B, "\"");
}

/* Emit code to push a constant */
static void emit_loadk(luaL_Buffer *B, Proto *p, int k_index, int str_encrypt, int seed) {
    TValue *k = &p->k[k_index];
    switch (ttype(k)) {
        case LUA_TNIL:
            add_fmt(B, "    lua_pushnil(L);\n");
            break;
        case LUA_TBOOLEAN:
            add_fmt(B, "    lua_pushboolean(L, %d);\n", !l_isfalse(k));
            break;
        case LUA_TNUMBER:
            if (ttisinteger(k)) {
                add_fmt(B, "    lua_pushinteger(L, %lld);\n", (long long)ivalue(k));
            } else {
                add_fmt(B, "    lua_pushnumber(L, %f);\n", fltvalue(k));
            }
            break;
        case LUA_TSTRING: {
            TString *ts = tsvalue(k);
            if (str_encrypt) {
                emit_encrypted_string_push(B, getstr(ts), tsslen(ts), seed);
            } else {
                add_fmt(B, "    lua_pushlstring(L, ");
                emit_quoted_string(B, getstr(ts), tsslen(ts));
                add_fmt(B, ", %llu);\n", (unsigned long long)tsslen(ts));
            }
            break;
        }
        default:
            add_fmt(B, "    lua_pushnil(L); /* UNKNOWN CONSTANT TYPE */\n");
            break;
    }
}

static void emit_instruction(luaL_Buffer *B, Proto *p, int pc, Instruction i, ProtoInfo *protos, int proto_count, int use_pure_c, int str_encrypt, int seed) {
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);

    add_fmt(B, "    Label_%d: /* %s */\n", pc + 1, opnames[op]);

    switch (op) {
        case OP_MOVE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }
        case OP_LOADK: {
            int bx = GETARG_Bx(i);
            TValue *k = &p->k[bx];
            if (ttisstring(k)) {
                 if (str_encrypt) {
                     emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                     add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                 } else {
                     add_fmt(B, "    lua_tcc_loadk_str(L, %d, ", a + 1);
                     emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                     add_fmt(B, ");\n");
                 }
            } else if (ttisinteger(k)) {
                 add_fmt(B, "    lua_tcc_loadk_int(L, %d, %lld);\n", a + 1, (long long)ivalue(k));
            } else if (ttisnumber(k)) {
                 add_fmt(B, "    lua_tcc_loadk_flt(L, %d, %f);\n", a + 1, fltvalue(k));
            } else {
                 emit_loadk(B, p, bx, str_encrypt, seed);
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }
        case OP_LOADI: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_tcc_loadk_int(L, %d, %d);\n", a + 1, sbx);
            break;
        }
         case OP_LOADF: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_tcc_loadk_flt(L, %d, (lua_Number)%d);\n", a + 1, sbx);
            break;
        }
        case OP_LOADNIL: {
            int b = GETARG_B(i);
            add_fmt(B, "    for (int i = 0; i <= %d; i++) {\n", b);
            add_fmt(B, "        lua_pushnil(L);\n");
            add_fmt(B, "        lua_replace(L, %d + i);\n", a + 1);
            add_fmt(B, "    }\n");
            break;
        }
        case OP_LOADFALSE:
            add_fmt(B, "    lua_pushboolean(L, 0);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        case OP_LFALSESKIP:
            add_fmt(B, "    if (!lua_toboolean(L, %d)) {\n", a + 1);
            add_fmt(B, "        goto Label_%d;\n", pc + 1 + 2);
            add_fmt(B, "    } else {\n");
            add_fmt(B, "        lua_pushboolean(L, 0);\n");
            add_fmt(B, "        lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    }\n");
            break;
        case OP_LOADTRUE:
            add_fmt(B, "    lua_pushboolean(L, 1);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;

        case OP_GETUPVAL: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d));\n", b + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }
        case OP_LOADKX: {
            if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                int ax = GETARG_Ax(p->code[pc+1]);
                TValue *k = &p->k[ax];
                if (ttisstring(k)) {
                     if (str_encrypt) {
                         emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                         add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                     } else {
                         add_fmt(B, "    lua_tcc_loadk_str(L, %d, ", a + 1);
                         emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                         add_fmt(B, ");\n");
                     }
                } else if (ttisinteger(k)) {
                     add_fmt(B, "    lua_tcc_loadk_int(L, %d, %lld);\n", a + 1, (long long)ivalue(k));
                } else {
                     emit_loadk(B, p, ax, str_encrypt, seed);
                     add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                }
            }
            break;
        }
        case OP_SETUPVAL: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_replace(L, lua_upvalueindex(%d));\n", b + 1);
            break;
        }
        case OP_GETTABUP: { // R[A] := UpValue[B][K[C]]
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            TValue *k = &p->k[c];
            if (ttisstring(k)) {
                 if (str_encrypt) {
                     emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                     add_fmt(B, "    lua_getfield(L, lua_upvalueindex(%d), lua_tostring(L, -1));\n", b + 1);
                     add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                     add_fmt(B, "    lua_pop(L, 1);\n"); // pop decrypted key
                 } else {
                     add_fmt(B, "    lua_tcc_gettabup(L, %d, ", b + 1);
                     emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                     add_fmt(B, ", %d);\n", a + 1);
                 }
            } else {
                 add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d));\n", b + 1); // table
                 emit_loadk(B, p, c, str_encrypt, seed); // key
                 add_fmt(B, "    lua_gettable(L, -2);\n");
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1); // result to R[A]
                 add_fmt(B, "    lua_pop(L, 1);\n"); // pop table
            }
            break;
        }
        case OP_SETTABUP: { // UpValue[A][K[B]] := RK(C)
            // A is upval index
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            TValue *k = &p->k[b];
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                    // RK(C)
                    if (TESTARG_k(i)) {
                        emit_loadk(B, p, c, str_encrypt, seed);
                    } else {
                        add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
                    }
                    add_fmt(B, "    lua_setfield(L, lua_upvalueindex(%d), lua_tostring(L, -2));\n", a + 1);
                    add_fmt(B, "    lua_pop(L, 1);\n"); // pop decrypted key
                } else {
                    // RK(C)
                    if (TESTARG_k(i)) {
                        emit_loadk(B, p, c, str_encrypt, seed);
                    } else {
                        add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
                    }
                    add_fmt(B, "    lua_tcc_settabup(L, %d, ", a + 1);
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ", -1);\n");
                }
            } else {
                add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d));\n", a + 1); // table
                emit_loadk(B, p, b, str_encrypt, seed); // key
                // RK(C)
                if (TESTARG_k(i)) {
                    emit_loadk(B, p, c, str_encrypt, seed);
                } else {
                    add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
                }
                add_fmt(B, "    lua_settable(L, -3);\n");
                add_fmt(B, "    lua_pop(L, 1);\n"); // pop table
            }
            break;
        }

        // Arithmetic
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV:
        case OP_MOD: case OP_POW: case OP_BAND: case OP_BOR: case OP_BXOR:
        case OP_SHL: case OP_SHR: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            if (use_pure_c) {
                const char *op_str = NULL;
                int is_int = 0;
                int is_pow = 0;
                if (op == OP_ADD) op_str = "+";
                else if (op == OP_SUB) op_str = "-";
                else if (op == OP_MUL) op_str = "*";
                else if (op == OP_DIV) op_str = "/";
                else if (op == OP_IDIV) { op_str = "/"; is_int = 1; }
                else if (op == OP_MOD) { op_str = "%"; is_int = 1; }
                else if (op == OP_POW) is_pow = 1;
                else if (op == OP_BAND) { op_str = "&"; is_int = 1; }
                else if (op == OP_BOR) { op_str = "|"; is_int = 1; }
                else if (op == OP_BXOR) { op_str = "^"; is_int = 1; }
                else if (op == OP_SHL) { op_str = "<<"; is_int = 1; }
                else if (op == OP_SHR) { op_str = ">>"; is_int = 1; }

                if (is_pow) {
                    add_fmt(B, "    lua_pushnumber(L, pow(lua_tonumber(L, %d), lua_tonumber(L, %d)));\n", b + 1, c + 1);
                } else if (is_int) {
                    add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %d) %s (lua_Integer)lua_tointeger(L, %d));\n", b + 1, op_str, c + 1);
                } else {
                    add_fmt(B, "    lua_pushnumber(L, (lua_Number)lua_tonumber(L, %d) %s (lua_Number)lua_tonumber(L, %d));\n", b + 1, op_str, c + 1);
                }
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
                int op_enum = -1;
                if (op == OP_ADD) op_enum = LUA_OPADD;
                else if (op == OP_SUB) op_enum = LUA_OPSUB;
                else if (op == OP_MUL) op_enum = LUA_OPMUL;
                else if (op == OP_DIV) op_enum = LUA_OPDIV;
                else if (op == OP_IDIV) op_enum = LUA_OPIDIV;
                else if (op == OP_MOD) op_enum = LUA_OPMOD;
                else if (op == OP_POW) op_enum = LUA_OPPOW;
                else if (op == OP_BAND) op_enum = LUA_OPBAND;
                else if (op == OP_BOR) op_enum = LUA_OPBOR;
                else if (op == OP_BXOR) op_enum = LUA_OPBXOR;
                else if (op == OP_SHL) op_enum = LUA_OPSHL;
                else if (op == OP_SHR) op_enum = LUA_OPSHR;

                add_fmt(B, "    lua_arith(L, %d);\n", op_enum);
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }

        case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
        case OP_POWK: case OP_DIVK: case OP_IDIVK:
        case OP_BANDK: case OP_BORK: case OP_BXORK: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            if (use_pure_c) {
                const char *op_str = NULL;
                int is_int = 0;
                int is_pow = 0;
                if (op == OP_ADDK) op_str = "+";
                else if (op == OP_SUBK) op_str = "-";
                else if (op == OP_MULK) op_str = "*";
                else if (op == OP_DIVK) op_str = "/";
                else if (op == OP_IDIVK) { op_str = "/"; is_int = 1; }
                else if (op == OP_MODK) { op_str = "%"; is_int = 1; }
                else if (op == OP_POWK) is_pow = 1;
                else if (op == OP_BANDK) { op_str = "&"; is_int = 1; }
                else if (op == OP_BORK) { op_str = "|"; is_int = 1; }
                else if (op == OP_BXORK) { op_str = "^"; is_int = 1; }

                TValue *k = &p->k[c];
                char k_str[64];
                if (ttisinteger(k)) snprintf(k_str, sizeof(k_str), "%lld", (long long)ivalue(k));
                else if (ttisnumber(k)) snprintf(k_str, sizeof(k_str), "%f", fltvalue(k));
                else snprintf(k_str, sizeof(k_str), "0");

                if (is_pow) {
                     add_fmt(B, "    lua_pushnumber(L, pow(lua_tonumber(L, %d), %s));\n", b + 1, k_str);
                } else if (is_int) {
                     add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %d) %s (lua_Integer)%s);\n", b + 1, op_str, k_str);
                } else {
                     add_fmt(B, "    lua_pushnumber(L, (lua_Number)lua_tonumber(L, %d) %s (lua_Number)%s);\n", b + 1, op_str, k_str);
                }
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                emit_loadk(B, p, c, str_encrypt, seed);
                int op_enum = -1;
                if (op == OP_ADDK) op_enum = LUA_OPADD;
                else if (op == OP_SUBK) op_enum = LUA_OPSUB;
                else if (op == OP_MULK) op_enum = LUA_OPMUL;
                else if (op == OP_MODK) op_enum = LUA_OPMOD;
                else if (op == OP_POWK) op_enum = LUA_OPPOW;
                else if (op == OP_DIVK) op_enum = LUA_OPDIV;
                else if (op == OP_IDIVK) op_enum = LUA_OPIDIV;
                else if (op == OP_BANDK) op_enum = LUA_OPBAND;
                else if (op == OP_BORK) op_enum = LUA_OPBOR;
                else if (op == OP_BXORK) op_enum = LUA_OPBXOR;

                add_fmt(B, "    lua_arith(L, %d);\n", op_enum);
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }

        case OP_SELF: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_pushvalue(L, -1);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 2);
            if (TESTARG_k(i)) {
                 TValue *k = &p->k[c];
                 if (ttisstring(k)) {
                      if (str_encrypt) {
                          emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                          add_fmt(B, "    lua_gettable(L, -2);\n");
                      } else {
                          add_fmt(B, "    lua_getfield(L, -1, ");
                          emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                          add_fmt(B, ");\n");
                      }
                      add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                      add_fmt(B, "    lua_pop(L, 1);\n");
                 } else {
                      emit_loadk(B, p, c, str_encrypt, seed);
                      add_fmt(B, "    lua_gettable(L, -2);\n");
                      add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                      add_fmt(B, "    lua_pop(L, 1);\n");
                 }
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
                add_fmt(B, "    lua_gettable(L, -2);\n");
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
                add_fmt(B, "    lua_pop(L, 1);\n");
            }
            break;
        }

        case OP_ADDI: { // R[A] := R[B] + sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             if (use_pure_c) {
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %d) + %d);\n", b + 1, sc);
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             } else {
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                 add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
                 add_fmt(B, "    lua_arith(L, LUA_OPADD);\n");
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             }
             break;
        }

        case OP_SHLI: { // R[A] := sC << R[B]
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             if (use_pure_c) {
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)%d << (lua_Integer)lua_tointeger(L, %d));\n", sc, b + 1);
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             } else {
                 add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                 add_fmt(B, "    lua_arith(L, LUA_OPSHL);\n");
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             }
             break;
        }

        case OP_SHRI: { // R[A] := R[B] >> sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             if (use_pure_c) {
                 add_fmt(B, "    lua_pushinteger(L, (lua_Integer)lua_tointeger(L, %d) >> %d);\n", b + 1, sc);
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             } else {
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                 add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
                 add_fmt(B, "    lua_arith(L, LUA_OPSHR);\n");
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             }
             break;
        }

        case OP_UNM: {
            int b = GETARG_B(i);
            if (use_pure_c) {
                add_fmt(B, "    lua_pushnumber(L, -(lua_Number)lua_tonumber(L, %d));\n", b + 1);
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                add_fmt(B, "    lua_arith(L, LUA_OPUNM);\n");
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }

        case OP_BNOT: {
            int b = GETARG_B(i);
            if (use_pure_c) {
                add_fmt(B, "    lua_pushinteger(L, ~(lua_Integer)lua_tointeger(L, %d));\n", b + 1);
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                add_fmt(B, "    lua_arith(L, LUA_OPBNOT);\n");
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }

        case OP_CALL: { // R[A], ... := R[A](R[A+1], ... ,R[A+B-1])
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = (b == 0) ? -1 : (b - 1); // b=0 means top-A
            int nresults = (c == 0) ? -1 : (c - 1);

            add_fmt(B, "    {\n");
            if (b != 0) {
                if (c == 0) add_fmt(B, "    int s = lua_gettop(L);\n");
                add_fmt(B, "    lua_tcc_push_args(L, %d, %d); /* func + args */\n", a + 1, nargs + 1);
                add_fmt(B, "    lua_call(L, %d, %d);\n", nargs, nresults);
                if (c != 0) {
                     add_fmt(B, "    lua_tcc_store_results(L, %d, %d);\n", a + 1, nresults);
                } else {
                     add_fmt(B, "    {\n");
                     add_fmt(B, "        int nres = lua_gettop(L) - s;\n");
                     add_fmt(B, "        for (int k = 0; k < nres; k++) {\n");
                     add_fmt(B, "            lua_pushvalue(L, s + 1 + k);\n");
                     add_fmt(B, "            lua_replace(L, %d + k);\n", a + 1);
                     add_fmt(B, "        }\n");
                     add_fmt(B, "        lua_settop(L, %d + nres);\n", a);
                     add_fmt(B, "    }\n");
                }
            } else {
                 /* Variable number of arguments from stack (B=0) */
                 if (p->is_vararg) {
                     add_fmt(B, "    if (vtab_idx == lua_gettop(L)) {\n");
                     add_fmt(B, "        int r = luaL_ref(L, LUA_REGISTRYINDEX);\n");
                     add_fmt(B, "        lua_call(L, lua_gettop(L) - %d, %d);\n", a + 1, nresults);
                     add_fmt(B, "        lua_rawgeti(L, LUA_REGISTRYINDEX, r);\n");
                     add_fmt(B, "        luaL_unref(L, LUA_REGISTRYINDEX, r);\n");
                     add_fmt(B, "        vtab_idx = lua_gettop(L);\n");
                     add_fmt(B, "    } else {\n");
                     add_fmt(B, "        lua_call(L, lua_gettop(L) - %d, %d);\n", a + 1, nresults);
                     add_fmt(B, "    }\n");
                 } else {
                     add_fmt(B, "    lua_call(L, lua_gettop(L) - %d, %d);\n", a + 1, nresults);
                 }
                 /* If fixed results (C!=0), restore stack frame size if needed */
                 if (c != 0) {
                      if (p->is_vararg) {
                          add_fmt(B, "    lua_pushvalue(L, vtab_idx);\n");
                          add_fmt(B, "    lua_replace(L, %d);\n", p->maxstacksize + 1);
                          add_fmt(B, "    vtab_idx = %d;\n", p->maxstacksize + 1);
                          add_fmt(B, "    lua_settop(L, %d);\n", p->maxstacksize + 1);
                      } else {
                          add_fmt(B, "    lua_settop(L, %d);\n", p->maxstacksize);
                      }
                 }
            }
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TAILCALL: { // return R[A](...)
             // Treat as regular CALL + RETURN
            int b = GETARG_B(i);
            int nargs = (b == 0) ? -1 : (b - 1);

            if (b != 0) {
                add_fmt(B, "    lua_tcc_push_args(L, %d, %d); /* func + args */\n", a + 1, nargs + 1);
                add_fmt(B, "    lua_call(L, %d, LUA_MULTRET);\n", nargs);
                add_fmt(B, "    return lua_gettop(L) - %d;\n", p->maxstacksize + (p->is_vararg ? 1 : 0));
            } else {
                 /* Variable number of arguments from stack (B=0) */
                 if (p->is_vararg) {
                     add_fmt(B, "    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - 1);\n");
                 }
                 add_fmt(B, "    lua_call(L, lua_gettop(L) - %d, LUA_MULTRET);\n", a + 1);
                 add_fmt(B, "    return lua_gettop(L) - %d;\n", a);
            }
            break;
        }

        case OP_RETURN: { // return R[A], ... ,R[A+B-2]
            int b = GETARG_B(i);
            int nret = (b == 0) ? -1 : (b - 1);

            if (nret > 0) {
                add_fmt(B, "    lua_tcc_push_args(L, %d, %d);\n", a + 1, nret);
                add_fmt(B, "    return %d;\n", nret);
            } else if (nret == 0) {
                add_fmt(B, "    return 0;\n");
            } else {
                 if (p->is_vararg) {
                     add_fmt(B, "    if (vtab_idx == lua_gettop(L)) lua_settop(L, lua_gettop(L) - 1);\n");
                 }
                 add_fmt(B, "    return lua_gettop(L) - %d;\n", a);
            }
            break;
        }

        case OP_RETURN0:
            add_fmt(B, "    return 0;\n");
            break;

        case OP_RETURN1:
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    return 1;\n");
            break;

        case OP_CLOSURE: { // R[A] := closure(KPROTO[Bx])
            int bx = GETARG_Bx(i);
            Proto *child = p->p[bx];
            int child_id = get_proto_id(child, protos, proto_count);

            for (int k = 0; k < child->sizeupvalues; k++) {
                 Upvaldesc *uv = &child->upvalues[k];
                 if (uv->instack) {
                     add_fmt(B, "    lua_pushvalue(L, %d); /* upval %d (local) */\n", uv->idx + 1, k);
                 } else {
                     add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d)); /* upval %d (upval) */\n", uv->idx + 1, k);
                 }
            }

            add_fmt(B, "    lua_pushcclosure(L, function_%d, %d);\n", child_id, child->sizeupvalues);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_NEWCONCEPT: {
            int bx = GETARG_Bx(i);
            Proto *child = p->p[bx];
            int child_id = get_proto_id(child, protos, proto_count);

            for (int k = 0; k < child->sizeupvalues; k++) {
                 Upvaldesc *uv = &child->upvalues[k];
                 if (uv->instack) {
                     add_fmt(B, "    lua_pushvalue(L, %d); /* upval %d (local) */\n", uv->idx + 1, k);
                 } else {
                     add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d)); /* upval %d (upval) */\n", uv->idx + 1, k);
                 }
            }

            add_fmt(B, "    lua_pushcclosure(L, function_%d, %d); /* concept */\n", child_id, child->sizeupvalues);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_JMP: {
            int sj = GETARG_sJ(i);
            add_fmt(B, "    goto Label_%d;\n", pc + 1 + sj + 1);
            break;
        }

        case OP_EQ: { // if ((R[A] == R[B]) ~= k) then pc++
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPEQ);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LT: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLT);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LE: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLE);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_EQK: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            emit_loadk(B, p, b, str_encrypt, seed);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPEQ);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_EQI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPEQ);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLT);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_LEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLE);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_GTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLT);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_GEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "        int res = lua_compare(L, -2, -1, LUA_OPLE);\n");
            add_fmt(B, "        lua_pop(L, 2);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_VARARG: {
            int a = GETARG_A(i);
            int nneeded = GETARG_C(i) - 1;

            if (nneeded >= 0) {
                add_fmt(B, "    if (%d + %d >= vtab_idx) {\n", a + 1, nneeded);
                add_fmt(B, "        lua_settop(L, %d + %d);\n", a + 1, nneeded);
                add_fmt(B, "        lua_pushvalue(L, vtab_idx);\n");
                add_fmt(B, "        lua_replace(L, %d + %d);\n", a + 1, nneeded);
                add_fmt(B, "        vtab_idx = %d + %d;\n", a + 1, nneeded);
                add_fmt(B, "    }\n");
                add_fmt(B, "    for (int i=0; i<%d; i++) {\n", nneeded);
                add_fmt(B, "        lua_rawgeti(L, vtab_idx, i+1);\n");
                add_fmt(B, "        lua_replace(L, %d + i);\n", a + 1);
                add_fmt(B, "    }\n");
            } else {
                add_fmt(B, "    {\n");
                add_fmt(B, "        int nvar = (int)lua_rawlen(L, vtab_idx);\n");
                add_fmt(B, "        lua_settop(L, %d + nvar);\n", a + 1);
                add_fmt(B, "        lua_pushvalue(L, vtab_idx);\n");
                add_fmt(B, "        lua_replace(L, %d + nvar);\n", a + 1);
                add_fmt(B, "        vtab_idx = %d + nvar;\n", a + 1);
                add_fmt(B, "        for (int i=1; i<=nvar; i++) {\n");
                add_fmt(B, "            lua_rawgeti(L, vtab_idx, i);\n");
                add_fmt(B, "            lua_replace(L, %d + i - 1);\n", a + 1);
                add_fmt(B, "        }\n");
                add_fmt(B, "    }\n");
            }
            break;
        }

        case OP_GETVARG: {
            int c = GETARG_C(i);
            add_fmt(B, "    lua_rawgeti(L, vtab_idx, lua_tointeger(L, %d));\n", c + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_VARARGPREP:
            add_fmt(B, "    /* VARARGPREP: adjust varargs if needed */\n");
            break;

        case OP_MMBIN:
        case OP_MMBINI:
        case OP_MMBINK:
             add_fmt(B, "    /* MMBIN: ignored as lua_arith handles it */\n");
             break;

        case OP_NEWTABLE: {
            unsigned int b = GETARG_vB(i);
            unsigned int c = GETARG_vC(i);
            if (TESTARG_k(i)) {
                if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                    int ax = GETARG_Ax(p->code[pc+1]);
                    c += ax * (MAXARG_C + 1);
                }
            }
            int nhash = (b > 0) ? (1 << (b - 1)) : 0;
            add_fmt(B, "    lua_createtable(L, %d, %d);\n", c, nhash);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_GETTABLE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
            add_fmt(B, "    lua_gettable(L, -2);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETTABLE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1); // table
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1); // key
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed); // value K
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1); // value R
            add_fmt(B, "    lua_settable(L, -3);\n");
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_GETFIELD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            TValue *k = &p->k[c];
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                    add_fmt(B, "    lua_gettable(L, -2);\n");
                } else {
                    add_fmt(B, "    lua_getfield(L, -1, ");
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ");\n");
                }
            } else {
                add_fmt(B, "    lua_pushnil(L);\n"); // Should not happen for GETFIELD
            }
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETFIELD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1); // table
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed); // value K
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1); // value R
            TValue *k = &p->k[b];
            if (ttisstring(k)) {
                if (str_encrypt) {
                    emit_encrypted_string_push(B, getstr(tsvalue(k)), tsslen(tsvalue(k)), seed);
                    add_fmt(B, "    lua_insert(L, -2);\n");
                    add_fmt(B, "    lua_settable(L, -3);\n");
                } else {
                    add_fmt(B, "    lua_setfield(L, -2, ");
                    emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                    add_fmt(B, ");\n");
                }
            } else {
                add_fmt(B, "    lua_pop(L, 1);\n"); // pop value
            }
            add_fmt(B, "    lua_pop(L, 1);\n"); // pop table
            break;
        }

        case OP_GETI: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_geti(L, -1, %d);\n", c);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETI: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1); // table
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed); // value K
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1); // value R
            add_fmt(B, "    lua_seti(L, -2, %d);\n", b);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETLIST: {
            int n = GETARG_vB(i);
            unsigned int c = GETARG_vC(i);
            if (TESTARG_k(i)) {
                if (pc + 1 < p->sizecode && GET_OPCODE(p->code[pc+1]) == OP_EXTRAARG) {
                    int ax = GETARG_Ax(p->code[pc+1]);
                    c += ax * (MAXARG_C + 1);
                }
            }

            add_fmt(B, "    {\n");
            add_fmt(B, "        int n = %d;\n", n);
            add_fmt(B, "        if (n == 0) {\n");
            if (p->is_vararg) {
                add_fmt(B, "            if (vtab_idx == lua_gettop(L)) {\n");
                add_fmt(B, "                n = lua_gettop(L) - %d - 1;\n", a + 1);
                add_fmt(B, "            } else {\n");
                add_fmt(B, "                n = lua_gettop(L) - %d;\n", a + 1);
                add_fmt(B, "            }\n");
            } else {
                add_fmt(B, "            n = lua_gettop(L) - %d;\n", a + 1);
            }
            add_fmt(B, "        }\n");
            add_fmt(B, "        lua_pushvalue(L, %d); /* table */\n", a + 1);
            add_fmt(B, "        for (int j = 1; j <= n; j++) {\n");
            add_fmt(B, "            lua_pushvalue(L, %d + j);\n", a + 1);
        add_fmt(B, "            lua_seti(L, -2, %d + j);\n", c);
            add_fmt(B, "        }\n");
            add_fmt(B, "        lua_pop(L, 1);\n");
            if (n == 0) {
                 if (p->is_vararg) {
                      add_fmt(B, "    lua_pushvalue(L, vtab_idx);\n");
                      add_fmt(B, "    lua_replace(L, %d);\n", p->maxstacksize + 1);
                      add_fmt(B, "    vtab_idx = %d;\n", p->maxstacksize + 1);
                      add_fmt(B, "    lua_settop(L, %d);\n", p->maxstacksize + 1);
                 } else {
                      add_fmt(B, "    lua_settop(L, %d);\n", p->maxstacksize);
                 }
            }
            add_fmt(B, "    }\n");
            break;
        }

        case OP_FORPREP: {
            int bx = GETARG_Bx(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        if (lua_isinteger(L, %d) && lua_isinteger(L, %d)) {\n", a + 1, a + 3);
            add_fmt(B, "            lua_Integer step = lua_tointeger(L, %d);\n", a + 3);
            add_fmt(B, "            lua_Integer init = lua_tointeger(L, %d);\n", a + 1);
            add_fmt(B, "            lua_pushinteger(L, init - step);\n");
            add_fmt(B, "            lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "        } else {\n");
            add_fmt(B, "            lua_Number step = lua_tonumber(L, %d);\n", a + 3);
            add_fmt(B, "            lua_Number init = lua_tonumber(L, %d);\n", a + 1);
            add_fmt(B, "            lua_pushnumber(L, init - step);\n");
            add_fmt(B, "            lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "        }\n");
            add_fmt(B, "        goto Label_%d;\n", pc + 1 + bx + 1);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_FORLOOP: {
            int bx = GETARG_Bx(i);
            add_fmt(B, "    {\n");
            add_fmt(B, "        if (lua_isinteger(L, %d)) {\n", a + 3);
            add_fmt(B, "            lua_Integer step = lua_tointeger(L, %d);\n", a + 3);
            add_fmt(B, "            lua_Integer limit = lua_tointeger(L, %d);\n", a + 2);
            add_fmt(B, "            lua_Integer idx = lua_tointeger(L, %d) + step;\n", a + 1);
            add_fmt(B, "            lua_pushinteger(L, idx);\n");
            add_fmt(B, "            lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {\n");
            add_fmt(B, "                lua_pushinteger(L, idx);\n");
            add_fmt(B, "                lua_replace(L, %d);\n", a + 4);
            add_fmt(B, "                goto Label_%d;\n", pc + 2 - bx);
            add_fmt(B, "            }\n");
            add_fmt(B, "        } else {\n");
            add_fmt(B, "            lua_Number step = lua_tonumber(L, %d);\n", a + 3);
            add_fmt(B, "            lua_Number limit = lua_tonumber(L, %d);\n", a + 2);
            add_fmt(B, "            lua_Number idx = lua_tonumber(L, %d) + step;\n", a + 1);
            add_fmt(B, "            lua_pushnumber(L, idx);\n");
            add_fmt(B, "            lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "            if ((step > 0) ? (idx <= limit) : (idx >= limit)) {\n");
            add_fmt(B, "                lua_pushnumber(L, idx);\n");
            add_fmt(B, "                lua_replace(L, %d);\n", a + 4);
            add_fmt(B, "                goto Label_%d;\n", pc + 2 - bx);
            add_fmt(B, "            }\n");
            add_fmt(B, "        }\n");
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TFORPREP: {
            int bx = GETARG_Bx(i);
            add_fmt(B, "    lua_toclose(L, %d);\n", a + 3 + 1);
            add_fmt(B, "    goto Label_%d;\n", pc + 1 + bx + 1);
            break;
        }

        case OP_TFORCALL: {
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 2);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 3);
            add_fmt(B, "    lua_call(L, 2, %d);\n", c);
            for (int k = c; k >= 1; k--) {
                add_fmt(B, "    lua_replace(L, %d);\n", a + 4 + k);
            }
            break;
        }

        case OP_TFORLOOP: {
            int bx = GETARG_Bx(i);
            add_fmt(B, "    if (!lua_isnil(L, %d)) {\n", a + 5);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 5);
            add_fmt(B, "        lua_replace(L, %d);\n", a + 3);
            add_fmt(B, "        goto Label_%d;\n", pc + 2 - bx);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_TEST: {
            int k = GETARG_k(i);
            add_fmt(B, "    if (lua_toboolean(L, %d) != %d) goto Label_%d;\n", a + 1, k, pc + 1 + 2);
            break;
        }

        case OP_TESTSET: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    if (lua_toboolean(L, %d) != %d) goto Label_%d;\n", b + 1, k, pc + 1 + 2);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_TESTNIL: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    if (lua_isnil(L, %d) == %d) goto Label_%d;\n", b + 1, k, pc + 1 + 2);
            int a = GETARG_A(i);
            if (a != MAXARG_A) {
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
                 add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            }
            break;
        }

        case OP_NEWCLASS: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed);
            add_fmt(B, "    lua_newclass(L, lua_tostring(L, -1));\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_INHERIT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_inherit(L, %d, %d);\n", a + 1, b + 1);
            break;
        }

        case OP_SETMETHOD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed);
            add_fmt(B, "    lua_setmethod(L, %d, lua_tostring(L, -1), %d);\n", a + 1, c + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETSTATIC: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed);
            add_fmt(B, "    lua_setstatic(L, %d, lua_tostring(L, -1), %d);\n", a + 1, c + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_GETSUPER: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            emit_loadk(B, p, c, str_encrypt, seed);
            add_fmt(B, "    lua_getsuper(L, -2, lua_tostring(L, -1));\n", a + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_NEWOBJ: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = c - 1;
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            for (int k = 0; k < nargs; k++) {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1 + k);
            }
            add_fmt(B, "    lua_newobject(L, -%d, %d);\n", nargs + 1, nargs);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_GETPROP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            emit_loadk(B, p, c, str_encrypt, seed);
            add_fmt(B, "    lua_getprop(L, -2, lua_tostring(L, -1));\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_SETPROP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            emit_loadk(B, p, b, str_encrypt, seed);
            if (TESTARG_k(i)) emit_loadk(B, p, c, str_encrypt, seed);
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
            add_fmt(B, "    lua_setprop(L, -3, lua_tostring(L, -2), -1);\n");
            add_fmt(B, "    lua_pop(L, 3);\n");
            break;
        }

        case OP_INSTANCEOF: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    if (lua_instanceof(L, -2, -1) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_IMPLEMENT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_implement(L, %d, %d);\n", a + 1, b + 1);
            break;
        }

        case OP_ASYNCWRAP: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_getglobal(L, \"__async_wrap\");\n");
            add_fmt(B, "    if (lua_isfunction(L, -1)) {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "        lua_call(L, 1, 1);\n");
            add_fmt(B, "        lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    } else {\n");
            add_fmt(B, "        lua_pop(L, 1);\n");
            add_fmt(B, "        luaL_error(L, \"__async_wrap not found\");\n");
            add_fmt(B, "    }\n");
            break;
        }

        case OP_GENERICWRAP: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_getglobal(L, \"__generic_wrap\");\n");
            add_fmt(B, "    if (lua_isfunction(L, -1)) {\n");
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 2);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", b + 3);
            add_fmt(B, "        lua_call(L, 3, 1);\n");
            add_fmt(B, "        lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    } else {\n");
            add_fmt(B, "        lua_pop(L, 1);\n");
            add_fmt(B, "    }\n");
            break;
        }

        case OP_CHECKTYPE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d); /* type */\n", b + 1);
            emit_loadk(B, p, c, str_encrypt, seed); /* name */
            add_fmt(B, "    lua_checktype(L, %d, lua_tostring(L, -1));\n", a + 1);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_SPACESHIP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushinteger(L, lua_spaceship(L, %d, %d));\n", b + 1, c + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_IS: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    {\n");
            emit_loadk(B, p, b, str_encrypt, seed); // Push type name K[B]
            add_fmt(B, "        int res = lua_is(L, %d, lua_tostring(L, -1));\n", a + 1);
            add_fmt(B, "        lua_pop(L, 1);\n");
            add_fmt(B, "        if (res != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    }\n");
            break;
        }

        case OP_NEWNAMESPACE: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed);
            add_fmt(B, "    lua_newnamespace(L, lua_tostring(L, -1));\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_LINKNAMESPACE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_linknamespace(L, %d, %d);\n", a + 1, b + 1);
            break;
        }

        case OP_NEWSUPER: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx, str_encrypt, seed);
            add_fmt(B, "    lua_newsuperstruct(L, lua_tostring(L, -1));\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETSUPER: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_setsuper(L, %d, %d, %d);\n", a + 1, b + 1, c + 1);
            break;
        }

        case OP_SLICE: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_slice(L, %d, %d, %d, %d);\n", b + 1, b + 2, b + 3, b + 4);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_SETIFACEFLAG: {
            add_fmt(B, "    lua_setifaceflag(L, %d);\n", a + 1);
            break;
        }

        case OP_ADDMETHOD: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b, str_encrypt, seed); // method name
            add_fmt(B, "    lua_addmethod(L, %d, lua_tostring(L, -1), %d);\n", a + 1, c);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_GETCMDS: {
            add_fmt(B, "    lua_getcmds(L);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_GETOPS: {
            add_fmt(B, "    lua_getops(L);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_ERRNNIL: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx - 1, str_encrypt, seed); // global name
            add_fmt(B, "    lua_errnnil(L, %d, lua_tostring(L, -1));\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_TBC: {
            add_fmt(B, "    lua_toclose(L, %d);\n", a + 1);
            break;
        }

        case OP_CASE: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_createtable(L, 2, 0);\n");
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_rawseti(L, -2, 1);\n");
            add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
            add_fmt(B, "    lua_rawseti(L, -2, 2);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_IN: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushinteger(L, lua_tcc_in(L, %d, %d));\n", b + 1, c + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_NOT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushboolean(L, !lua_toboolean(L, %d));\n", b + 1);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_LEN: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_len(L, -1);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_CONCAT: {
            int b = GETARG_B(i);
            for (int k = 0; k < b; k++) {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1 + k);
            }
            add_fmt(B, "    lua_concat(L, %d);\n", b);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_CLOSE: {
            add_fmt(B, "    lua_closeslot(L, %d);\n", a + 1);
            break;
        }

        case OP_EXTRAARG:
            add_fmt(B, "    /* EXTRAARG */\n");
            break;

        case OP_NOP:
            if (!use_pure_c) add_fmt(B, "    __asm__ volatile (\"nop\");\n");
            else add_fmt(B, "    /* NOP */\n");
            break;

        default:
            add_fmt(B, "    /* Unimplemented opcode: %s */\n", opnames[op]);
            break;
    }
}

static void process_proto(luaL_Buffer *B, Proto *p, int id, ProtoInfo *protos, int proto_count, int use_pure_c, int str_encrypt, int seed) {
    add_fmt(B, "\n/* Proto %d */\n", id);
    add_fmt(B, "static int function_%d(lua_State *L) {\n", id);

    if (p->is_vararg) {
        add_fmt(B, "    int vtab_idx = %d;\n", p->maxstacksize + 1);
        add_fmt(B, "    lua_tcc_prologue(L, %d, %d);\n", p->numparams, p->maxstacksize);
    } else {
        add_fmt(B, "    lua_settop(L, %d); /* Max Stack Size */\n", p->maxstacksize);
    }

    // Iterate instructions
    for (int i = 0; i < p->sizecode; i++) {
        emit_instruction(B, p, i, p->code[i], protos, proto_count, use_pure_c, str_encrypt, seed);
    }

    // Fallback return if no return op
    if (p->sizecode == 0 || GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN0 && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN1) {
        add_fmt(B, "    return 0;\n");
    }
    add_fmt(B, "}\n");
}


static int tcc_compute_flags(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushinteger(L, 0);
        return 1;
    }
    int flags = 0;

    struct { const char *name; int flag; } options[] = {
        {"flatten", OBFUSCATE_CFF},
        {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
        {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
        {"state_encode", OBFUSCATE_STATE_ENCODE},
        {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
        {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
        {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
        {"vm_protect", OBFUSCATE_VM_PROTECT},
        {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
        {"random_nop", OBFUSCATE_RANDOM_NOP},
        {"string_encryption", OBFUSCATE_STR_ENCRYPT},
        {NULL, 0}
    };

    for (int i = 0; options[i].name; i++) {
        lua_getfield(L, 1, options[i].name);
        if (lua_toboolean(L, -1)) {
            flags |= options[i].flag;
        }
        lua_pop(L, 1);
    }

    lua_pushinteger(L, flags);
    return 1;
}

static int tcc_compile(lua_State *L) {
    size_t len;
    const char *code = luaL_checklstring(L, 1, &len);
    const char *modname = "module";
    int use_pure_c = 0;
    int obfuscate = 0;
    int flatten = 0;
    int str_encrypt = 0;
    int seed = 0;
    int provided_flags = 0;

    if (lua_gettop(L) >= 2) {
        if (lua_type(L, 2) == LUA_TTABLE) {
             /* Parse table options */
             lua_getfield(L, 2, "use_pure_c");
             if (!lua_isnil(L, -1)) use_pure_c = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "obfuscate");
             if (!lua_isnil(L, -1)) obfuscate = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "flatten");
             if (!lua_isnil(L, -1)) flatten = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "string_encryption");
             if (!lua_isnil(L, -1)) str_encrypt = lua_toboolean(L, -1);
             lua_pop(L, 1);

             lua_getfield(L, 2, "flags");
             if (!lua_isnil(L, -1)) provided_flags = (int)lua_tointeger(L, -1);
             lua_pop(L, 1);

             /* Parse boolean flags from table and merge into provided_flags */
             struct { const char *name; int flag; } bool_opts[] = {
                 {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
                 {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
                 {"state_encode", OBFUSCATE_STATE_ENCODE},
                 {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
                 {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
                 {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
                 {"vm_protect", OBFUSCATE_VM_PROTECT},
                 {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
                 {"random_nop", OBFUSCATE_RANDOM_NOP},
                 {NULL, 0}
             };
             for (int i = 0; bool_opts[i].name; i++) {
                 lua_getfield(L, 2, bool_opts[i].name);
                 if (lua_toboolean(L, -1)) {
                     provided_flags |= bool_opts[i].flag;
                 }
                 lua_pop(L, 1);
             }

             lua_getfield(L, 2, "seed");
             if (!lua_isnil(L, -1)) seed = (int)lua_tointeger(L, -1);
             else seed = (int)time(NULL);
             lua_pop(L, 1);

             if (lua_gettop(L) >= 3) {
                 modname = luaL_checkstring(L, 3);
             }
        } else if (lua_type(L, 2) == LUA_TBOOLEAN) {
            use_pure_c = lua_toboolean(L, 2);
        } else {
            modname = luaL_checkstring(L, 2);
            if (lua_gettop(L) >= 3) {
                 if (lua_type(L, 3) == LUA_TTABLE) {
                     lua_getfield(L, 3, "use_pure_c");
                     if (!lua_isnil(L, -1)) use_pure_c = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "obfuscate");
                     if (!lua_isnil(L, -1)) obfuscate = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "flatten");
                     if (!lua_isnil(L, -1)) flatten = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "string_encryption");
                     if (!lua_isnil(L, -1)) str_encrypt = lua_toboolean(L, -1);
                     lua_pop(L, 1);

                     lua_getfield(L, 3, "flags");
                     if (!lua_isnil(L, -1)) provided_flags = (int)lua_tointeger(L, -1);
                     lua_pop(L, 1);

                     /* Parse boolean flags from table (arg 3) and merge into provided_flags */
                     struct { const char *name; int flag; } bool_opts[] = {
                         {"block_shuffle", OBFUSCATE_BLOCK_SHUFFLE},
                         {"bogus_blocks", OBFUSCATE_BOGUS_BLOCKS},
                         {"state_encode", OBFUSCATE_STATE_ENCODE},
                         {"nested_dispatcher", OBFUSCATE_NESTED_DISPATCHER},
                         {"opaque_predicates", OBFUSCATE_OPAQUE_PREDICATES},
                         {"func_interleave", OBFUSCATE_FUNC_INTERLEAVE},
                         {"vm_protect", OBFUSCATE_VM_PROTECT},
                         {"binary_dispatcher", OBFUSCATE_BINARY_DISPATCHER},
                         {"random_nop", OBFUSCATE_RANDOM_NOP},
                         {NULL, 0}
                     };
                     for (int i = 0; bool_opts[i].name; i++) {
                         lua_getfield(L, 3, bool_opts[i].name);
                         if (lua_toboolean(L, -1)) {
                             provided_flags |= bool_opts[i].flag;
                         }
                         lua_pop(L, 1);
                     }

                     lua_getfield(L, 3, "seed");
                     if (!lua_isnil(L, -1)) seed = (int)lua_tointeger(L, -1);
                     else seed = (int)time(NULL);
                     lua_pop(L, 1);
                 } else {
                     use_pure_c = lua_toboolean(L, 3);
                 }
            }
        }
    }

    // Compile Lua code to Bytecode
    if (luaL_loadbuffer(L, code, len, modname) != LUA_OK) {
        return lua_error(L);
    }

    // Get Proto
    const LClosure *cl = (const LClosure *)lua_topointer(L, -1);
    if (!cl || !isLfunction(s2v(L->top.p-1))) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to load closure");
        return 2;
    }
    Proto *p = cl->p;

    // Collect all protos
    int capacity = 16;
    int count = 0;
    ProtoInfo *protos = (ProtoInfo *)malloc(capacity * sizeof(ProtoInfo));
    collect_protos(p, &count, &protos, &capacity);

    // Apply obfuscation if requested
    int obfuscate_flags = provided_flags;
    if (flatten) obfuscate_flags |= OBFUSCATE_CFF;
    // Note: We do NOT enable OBFUSCATE_STR_ENCRYPT here for luaO_flatten if str_encrypt is set,
    // because we handle string encryption explicitly during C code generation in ltcc.c.
    // if (str_encrypt) obfuscate_flags |= OBFUSCATE_STR_ENCRYPT;

    if (obfuscate_flags != 0) {
        for (int i = 0; i < count; i++) {
             /* Use different seed for each proto to vary obfuscation */
             if (luaO_flatten(L, protos[i].p, obfuscate_flags, seed + protos[i].id, NULL) != 0) {
                 free(protos);
                 return luaL_error(L, "Failed to obfuscate proto %d", protos[i].id);
             }
        }
    }

    // Start generating C code
    luaL_Buffer B;
    luaL_buffinit(L, &B);

    add_fmt(&B, "#include \"lua.h\"\n");
    add_fmt(&B, "#include \"lauxlib.h\"\n");
    add_fmt(&B, "#include <string.h>\n");
    if (use_pure_c) {
        add_fmt(&B, "#include <math.h>\n");
    }
    add_fmt(&B, "\n");

    if (obfuscate) {
        add_fmt(&B, "/* Obfuscated Interface */\n");
        add_fmt(&B, "typedef struct TCC_Interface {\n");
        add_fmt(&B, "    void *f[%d];\n", tcc_api_count);
        add_fmt(&B, "} TCC_Interface;\n");
        add_fmt(&B, "static const TCC_Interface *api;\n\n");

        /* Generate shuffled indices matching lua_tcc_get_interface logic */
        int *indices = (int *)malloc(tcc_api_count * sizeof(int));
        for (int i = 0; i < tcc_api_count; i++) indices[i] = i;

        unsigned int useed = (unsigned int)seed;
        for (int i = tcc_api_count - 1; i > 0; i--) {
            int j = my_rand(&useed) % (i + 1);
            int temp = indices[i];
            indices[i] = indices[j];
            indices[j] = temp;
        }

        /* Emit macros mapping original names to shuffled locations */
        int counter = 0;
        #define X(name, ret, args) \
            add_fmt(&B, "#undef %s\n", #name); \
            add_fmt(&B, "#define %s(...) ((%s (*) %s)api->f[%d])(__VA_ARGS__)\n", #name, #ret, #args, indices[counter++]);

        #include "ltcc_api_list.h"
        #undef X

        free(indices);
        add_fmt(&B, "\n");

        add_fmt(&B, "extern void *lua_tcc_get_interface(lua_State *L, int seed);\n");
    }

    // Helpers (now provided by lapi.c/ltcc.c via LUA_API)

    // Forward declarations
    for (int i = 0; i < count; i++) {
        add_fmt(&B, "static int function_%d(lua_State *L);\n", protos[i].id);
    }

    // Implementations
    for (int i = 0; i < count; i++) {
        process_proto(&B, protos[i].p, protos[i].id, protos, count, use_pure_c, str_encrypt, seed);
    }

    // Main entry point
    add_fmt(&B, "\nint luaopen_%s(lua_State *L) {\n", modname);

    if (obfuscate) {
        add_fmt(&B, "    api = (const TCC_Interface *)lua_tcc_get_interface(L, %d);\n", seed);
        add_fmt(&B, "    luaL_ref(L, LUA_REGISTRYINDEX); /* Anchor interface to prevent GC */\n");
    }

    if (p->sizeupvalues > 0) {
         add_fmt(&B, "    lua_pushglobaltable(L);\n"); // Upvalue 1
         for (int k = 1; k < p->sizeupvalues; k++) {
             add_fmt(&B, "    lua_pushnil(L);\n");
         }
         add_fmt(&B, "    lua_pushcclosure(L, function_0, %d);\n", p->sizeupvalues);
    } else {
         add_fmt(&B, "    lua_pushcfunction(L, function_0);\n");
    }

    add_fmt(&B, "    lua_call(L, 0, 1);\n");
    add_fmt(&B, "    return 1;\n");
    add_fmt(&B, "}\n");

    luaL_pushresult(&B);
    free(protos);
    return 1;
}

static const luaL_Reg tcc_lib[] = {
    {"compile", tcc_compile},
    {"compute_flags", tcc_compute_flags},
    {NULL, NULL}
};

int luaopen_tcc(lua_State *L) {
    luaL_newlib(L, tcc_lib);
    return 1;
}
