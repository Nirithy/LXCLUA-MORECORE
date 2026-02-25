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
            add_fmt(B, "    lua_pushlstring(L, \"");
            const char *s = getstr(ts);
            size_t len = tsslen(ts);
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
            add_fmt(B, "\", %llu);\n", (unsigned long long)len);
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

        case OP_ADDI: { // R[A] := R[B] + sC
             int b = GETARG_B(i);
             int sc = GETARG_sC(i);
             add_fmt(B, "    lua_pushvalue(L, %d);\n", b + 1);
             add_fmt(B, "    lua_pushinteger(L, %d);\n", sc);
             add_fmt(B, "    lua_arith(L, LUA_OPADD);\n");
             add_fmt(B, "    lua_replace(L, %d);\n", a + 1);
             break;
        }

        case OP_CALL: { // R[A], ... := R[A](R[A+1], ... ,R[A+B-1])
            int b = GETARG_B(i);
            int c = GETARG_C(i);
            int nargs = (b == 0) ? -1 : (b - 1); // b=0 means top-A
            int nresults = (c == 0) ? -1 : (c - 1);

            if (b != 0) {
                add_fmt(B, "    lua_pushvalue(L, %d); /* func */\n", a + 1);
                for (int n = 0; n < nargs; n++) {
                     add_fmt(B, "    lua_pushvalue(L, %d); /* arg %d */\n", a + 2 + n, n);
                }
                add_fmt(B, "    lua_call(L, %d, %d);\n", nargs, nresults);
            } else {
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
                 add_fmt(B, "    lua_call(L, 0, %d);\n", nresults);
            }

            if (c != 0) {
                 for (int n = nresults - 1; n >= 0; n--) {
                     add_fmt(B, "    lua_replace(L, %d);\n", a + 1 + n);
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
            } else {
                 add_fmt(B, "    lua_pushvalue(L, %d);\n", a + 1);
                 add_fmt(B, "    lua_call(L, 0, LUA_MULTRET);\n");
            }
            add_fmt(B, "    return lua_gettop(L) - %d;\n", p->maxstacksize);
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

        case OP_VARARGPREP:
            add_fmt(B, "    /* VARARGPREP: adjust varargs if needed */\n");
            break;

        case OP_MMBIN:
        case OP_MMBINI:
        case OP_MMBINK:
             add_fmt(B, "    /* MMBIN: ignored as lua_arith handles it */\n");
             break;

        default:
            add_fmt(B, "    /* Unimplemented opcode: %s */\n", opnames[op]);
            break;
    }
}

static void process_proto(luaL_Buffer *B, Proto *p, int id, ProtoInfo *protos, int proto_count) {
    add_fmt(B, "\n/* Proto %d */\n", id);
    add_fmt(B, "static int function_%d(lua_State *L) {\n", id);
    add_fmt(B, "    lua_settop(L, %d); /* Max Stack Size */\n", p->maxstacksize);

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
    add_fmt(&B, "#include \"lauxlib.h\"\n\n");

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
