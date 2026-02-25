#define ltcc_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lundump.h"
#include "ltcc.h"
#include "lopnames.h"

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
static void emit_loadk(luaL_Buffer *B, Proto *p, int k_index) {
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
            add_fmt(B, "    lua_pushlstring(L, ");
            emit_quoted_string(B, getstr(ts), tsslen(ts));
            add_fmt(B, ", %llu);\n", (unsigned long long)tsslen(ts));
            break;
        }
        default:
            add_fmt(B, "    lua_pushnil(L); /* UNKNOWN CONSTANT TYPE */\n");
            break;
    }
}

static void emit_instruction(luaL_Buffer *B, Proto *p, int pc, Instruction i, ProtoInfo *protos, int proto_count) {
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
            emit_loadk(B, p, bx);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }
        case OP_LOADI: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sbx);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }
         case OP_LOADF: {
            int sbx = GETARG_sBx(i);
            add_fmt(B, "    lua_pushnumber(L, (lua_Number)%d);\n", sbx);
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
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
                emit_loadk(B, p, ax);
                add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
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
            add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d));\n", b + 1); // table
            emit_loadk(B, p, c); // key
            add_fmt(B, "    lua_gettable(L, -2);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1); // result to R[A]
            add_fmt(B, "    lua_pop(L, 1);\n"); // pop table
            break;
        }
        case OP_SETTABUP: { // UpValue[A][K[B]] := RK(C)
            // A is upval index
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, lua_upvalueindex(%d));\n", a + 1); // table
            emit_loadk(B, p, b); // key
            // RK(C)
            if (TESTARG_k(i)) {
                emit_loadk(B, p, c);
            } else {
                add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
            }
            add_fmt(B, "    lua_settable(L, -3);\n");
            add_fmt(B, "    lua_pop(L, 1);\n"); // pop table
            break;
        }

        // Arithmetic
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_IDIV:
        case OP_MOD: case OP_POW: case OP_BAND: case OP_BOR: case OP_BXOR:
        case OP_SHL: case OP_SHR: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
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
            break;
        }

        case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_MODK:
        case OP_POWK: case OP_DIVK: case OP_IDIVK:
        case OP_BANDK: case OP_BORK: case OP_BXORK: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            emit_loadk(B, p, c);
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
            break;
        }

        case OP_SELF: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_pushvalue(L, -1);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 2);
            if (TESTARG_k(i)) emit_loadk(B, p, c);
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1);
            add_fmt(B, "    lua_gettable(L, -2);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_ADDI: { // R[A] := R[B] + sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
             add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
             add_fmt(B, "    lua_arith(L, LUA_OPADD);\n");
             add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             break;
        }

        case OP_SHLI: { // R[A] := sC << R[B]
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
             add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
             add_fmt(B, "    lua_arith(L, LUA_OPSHL);\n");
             add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             break;
        }

        case OP_SHRI: { // R[A] := R[B] >> sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
             add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
             add_fmt(B, "    lua_arith(L, LUA_OPSHR);\n");
             add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             break;
        }

        case OP_UNM: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_arith(L, LUA_OPUNM);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_BNOT: {
            int b = GETARG_B(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    lua_arith(L, LUA_OPBNOT);\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            break;
        }

        case OP_CALL: { // R[A], ... := R[A](R[A+1], ... ,R[A+B-1])
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = (b == 0) ? -1 : (b - 1); // b=0 means top-A
            int nresults = (c == 0) ? -1 : (c - 1);

            if (b != 0) {
                if (c == 0) add_fmt(B, "    int s = lua_gettop(L);\n");
                add_fmt(B, "    lua_pushvalue(L, %d); /* func */\n", a + 1);
                for (int n = 0; n < nargs; n++) {
                     add_fmt(B, "    lua_pushvalue(L, %d); /* arg %d */\n", a + 2 + n, n);
                }
                add_fmt(B, "    lua_call(L, %d, %d);\n", nargs, nresults);
                if (c != 0) {
                     for (int n = nresults - 1; n >= 0; n--) {
                         add_fmt(B, "    lua_replace(L, %d);\n", a + 1 + n);
                     }
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
                 /* Function is at R[A] (a+1), arguments are from R[A+1] (a+2) to top */
                 add_fmt(B, "    lua_call(L, lua_gettop(L) - %d, %d);\n", a + 1, nresults);
                 /* If fixed results (C!=0), restore stack frame size if needed */
                 if (c != 0) {
                      add_fmt(B, "    lua_settop(L, %d);\n", p->maxstacksize);
                 }
            }
            break;
        }

        case OP_TAILCALL: { // return R[A](...)
             // Treat as regular CALL + RETURN
            int b = GETARG_B(i);
            int nargs = (b == 0) ? -1 : (b - 1);

            if (b != 0) {
                add_fmt(B, "    lua_pushvalue(L, %d); /* func */\n", a + 1);
                for (int n = 0; n < nargs; n++) {
                     add_fmt(B, "    lua_pushvalue(L, %d); /* arg %d */\n", a + 2 + n, n);
                }
                add_fmt(B, "    lua_call(L, %d, LUA_MULTRET);\n", nargs);
                add_fmt(B, "    return lua_gettop(L) - %d;\n", p->maxstacksize + (p->is_vararg ? 1 : 0));
            } else {
                 /* Variable number of arguments from stack (B=0) */
                 add_fmt(B, "    lua_call(L, lua_gettop(L) - %d, LUA_MULTRET);\n", a + 1);
                 add_fmt(B, "    return lua_gettop(L) - %d;\n", a);
            }
            break;
        }

        case OP_RETURN: { // return R[A], ... ,R[A+B-2]
            int b = GETARG_B(i);
            int nret = (b == 0) ? -1 : (b - 1);

            if (nret > 0) {
                for (int n = 0; n < nret; n++) {
                    add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1 + n);
                }
                add_fmt(B, "    return %d;\n", nret);
            } else if (nret == 0) {
                add_fmt(B, "    return 0;\n");
            } else {
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
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPEQ) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_LT: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLT) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_LE: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLE) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_EQK: {
            int b = GETARG_B(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            emit_loadk(B, p, b);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPEQ) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_EQI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPEQ) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_LTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLT) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_LEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLE) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_GTI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLT) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_GEI: {
            int sb = GETARG_sB(i);
            int k = GETARG_k(i);
            add_fmt(B, "    lua_pushinteger(L, %d);\n", sb);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            add_fmt(B, "    if (lua_compare(L, -2, -1, LUA_OPLE) != %d) goto Label_%d;\n", k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_VARARG: {
            int a = GETARG_A(i);
            int nneeded = GETARG_C(i) - 1;
            int vtab_idx = p->maxstacksize + 1;

            if (nneeded >= 0) {
                add_fmt(B, "    for (int i=0; i<%d; i++) {\n", nneeded);
                add_fmt(B, "        lua_rawgeti(L, %d, i+1);\n", vtab_idx);
                add_fmt(B, "        lua_replace(L, %d + i);\n", a + 1);
                add_fmt(B, "    }\n");
            } else {
                add_fmt(B, "    {\n");
                add_fmt(B, "        int nvar = (int)lua_rawlen(L, %d);\n", vtab_idx);
                add_fmt(B, "        lua_settop(L, %d + nvar);\n", a);
                add_fmt(B, "        for (int i=1; i<=nvar; i++) {\n");
                add_fmt(B, "            lua_rawgeti(L, %d, i);\n", vtab_idx);
                add_fmt(B, "            lua_replace(L, %d + i - 1);\n", a + 1);
                add_fmt(B, "        }\n");
                add_fmt(B, "    }\n");
            }
            break;
        }

        case OP_GETVARG: {
            int c = GETARG_C(i);
            add_fmt(B, "    lua_rawgeti(L, %d, lua_tointeger(L, %d));\n", p->maxstacksize + 1, c + 1);
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
            if (TESTARG_k(i)) emit_loadk(B, p, c); // value K
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
                add_fmt(B, "    lua_getfield(L, -1, ");
                emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                add_fmt(B, ");\n");
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
            if (TESTARG_k(i)) emit_loadk(B, p, c); // value K
            else add_fmt(B, "    lua_pushvalue(L, %d);\n", c + 1); // value R
            TValue *k = &p->k[b];
            if (ttisstring(k)) {
                add_fmt(B, "    lua_setfield(L, -2, ");
                emit_quoted_string(B, getstr(tsvalue(k)), tsslen(tsvalue(k)));
                add_fmt(B, ");\n");
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
            if (TESTARG_k(i)) emit_loadk(B, p, c); // value K
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
            add_fmt(B, "        if (n == 0) n = lua_gettop(L) - %d;\n", a + 1);
            add_fmt(B, "        lua_pushvalue(L, %d); /* table */\n", a + 1);
            add_fmt(B, "        for (int j = 1; j <= n; j++) {\n");
            add_fmt(B, "            lua_pushvalue(L, %d + j);\n", a + 1);
        add_fmt(B, "            lua_seti(L, -2, %d + j);\n", c);
            add_fmt(B, "        }\n");
            add_fmt(B, "        lua_pop(L, 1);\n");
            if (n == 0) {
                 add_fmt(B, "        lua_settop(L, %d);\n", p->maxstacksize);
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
            add_fmt(B, "    if (!lua_isnil(L, %d)) {\n", a + 3);
            add_fmt(B, "        lua_pushvalue(L, %d);\n", a + 3);
            add_fmt(B, "        lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "        goto Label_%d;\n", pc + 1 - bx);
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
            emit_loadk(B, p, bx);
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
            emit_loadk(B, p, b);
            add_fmt(B, "    lua_setmethod(L, %d, lua_tostring(L, -1), %d);\n", a + 1, c + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_SETSTATIC: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            emit_loadk(B, p, b);
            add_fmt(B, "    lua_setstatic(L, %d, lua_tostring(L, -1), %d);\n", a + 1, c + 1);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_GETSUPER: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
            emit_loadk(B, p, c);
            add_fmt(B, "    lua_getsuper(L, -2, lua_tostring(L, -1));\n");
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
            emit_loadk(B, p, c);
            add_fmt(B, "    lua_getprop(L, -2, lua_tostring(L, -1));\n");
            add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
            add_fmt(B, "    lua_pop(L, 2);\n");
            break;
        }

        case OP_SETPROP: {
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
            emit_loadk(B, p, b);
            if (TESTARG_k(i)) emit_loadk(B, p, c);
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
            emit_loadk(B, p, c); /* name */
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
            emit_loadk(B, p, b); // Push type name K[B]
            add_fmt(B, "    if (lua_is(L, %d, lua_tostring(L, -1)) != %d) goto Label_%d;\n", a + 1, k, pc + 1 + 2);
            add_fmt(B, "    lua_pop(L, 1);\n");
            break;
        }

        case OP_NEWNAMESPACE: {
            int bx = GETARG_Bx(i);
            emit_loadk(B, p, bx);
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
            emit_loadk(B, p, bx);
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
            emit_loadk(B, p, b); // method name
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
            emit_loadk(B, p, bx - 1); // global name
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
            add_fmt(B, "    lua_pushinteger(L, tcc_in(L, %d, %d));\n", b + 1, c + 1);
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
        case OP_NOP:
            add_fmt(B, "    /* NOP/EXTRAARG */\n");
            break;

        default:
            add_fmt(B, "    /* Unimplemented opcode: %s */\n", opnames[op]);
            break;
    }
}

static void process_proto(luaL_Buffer *B, Proto *p, int id, ProtoInfo *protos, int proto_count) {
    add_fmt(B, "\n/* Proto %d */\n", id);
    add_fmt(B, "static int function_%d(lua_State *L) {\n", id);

    if (p->is_vararg) {
        add_fmt(B, "    {\n");
        add_fmt(B, "        int nargs = lua_gettop(L);\n");
        add_fmt(B, "        int nparams = %d;\n", p->numparams);
        add_fmt(B, "        lua_createtable(L, (nargs > nparams) ? nargs - nparams : 0, 0);\n");
        add_fmt(B, "        if (nargs > nparams) {\n");
        add_fmt(B, "            for (int i = nparams + 1; i <= nargs; i++) {\n");
        add_fmt(B, "                lua_pushvalue(L, i);\n");
        add_fmt(B, "                lua_rawseti(L, -2, i - nparams);\n");
        add_fmt(B, "            }\n");
        add_fmt(B, "        }\n");
        add_fmt(B, "        int table_pos = lua_gettop(L);\n");
        add_fmt(B, "        int target = %d + 1;\n", p->maxstacksize);
        add_fmt(B, "        if (table_pos >= target) {\n");
        add_fmt(B, "            lua_replace(L, target);\n");
        add_fmt(B, "            lua_settop(L, target);\n");
        add_fmt(B, "        } else {\n");
        add_fmt(B, "            lua_settop(L, target);\n");
        add_fmt(B, "            lua_pushvalue(L, table_pos);\n");
        add_fmt(B, "            lua_replace(L, target);\n");
        add_fmt(B, "            lua_pushnil(L);\n");
        add_fmt(B, "            lua_replace(L, table_pos);\n");
        add_fmt(B, "        }\n");
        add_fmt(B, "    }\n");
    } else {
        add_fmt(B, "    lua_settop(L, %d); /* Max Stack Size */\n", p->maxstacksize);
    }

    // Iterate instructions
    for (int i = 0; i < p->sizecode; i++) {
        emit_instruction(B, p, i, p->code[i], protos, proto_count);
    }

    // Fallback return if no return op
    if (p->sizecode == 0 || GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN0 && GET_OPCODE(p->code[p->sizecode-1]) != OP_RETURN1) {
        add_fmt(B, "    return 0;\n");
    }
    add_fmt(B, "}\n");
}


static int tcc_compile(lua_State *L) {
    size_t len;
    const char *code = luaL_checklstring(L, 1, &len);
    const char *modname = luaL_optstring(L, 2, "module");

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

    // Start generating C code
    luaL_Buffer B;
    luaL_buffinit(L, &B);

    add_fmt(&B, "#include \"lua.h\"\n");
    add_fmt(&B, "#include \"lauxlib.h\"\n");
    add_fmt(&B, "#include <string.h>\n\n");

    // Helper function for OP_IN
    add_fmt(&B, "static int tcc_in(lua_State *L, int val_idx, int container_idx) {\n");
    add_fmt(&B, "    int res = 0;\n");
    add_fmt(&B, "    if (lua_type(L, container_idx) == LUA_TTABLE) {\n");
    add_fmt(&B, "        lua_pushvalue(L, val_idx);\n");
    add_fmt(&B, "        lua_gettable(L, container_idx);\n");
    add_fmt(&B, "        if (!lua_isnil(L, -1)) res = 1;\n");
    add_fmt(&B, "        lua_pop(L, 1);\n");
    add_fmt(&B, "    } else if (lua_isstring(L, container_idx) && lua_isstring(L, val_idx)) {\n");
    add_fmt(&B, "        const char *s = lua_tostring(L, container_idx);\n");
    add_fmt(&B, "        const char *sub = lua_tostring(L, val_idx);\n");
    add_fmt(&B, "        if (strstr(s, sub)) res = 1;\n");
    add_fmt(&B, "    }\n");
    add_fmt(&B, "    return res;\n");
    add_fmt(&B, "}\n\n");

    // Forward declarations
    for (int i = 0; i < count; i++) {
        add_fmt(&B, "static int function_%d(lua_State *L);\n", protos[i].id);
    }

    // Implementations
    for (int i = 0; i < count; i++) {
        process_proto(&B, protos[i].p, protos[i].id, protos, count);
    }

    // Main entry point
    add_fmt(&B, "\nint luaopen_%s(lua_State *L) {\n", modname);

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
    {NULL, NULL}
};

int luaopen_tcc(lua_State *L) {
    luaL_newlib(L, tcc_lib);
    return 1;
}
