#include <stdio.h>
#include <vector>

// Handle C11 _Atomic in C++
#ifdef __cplusplus
#include <atomic>
#define _Atomic(T) std::atomic<T>
#endif

// Define LUA_CORE to access internal structs
#define LUA_CORE
extern "C" {
#include "jit_backend.h"
#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"
#include "ldo.h"
#include "lfunc.h"
#include "lstring.h"
#include "ltable.h"
#include "lgc.h"
}

// Undefine to avoid pollution
#undef _Atomic

extern "C" void jit_barrier(lua_State *L, GCObject *p, TValue *v) {
    luaC_barrier(L, p, v);
}

#ifndef __EMSCRIPTEN__

#include "asmjit/core.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #define JIT_ARCH_X86
  #include "asmjit/x86.h"
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define JIT_ARCH_A64
  #include "asmjit/a64.h"
#endif

using namespace asmjit;

// Global JIT runtime
static JitRuntime* g_jitRuntime = nullptr;

extern "C" void jit_init(void) {
    if (!g_jitRuntime) {
        g_jitRuntime = new JitRuntime();
    }
}

#ifdef JIT_ARCH_X86
// Access register R[i] (StackValue is TValue + padding/delta)
static x86::Mem ptr_ivalue(x86::Gp& base, int i) {
    return x86::qword_ptr(base, i * sizeof(StackValue) + offsetof(TValue, value_));
}

// Access TValue tag field
static x86::Mem ptr_tt(x86::Gp& base, int i) {
    return x86::byte_ptr(base, i * sizeof(StackValue) + offsetof(TValue, tt_));
}
#endif

#ifdef JIT_ARCH_A64
static a64::Mem ptr_ivalue(a64::Gp& base, int i) {
    return a64::ptr(base, i * sizeof(StackValue) + offsetof(TValue, value_));
}

static a64::Mem ptr_tt(a64::Gp& base, int i) {
    return a64::ptr(base, i * sizeof(StackValue) + offsetof(TValue, tt_));
}
#endif

// 64-bit instruction decoding macros
#define SIZE_OP_64		9
#define SIZE_A_64		16
#define SIZE_Bx_64		33
#define POS_OP_64		0
#define POS_A_64		(POS_OP_64 + SIZE_OP_64)
#define POS_k_64		(POS_A_64 + SIZE_A_64)
#define POS_Bx_64		POS_k_64

#define MAXARG_Bx_64	((1ULL << SIZE_Bx_64) - 1)
#define OFFSET_sBx_64	(MAXARG_Bx_64 >> 1)

#define GETARG_Bx_64(i)	((long long)(((i) >> POS_Bx_64) & MAXARG_Bx_64))
#define GETARG_sBx_64(i) ((long long)(GETARG_Bx_64(i) - OFFSET_sBx_64))

extern "C" int jit_compile(lua_State *L, Proto *p) {
    if (p->jit_code) return 1;
    jit_init();

    CodeHolder code;
    code.init(g_jitRuntime->environment());

    // Simple logging
    printf("[JIT] Compiling function with %d instructions\n", p->sizecode);

    #ifdef JIT_ARCH_X86
    x86::Compiler cc(&code);
    FuncNode* func_node = cc.add_func(FuncSignature::build<int, lua_State*>(CallConvId::kCDecl));

    x86::Gp L_reg = cc.new_gpz("L");
    func_node->set_arg(0, L_reg);

    x86::Gp base = cc.new_gpz("base");

    // Load base = L->ci->func.p + 1
    x86::Gp ci = cc.new_gpz("ci");
    cc.mov(ci, x86::ptr(L_reg, offsetof(lua_State, ci)));

    x86::Gp func_ptr = cc.new_gpz("func");
    cc.mov(func_ptr, x86::ptr(ci, offsetof(CallInfo, func) + offsetof(StkIdRel, p)));

    cc.lea(base, x86::ptr(func_ptr, sizeof(StackValue)));

    std::vector<Label> labels(p->sizecode);
    for (int i = 0; i < p->sizecode; i++) {
        labels[i] = cc.new_label();
    }

    bool unsupported = false;

    // Helper macro for inline bailout
    // Note: Ideally we update savedpc here, but complex movs trigger asmjit assertions in this env.
    // Returning 0 causes function restart (safest fallback without exact PC restoration).
    #define EMIT_BAILOUT(cond_op, target_pc) do { \
        Label ok = cc.new_label(); \
        cond_op(ok); \
        cc.xor_(x86::eax, x86::eax); \
        cc.ret(); \
        cc.bind(ok); \
    } while(0)

    for (int pc = 0; pc < p->sizecode; pc++) {
        cc.bind(labels[pc]);

        Instruction i = p->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);
        int k_flag = GETARG_k(i);

        switch (op) {
            case OP_MOVE: {
                int b = GETARG_B(i);
                x86::Gp tmp1 = cc.new_gp64();
                x86::Gp tmp2 = cc.new_gp64();

                cc.mov(tmp1, x86::ptr(base, b * sizeof(StackValue)));
                cc.mov(tmp2, x86::ptr(base, b * sizeof(StackValue) + 8));

                cc.mov(x86::ptr(base, a * sizeof(StackValue)), tmp1);
                cc.mov(x86::ptr(base, a * sizeof(StackValue) + 8), tmp2);
                break;
            }
            case OP_LOADI: {
                long long sbx = GETARG_sBx_64(i);
                cc.mov(ptr_ivalue(base, a), sbx);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                break;
            }
            case OP_LOADK: {
                long long bx = GETARG_Bx_64(i);
                TValue* k_ptr = &p->k[bx];
                x86::Gp k_addr = cc.new_gp64();
                cc.mov(k_addr, (uint64_t)k_ptr);
                x86::Gp val = cc.new_gp64();
                cc.mov(val, x86::ptr(k_addr, offsetof(TValue, value_)));
                cc.mov(ptr_ivalue(base, a), val);
                x86::Gp tt = cc.new_gp32();
                cc.movzx(tt, x86::byte_ptr(k_addr, offsetof(TValue, tt_)));
                cc.mov(ptr_tt(base, a), tt.r8());
                break;
            }
            case OP_LOADNIL: {
                int b = GETARG_B(i);
                for (int j = 0; j <= b; j++) {
                    cc.mov(ptr_tt(base, a + j), LUA_VNIL);
                }
                break;
            }
            case OP_LOADFALSE: {
                cc.mov(ptr_tt(base, a), LUA_VFALSE);
                break;
            }
            case OP_LOADTRUE: {
                cc.mov(ptr_tt(base, a), LUA_VTRUE);
                break;
            }
            case OP_GETUPVAL: {
                int b = GETARG_B(i);
                x86::Gp closure = cc.new_gp64();
                cc.mov(closure, x86::ptr(func_ptr, offsetof(TValue, value_)));

                x86::Gp upval = cc.new_gp64();
                cc.mov(upval, x86::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                x86::Gp val_ptr = cc.new_gp64();
                cc.mov(val_ptr, x86::ptr(upval, offsetof(UpVal, v)));

                x86::Gp tmp1 = cc.new_gp64();
                x86::Gp tmp2 = cc.new_gp64();
                cc.mov(tmp1, x86::ptr(val_ptr, 0));
                cc.mov(tmp2, x86::ptr(val_ptr, 8));

                cc.mov(x86::ptr(base, a * sizeof(StackValue)), tmp1);
                cc.mov(x86::ptr(base, a * sizeof(StackValue) + 8), tmp2);
                break;
            }
            case OP_SETUPVAL: {
                int b = GETARG_B(i);
                x86::Gp closure = cc.new_gp64();
                cc.mov(closure, x86::ptr(func_ptr, offsetof(TValue, value_)));

                x86::Gp upval = cc.new_gp64();
                cc.mov(upval, x86::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                x86::Gp val_ptr = cc.new_gp64();
                cc.mov(val_ptr, x86::ptr(upval, offsetof(UpVal, v)));

                x86::Gp tmp1 = cc.new_gp64();
                x86::Gp tmp2 = cc.new_gp64();
                cc.mov(tmp1, x86::ptr(base, a * sizeof(StackValue)));
                cc.mov(tmp2, x86::ptr(base, a * sizeof(StackValue) + 8));

                cc.mov(x86::ptr(val_ptr, 0), tmp1);
                cc.mov(x86::ptr(val_ptr, 8), tmp2);

                x86::Gp val_addr = cc.new_gp64();
                cc.lea(val_addr, x86::ptr(base, a * sizeof(StackValue)));

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&jit_barrier, FuncSignature::build<void, lua_State*, GCObject*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, upval);
                invoke->set_arg(2, val_addr);
                break;
            }
            case OP_GETTABUP: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                x86::Gp closure = cc.new_gp64();
                cc.mov(closure, x86::ptr(func_ptr, offsetof(TValue, value_)));

                x86::Gp upval = cc.new_gp64();
                cc.mov(upval, x86::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                x86::Gp val_ptr = cc.new_gp64();
                cc.mov(val_ptr, x86::ptr(upval, offsetof(UpVal, v)));

                x86::Gp tt = cc.new_gp32();
                cc.movzx(tt, x86::byte_ptr(val_ptr, offsetof(TValue, tt_)));
                cc.cmp(tt, LUA_VTABLE);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, x86::ptr(val_ptr, offsetof(TValue, value_)));

                x86::Gp key_str = cc.new_gp64();
                x86::Gp k_val_addr = cc.new_gp64();
                cc.mov(k_val_addr, (uint64_t)&p->k[c]);
                cc.mov(key_str, x86::ptr(k_val_addr, offsetof(TValue, value_)));

                InvokeNode* invoke;
                x86::Gp result_ptr = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getshortstr, FuncSignature::build<const TValue*, Table*, TString*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_arg(1, key_str);
                invoke->set_ret(0, result_ptr);

                cc.cmp(x86::byte_ptr(result_ptr, offsetof(TValue, tt_)), LUA_VNIL);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp val = cc.new_gp64();
                cc.mov(val, x86::ptr(result_ptr, offsetof(TValue, value_)));
                cc.mov(ptr_ivalue(base, a), val);

                x86::Gp res_tt = cc.new_gp32();
                cc.movzx(res_tt, x86::byte_ptr(result_ptr, offsetof(TValue, tt_)));
                cc.mov(ptr_tt(base, a), res_tt.r8());
                break;
            }
            case OP_SETTABUP: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                x86::Gp closure = cc.new_gp64();
                cc.mov(closure, x86::ptr(func_ptr, offsetof(TValue, value_)));

                x86::Gp upval = cc.new_gp64();
                cc.mov(upval, x86::ptr(closure, offsetof(LClosure, upvals) + a * sizeof(UpVal*)));

                x86::Gp val_ptr = cc.new_gp64();
                cc.mov(val_ptr, x86::ptr(upval, offsetof(UpVal, v)));

                x86::Gp tt = cc.new_gp32();
                cc.movzx(tt, x86::byte_ptr(val_ptr, offsetof(TValue, tt_)));
                cc.cmp(tt, LUA_VTABLE);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, x86::ptr(val_ptr, offsetof(TValue, value_)));

                cc.cmp(x86::qword_ptr(table_ptr, offsetof(Table, metatable)), 0);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp key_val_ptr = cc.new_gp64();
                cc.mov(key_val_ptr, (uint64_t)&p->k[b]);

                x86::Gp rk_val_ptr = cc.new_gp64();
                if (k_flag) {
                     TValue* k_val = &p->k[c];
                     cc.mov(rk_val_ptr, (uint64_t)k_val);
                } else {
                     cc.lea(rk_val_ptr, x86::ptr(base, c * sizeof(StackValue)));
                }

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_set, FuncSignature::build<void, lua_State*, Table*, const TValue*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, table_ptr);
                invoke->set_arg(2, key_val_ptr);
                invoke->set_arg(3, rk_val_ptr);
                break;
            }
            case OP_UNM: {
                int b = GETARG_B(i);
                x86::Gp vb = cc.new_gp64();
                cc.mov(vb, ptr_ivalue(base, b));

                cc.cmp(ptr_tt(base, b), LUA_VNUMINT);
                EMIT_BAILOUT(cc.jne, pc);

                cc.neg(vb);
                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                break;
            }
            case OP_NOT: {
                int b = GETARG_B(i);
                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, ptr_tt(base, b));

                Label set_true = cc.new_label();
                Label set_false = cc.new_label();

                cc.cmp(tag, LUA_VFALSE);
                cc.je(set_true);

                cc.and_(tag, 0x0F);
                cc.cmp(tag, LUA_TNIL);
                cc.je(set_true);

                cc.mov(ptr_tt(base, a), LUA_VFALSE);
                cc.jmp(set_false);

                cc.bind(set_true);
                cc.mov(ptr_tt(base, a), LUA_VTRUE);

                cc.bind(set_false);
                break;
            }
            case OP_LEN: {
                int b = GETARG_B(i);
                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, ptr_tt(base, b));

                Label try_string = cc.new_label();
                Label try_table = cc.new_label();
                Label done = cc.new_label();

                cc.cmp(tag, LUA_VSHRSTR);
                cc.je(try_string);
                cc.cmp(tag, LUA_VLNGSTR);
                cc.je(try_string);
                cc.cmp(tag, LUA_VTABLE);
                cc.je(try_table);
                EMIT_BAILOUT(cc.jmp, pc);

                cc.bind(try_string);
                x86::Gp str_ptr = cc.new_gp64();
                cc.mov(str_ptr, ptr_ivalue(base, b));
                Label is_shr = cc.new_label();
                Label loaded_len = cc.new_label();
                x86::Gp len = cc.new_gp64();

                cc.cmp(tag, LUA_VSHRSTR);
                cc.je(is_shr);
                cc.mov(len, x86::ptr(str_ptr, offsetof(TString, u)));
                cc.jmp(loaded_len);

                cc.bind(is_shr);
                cc.movzx(len, x86::byte_ptr(str_ptr, offsetof(TString, shrlen)));

                cc.bind(loaded_len);
                cc.mov(ptr_ivalue(base, a), len);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(done);

                cc.bind(try_table);
                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, b));
                cc.cmp(x86::qword_ptr(table_ptr, offsetof(Table, metatable)), 0);
                EMIT_BAILOUT(cc.jne, pc);

                InvokeNode* invoke;
                x86::Gp res = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getn, FuncSignature::build<lua_Unsigned, Table*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_ret(0, res);

                cc.mov(ptr_ivalue(base, a), res);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);

                cc.bind(done);
                break;
            }
            case OP_ADDI: {
                int b = GETARG_B(i);
                int sc = GETARG_sC(i);

                cc.cmp(ptr_tt(base, b), LUA_VNUMINT);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp vb = cc.new_gp64();
                cc.mov(vb, ptr_ivalue(base, b));
                cc.add(vb, sc);
                EMIT_BAILOUT(cc.jo, pc);

                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(labels[pc + 2]);
                break;
            }
            case OP_ADD: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                // Guard: check if inputs are integers
                cc.cmp(ptr_tt(base, b), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.cmp(ptr_tt(base, c), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp vb = cc.new_gp64();
                cc.mov(vb, ptr_ivalue(base, b));
                cc.add(vb, ptr_ivalue(base, c));
                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(labels[pc + 2]);
                break;
            }
            case OP_SUB: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                cc.cmp(ptr_tt(base, b), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.cmp(ptr_tt(base, c), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp vb = cc.new_gp64();
                cc.mov(vb, ptr_ivalue(base, b));
                cc.sub(vb, ptr_ivalue(base, c));
                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(labels[pc + 2]);
                break;
            }
            case OP_MUL: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                cc.cmp(ptr_tt(base, b), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.cmp(ptr_tt(base, c), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp vb = cc.new_gp64();
                cc.mov(vb, ptr_ivalue(base, b));
                cc.imul(vb, ptr_ivalue(base, c));
                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(labels[pc + 2]);
                break;
            }
            case OP_EQ: {
                int b = GETARG_B(i);
                x86::Gp va = cc.new_gp64();
                x86::Gp vb = cc.new_gp64();
                cc.mov(va, ptr_ivalue(base, a));
                cc.mov(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.jne(dest_false);
                else cc.je(dest_false);
                break;
            }
            case OP_LT: {
                int b = GETARG_B(i);
                x86::Gp va = cc.new_gp64();
                x86::Gp vb = cc.new_gp64();
                cc.mov(va, ptr_ivalue(base, a));
                cc.mov(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.jge(dest_false);
                else cc.jl(dest_false);
                break;
            }
            case OP_LE: {
                int b = GETARG_B(i);
                x86::Gp va = cc.new_gp64();
                x86::Gp vb = cc.new_gp64();
                cc.mov(va, ptr_ivalue(base, a));
                cc.mov(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.jg(dest_false);
                else cc.jle(dest_false);
                break;
            }
            case OP_EQK: {
                int b = GETARG_B(i);
                TValue* k_ptr = &p->k[b];
                if (ttisinteger(k_ptr)) {
                    lua_Integer k_val = ivalue(k_ptr);
                    cc.cmp(ptr_tt(base, a), LUA_VNUMINT);
                    EMIT_BAILOUT(cc.jne, pc);

                    x86::Gp val = cc.new_gp64();
                    cc.mov(val, ptr_ivalue(base, a));
                    cc.cmp(val, k_val);

                    Label dest_false = labels[pc + 2];
                    if (k_flag) cc.jne(dest_false);
                    else cc.je(dest_false);
                } else {
                    EMIT_BAILOUT(cc.jmp, pc);
                }
                break;
            }
            case OP_EQI: {
                int sb = GETARG_sB(i);
                cc.cmp(ptr_tt(base, a), LUA_VNUMINT);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp val = cc.new_gp64();
                cc.mov(val, ptr_ivalue(base, a));
                cc.cmp(val, sb);

                Label dest_false = labels[pc + 2];
                if (k_flag) cc.jne(dest_false);
                else cc.je(dest_false);
                break;
            }
            case OP_JMP: {
                int sJ = GETARG_sJ(i);
                cc.jmp(labels[pc + 1 + sJ]);
                break;
            }
            case OP_TEST: {
                int sJ = GETARG_sJ(p->code[pc+1]);
                Label skip = labels[pc + 2];

                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, ptr_tt(base, a));

                if (k_flag == 0) { // Jump if Falsy
                     Label stay = cc.new_label();
                     cc.cmp(tag, LUA_VFALSE);
                     cc.je(stay);

                     cc.and_(tag, 0x0F);
                     cc.cmp(tag, LUA_TNIL);
                     cc.je(stay);

                     cc.jmp(skip);

                     cc.bind(stay);
                } else { // Jump if Truthy
                     cc.cmp(tag, LUA_VFALSE);
                     cc.je(skip);

                     cc.and_(tag, 0x0F);
                     cc.cmp(tag, LUA_TNIL);
                     cc.je(skip);

                     // Fallthrough
                }
                break;
            }
            case OP_TESTSET: {
                int b = GETARG_B(i);
                int sJ = GETARG_sJ(p->code[pc+1]);
                Label skip = labels[pc + 2];

                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, ptr_tt(base, b)); // Check RB

                if (k_flag == 0) { // Jump if Falsy
                     Label try_copy = cc.new_label();
                     cc.cmp(tag, LUA_VFALSE);
                     cc.je(try_copy);

                     x86::Gp tag_masked = cc.new_gp32();
                     cc.mov(tag_masked, tag);
                     cc.and_(tag_masked, 0x0F);
                     cc.cmp(tag_masked, LUA_TNIL);
                     cc.je(try_copy);

                     cc.jmp(skip); // Truthy -> Skip

                     cc.bind(try_copy);
                     if (a != b) {
                         x86::Gp tmp1 = cc.new_gp64();
                         x86::Gp tmp2 = cc.new_gp64();
                         cc.mov(tmp1, x86::ptr(base, b * sizeof(StackValue)));
                         cc.mov(tmp2, x86::ptr(base, b * sizeof(StackValue) + 8));
                         cc.mov(x86::ptr(base, a * sizeof(StackValue)), tmp1);
                         cc.mov(x86::ptr(base, a * sizeof(StackValue) + 8), tmp2);
                     }
                     // Fallthrough to JMP
                } else { // Jump if Truthy
                     cc.cmp(tag, LUA_VFALSE);
                     cc.je(skip);

                     x86::Gp tag_masked = cc.new_gp32();
                     cc.mov(tag_masked, tag);
                     cc.and_(tag_masked, 0x0F);
                     cc.cmp(tag_masked, LUA_TNIL);
                     cc.je(skip);

                     // Truthy -> Copy and Fallthrough
                     if (a != b) {
                         x86::Gp tmp1 = cc.new_gp64();
                         x86::Gp tmp2 = cc.new_gp64();
                         cc.mov(tmp1, x86::ptr(base, b * sizeof(StackValue)));
                         cc.mov(tmp2, x86::ptr(base, b * sizeof(StackValue) + 8));
                         cc.mov(x86::ptr(base, a * sizeof(StackValue)), tmp1);
                         cc.mov(x86::ptr(base, a * sizeof(StackValue) + 8), tmp2);
                     }
                }
                break;
            }
            case OP_GETTABLE: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                // Guard: R[B] is table
                x86::Gp tb = cc.new_gp32();
                cc.movzx(tb, ptr_tt(base, b));
                cc.cmp(tb, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                // Load Table* t
                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, b));

                // Key ptr (R[C])
                x86::Gp key_ptr = cc.new_gp64();
                cc.lea(key_ptr, x86::ptr(base, c * sizeof(StackValue)));

                // Call luaH_get(t, key)
                InvokeNode* invoke;
                x86::Gp result_ptr = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_get, FuncSignature::build<const TValue*, Table*, const TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_arg(1, key_ptr);
                invoke->set_ret(0, result_ptr);

                // Check if result is nil (guard against __index)
                cc.cmp(x86::byte_ptr(result_ptr, offsetof(TValue, tt_)), LUA_VNIL);
                EMIT_BAILOUT(cc.jne, pc);

                // Copy result to R[A]
                x86::Gp val = cc.new_gp64();
                cc.mov(val, x86::ptr(result_ptr, offsetof(TValue, value_)));
                cc.mov(ptr_ivalue(base, a), val);

                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, x86::byte_ptr(result_ptr, offsetof(TValue, tt_)));
                cc.mov(ptr_tt(base, a), tag.r8());
                break;
            }
            case OP_SETTABLE: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                // Guard: R[A] is table
                x86::Gp ta = cc.new_gp32();
                cc.movzx(ta, ptr_tt(base, a));
                cc.cmp(ta, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, a));

                // Guard: table->metatable == NULL
                cc.cmp(x86::qword_ptr(table_ptr, offsetof(Table, metatable)), 0);
                EMIT_BAILOUT(cc.je, pc);

                // Key ptr (R[B])
                x86::Gp key_ptr = cc.new_gp64();
                cc.lea(key_ptr, x86::ptr(base, b * sizeof(StackValue)));

                // Value ptr (RK[C])
                x86::Gp val_ptr = cc.new_gp64();
                if (k_flag) {
                    TValue* k_val = &p->k[c];
                    cc.mov(val_ptr, (uint64_t)k_val);
                } else {
                    cc.lea(val_ptr, x86::ptr(base, c * sizeof(StackValue)));
                }

                // Call luaH_set(L, t, key, val)
                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_set, FuncSignature::build<void, lua_State*, Table*, const TValue*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, table_ptr);
                invoke->set_arg(2, key_ptr);
                invoke->set_arg(3, val_ptr);
                break;
            }
            case OP_GETFIELD: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                // Guard: R[B] is table
                x86::Gp tb = cc.new_gp32();
                cc.movzx(tb, ptr_tt(base, b));
                cc.cmp(tb, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, b));

                // Key (String) from K[C]
                x86::Gp key_str_ptr = cc.new_gp64();
                x86::Gp k_val_addr = cc.new_gp64();
                cc.mov(k_val_addr, (uint64_t)&p->k[c]);
                cc.mov(key_str_ptr, x86::ptr(k_val_addr, offsetof(TValue, value_)));

                // Call luaH_getshortstr(t, key)
                InvokeNode* invoke;
                x86::Gp result_ptr = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getshortstr, FuncSignature::build<const TValue*, Table*, TString*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_arg(1, key_str_ptr);
                invoke->set_ret(0, result_ptr);

                // Guard: result is nil
                cc.cmp(x86::byte_ptr(result_ptr, offsetof(TValue, tt_)), LUA_VNIL);
                EMIT_BAILOUT(cc.jne, pc);

                // Copy result
                x86::Gp val = cc.new_gp64();
                cc.mov(val, x86::ptr(result_ptr, offsetof(TValue, value_)));
                cc.mov(ptr_ivalue(base, a), val);

                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, x86::byte_ptr(result_ptr, offsetof(TValue, tt_)));
                cc.mov(ptr_tt(base, a), tag.r8());
                break;
            }
            case OP_SETFIELD: {
                int b = GETARG_B(i); // Key index in K
                int c = GETARG_C(i); // Value index (RK)

                // Guard: R[A] is table
                x86::Gp ta = cc.new_gp32();
                cc.movzx(ta, ptr_tt(base, a));
                cc.cmp(ta, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, a));

                // Guard: Metatable is NULL
                cc.cmp(x86::qword_ptr(table_ptr, offsetof(Table, metatable)), 0);
                EMIT_BAILOUT(cc.je, pc);

                // Key ptr (K[B]) - passed as TValue* to luaH_set
                x86::Gp key_ptr = cc.new_gp64();
                cc.mov(key_ptr, (uint64_t)&p->k[b]);

                // Value ptr (RK[C])
                x86::Gp val_ptr = cc.new_gp64();
                if (k_flag) {
                    TValue* k_val = &p->k[c];
                    cc.mov(val_ptr, (uint64_t)k_val);
                } else {
                    cc.lea(val_ptr, x86::ptr(base, c * sizeof(StackValue)));
                }

                // Call luaH_set(L, t, key, val)
                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_set, FuncSignature::build<void, lua_State*, Table*, const TValue*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, table_ptr);
                invoke->set_arg(2, key_ptr);
                invoke->set_arg(3, val_ptr);
                break;
            }
            case OP_GETI: {
                int b = GETARG_B(i); // Table
                int c = GETARG_C(i); // Key (int)

                x86::Gp tb = cc.new_gp32();
                cc.movzx(tb, ptr_tt(base, b));
                cc.cmp(tb, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, b));

                // Call luaH_getint(t, c)
                InvokeNode* invoke;
                x86::Gp result_ptr = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getint, FuncSignature::build<const TValue*, Table*, lua_Integer>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_arg(1, c); // Immediate integer
                invoke->set_ret(0, result_ptr);

                cc.cmp(x86::byte_ptr(result_ptr, offsetof(TValue, tt_)), LUA_VNIL);
                EMIT_BAILOUT(cc.jne, pc);

                x86::Gp val = cc.new_gp64();
                cc.mov(val, x86::ptr(result_ptr, offsetof(TValue, value_)));
                cc.mov(ptr_ivalue(base, a), val);

                x86::Gp tag = cc.new_gp32();
                cc.movzx(tag, x86::byte_ptr(result_ptr, offsetof(TValue, tt_)));
                cc.mov(ptr_tt(base, a), tag.r8());
                break;
            }
            case OP_SETI: {
                int b = GETARG_B(i); // Key (int)
                int c = GETARG_C(i); // Value (RK)

                x86::Gp ta = cc.new_gp32();
                cc.movzx(ta, ptr_tt(base, a));
                cc.cmp(ta, LUA_VTABLE);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp table_ptr = cc.new_gp64();
                cc.mov(table_ptr, ptr_ivalue(base, a));

                cc.cmp(x86::qword_ptr(table_ptr, offsetof(Table, metatable)), 0);
                EMIT_BAILOUT(cc.je, pc);

                x86::Gp val_ptr = cc.new_gp64();
                if (k_flag) {
                    TValue* k_val = &p->k[c];
                    cc.mov(val_ptr, (uint64_t)k_val);
                } else {
                    cc.lea(val_ptr, x86::ptr(base, c * sizeof(StackValue)));
                }

                // Call luaH_setint(L, t, key, val)
                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_setint, FuncSignature::build<void, lua_State*, Table*, lua_Integer, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, table_ptr);
                invoke->set_arg(2, b); // Immediate
                invoke->set_arg(3, val_ptr);
                break;
            }
            case OP_CALL: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                if (b == 0) { unsupported = true; break; }
                x86::Gp func_arg = cc.new_gp64();
                cc.lea(func_arg, x86::ptr(base, a * sizeof(StackValue)));
                x86::Gp call_addr = cc.new_gp64();
                cc.mov(call_addr, (uint64_t)&luaD_call);
                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), call_addr, FuncSignature::build<void, lua_State*, StkId, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, func_arg);
                invoke->set_arg(2, c - 1);
                // Reload base
                cc.mov(ci, x86::ptr(L_reg, offsetof(lua_State, ci)));
                cc.mov(func_ptr, x86::ptr(ci, offsetof(CallInfo, func) + offsetof(StkIdRel, p)));
                cc.lea(base, x86::ptr(func_ptr, sizeof(StackValue)));
                break;
            }
            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK: {
                cc.int3();
                break;
            }
            case OP_FORPREP: {
                long long bx = GETARG_Bx_64(i);
                long long jump_skip = pc + bx + 1;
                x86::Gp init = cc.new_gp64();
                x86::Gp count = cc.new_gp64();

                // Basic guard: check integer types
                cc.cmp(ptr_tt(base, a), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.cmp(ptr_tt(base, a + 1), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.cmp(ptr_tt(base, a + 2), LUA_VNUMINT);
                EMIT_BAILOUT(cc.je, pc);

                cc.mov(init, ptr_ivalue(base, a));
                cc.mov(ptr_ivalue(base, a + 3), init);
                cc.mov(ptr_tt(base, a + 3), LUA_VNUMINT);

                cc.mov(count, ptr_ivalue(base, a + 1));
                cc.sub(count, init);
                cc.cmp(count, 0);
                cc.jl(labels[jump_skip]);
                cc.mov(ptr_ivalue(base, a + 1), count);
                break;
            }
            case OP_FORLOOP: {
                long long bx = GETARG_Bx_64(i);
                long long jump_loop = pc - bx;

                // Optimized count handling
                x86::Mem count_mem = ptr_ivalue(base, a + 1);
                Label exit_loop = cc.new_label();

                cc.cmp(count_mem, 0);
                cc.jle(exit_loop);
                cc.dec(count_mem);

                // Optimized idx handling
                x86::Gp idx = cc.new_gp64();
                cc.mov(idx, ptr_ivalue(base, a));
                cc.add(idx, ptr_ivalue(base, a + 2));
                cc.mov(ptr_ivalue(base, a), idx);
                cc.mov(ptr_ivalue(base, a + 3), idx);
                cc.mov(ptr_tt(base, a + 3), LUA_VNUMINT);

                cc.jmp(labels[jump_loop]);
                cc.bind(exit_loop);
                break;
            }
            case OP_RETURN: {
                int b = GETARG_B(i);
                int n = b - 1;

                x86::Gp ra = cc.new_gp64();
                cc.lea(ra, x86::ptr(base, a * sizeof(StackValue)));

                if (n >= 0) {
                    x86::Gp new_top = cc.new_gp64();
                    cc.lea(new_top, x86::ptr(ra, n * sizeof(StackValue)));
                    cc.mov(x86::ptr(L_reg, offsetof(lua_State, top)), new_top);

                    InvokeNode* invoke;
                    cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                    invoke->set_arg(0, L_reg);
                    invoke->set_arg(1, ci);
                    invoke->set_arg(2, n);
                } else {
                    x86::Gp top_ptr = cc.new_gp64();
                    cc.mov(top_ptr, x86::ptr(L_reg, offsetof(lua_State, top)));
                    x86::Gp n_reg = cc.new_gp64();
                    cc.mov(n_reg, top_ptr);
                    cc.sub(n_reg, ra);
                    cc.sar(n_reg, 4);

                    InvokeNode* invoke;
                    cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                    invoke->set_arg(0, L_reg);
                    invoke->set_arg(1, ci);
                    invoke->set_arg(2, n_reg);
                }

                cc.mov(x86::eax, 1);
                cc.ret();
                break;
            }
            case OP_RETURN0: {
                x86::Gp ra = cc.new_gp64();
                cc.lea(ra, x86::ptr(base, a * sizeof(StackValue)));
                cc.mov(x86::ptr(L_reg, offsetof(lua_State, top)), ra);

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, ci);
                invoke->set_arg(2, 0);

                cc.mov(x86::eax, 1);
                cc.ret();
                break;
            }
            case OP_RETURN1: {
                x86::Gp ra = cc.new_gp64();
                cc.lea(ra, x86::ptr(base, a * sizeof(StackValue)));
                x86::Gp new_top = cc.new_gp64();
                cc.lea(new_top, x86::ptr(ra, sizeof(StackValue)));
                cc.mov(x86::ptr(L_reg, offsetof(lua_State, top)), new_top);

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, ci);
                invoke->set_arg(2, 1);

                cc.mov(x86::eax, 1);
                cc.ret();
                break;
            }
            default: {
                unsupported = true;
                break;
            }
        }
        if (unsupported) break;
    }
    #elif defined(JIT_ARCH_A64)
    a64::Compiler cc(&code);
    FuncNode* func_node = cc.add_func(FuncSignature::build<int, lua_State*>(CallConvId::kCDecl));
    a64::Gp L_reg = cc.new_gpz("L");
    func_node->set_arg(0, L_reg);
    a64::Gp base = cc.new_gpz("base");
    a64::Gp ci = cc.new_gpz("ci");
    cc.ldr(ci, a64::ptr(L_reg, offsetof(lua_State, ci)));
    a64::Gp func_ptr = cc.new_gpz("func");
    cc.ldr(func_ptr, a64::ptr(ci, offsetof(CallInfo, func) + offsetof(StkIdRel, p)));
    cc.add(base, func_ptr, sizeof(StackValue));
    std::vector<Label> labels(p->sizecode);
    for (int i = 0; i < p->sizecode; i++) labels[i] = cc.new_label();
    bool unsupported = false;

    // Helper macro for inline bailout
    #define EMIT_BAILOUT(cond_op, target_pc) do { \
        Label bailout = cc.new_label(); \
        Label skip = cc.new_label(); \
        cond_op(bailout); \
        cc.b(skip); \
        cc.bind(bailout); \
        a64::Gp code_ptr = cc.new_gp64(); \
        cc.ldr(code_ptr, a64::ptr(func_ptr, offsetof(Proto, code))); \
        a64::Gp pc_addr = cc.new_gp64(); \
        a64::Gp offset_reg = cc.new_gp64(); \
        cc.mov(offset_reg, target_pc * sizeof(Instruction)); \
        cc.add(pc_addr, code_ptr, offset_reg); \
        cc.str(pc_addr, a64::ptr(ci, offsetof(CallInfo, u))); \
        a64::Gp ret_reg = cc.new_gp32(); \
        cc.mov(ret_reg, 0); \
        cc.ret(ret_reg); \
        cc.bind(skip); \
    } while(0)

    for (int pc = 0; pc < p->sizecode; pc++) {
        cc.bind(labels[pc]);
        Instruction i = p->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);
        int k_flag = GETARG_k(i);

        switch (op) {
            case OP_MOVE: {
                int b = GETARG_B(i);
                a64::Gp tmp1 = cc.new_gp64();
                a64::Gp tmp2 = cc.new_gp64();
                cc.ldr(tmp1, a64::ptr(base, b * sizeof(StackValue)));
                cc.ldr(tmp2, a64::ptr(base, b * sizeof(StackValue) + 8));
                cc.str(tmp1, a64::ptr(base, a * sizeof(StackValue)));
                cc.str(tmp2, a64::ptr(base, a * sizeof(StackValue) + 8));
                break;
            }
            case OP_LOADI: {
                int sbx = GETARG_sBx(i);
                a64::Gp val = cc.new_gp64();
                cc.mov(val, sbx);
                cc.str(val, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                cc.b(labels[pc + 2]);
                break;
            }
            case OP_LOADK: {
                int bx = GETARG_Bx(i);
                TValue* k_ptr = &p->k[bx];
                a64::Gp k_addr = cc.new_gp64();
                cc.mov(k_addr, (uint64_t)k_ptr);
                a64::Gp val = cc.new_gp64();
                cc.ldr(val, a64::ptr(k_addr, offsetof(TValue, value_)));
                cc.str(val, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, a64::ptr(k_addr, offsetof(TValue, tt_)));
                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_LOADNIL: {
                int b = GETARG_B(i);
                a64::Gp val = cc.new_gp32();
                cc.mov(val, LUA_VNIL);
                for (int j = 0; j <= b; j++) {
                    cc.strb(val, ptr_tt(base, a + j));
                }
                break;
            }
            case OP_LOADFALSE: {
                a64::Gp val = cc.new_gp32();
                cc.mov(val, LUA_VFALSE);
                cc.strb(val, ptr_tt(base, a));
                break;
            }
            case OP_LOADTRUE: {
                a64::Gp val = cc.new_gp32();
                cc.mov(val, LUA_VTRUE);
                cc.strb(val, ptr_tt(base, a));
                break;
            }
            case OP_GETUPVAL: {
                int b = GETARG_B(i);
                a64::Gp closure = cc.new_gp64();
                cc.ldr(closure, a64::ptr(func_ptr, offsetof(TValue, value_)));

                a64::Gp upval = cc.new_gp64();
                cc.ldr(upval, a64::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                a64::Gp val_ptr = cc.new_gp64();
                cc.ldr(val_ptr, a64::ptr(upval, offsetof(UpVal, v)));

                a64::Gp tmp1 = cc.new_gp64();
                a64::Gp tmp2 = cc.new_gp64();
                cc.ldr(tmp1, a64::ptr(val_ptr, 0));
                cc.ldr(tmp2, a64::ptr(val_ptr, 8));

                cc.str(tmp1, a64::ptr(base, a * sizeof(StackValue)));
                cc.str(tmp2, a64::ptr(base, a * sizeof(StackValue) + 8));
                break;
            }
            case OP_SETUPVAL: {
                int b = GETARG_B(i);
                a64::Gp closure = cc.new_gp64();
                cc.ldr(closure, a64::ptr(func_ptr, offsetof(TValue, value_)));

                a64::Gp upval = cc.new_gp64();
                cc.ldr(upval, a64::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                a64::Gp val_ptr = cc.new_gp64();
                cc.ldr(val_ptr, a64::ptr(upval, offsetof(UpVal, v)));

                a64::Gp tmp1 = cc.new_gp64();
                a64::Gp tmp2 = cc.new_gp64();
                cc.ldr(tmp1, a64::ptr(base, a * sizeof(StackValue)));
                cc.ldr(tmp2, a64::ptr(base, a * sizeof(StackValue) + 8));

                cc.str(tmp1, a64::ptr(val_ptr, 0));
                cc.str(tmp2, a64::ptr(val_ptr, 8));

                a64::Gp val_addr = cc.new_gp64();
                cc.add(val_addr, base, a * sizeof(StackValue));

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&jit_barrier, FuncSignature::build<void, lua_State*, GCObject*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, upval);
                invoke->set_arg(2, val_addr);
                break;
            }
            case OP_GETTABUP: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                a64::Gp closure = cc.new_gp64();
                cc.ldr(closure, a64::ptr(func_ptr, offsetof(TValue, value_)));

                a64::Gp upval = cc.new_gp64();
                cc.ldr(upval, a64::ptr(closure, offsetof(LClosure, upvals) + b * sizeof(UpVal*)));

                a64::Gp val_ptr = cc.new_gp64();
                cc.ldr(val_ptr, a64::ptr(upval, offsetof(UpVal, v)));

                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, a64::ptr(val_ptr, offsetof(TValue, tt_)));
                cc.cmp(tt, LUA_VTABLE);
                EMIT_BAILOUT(cc.b_ne, pc);

                a64::Gp table_ptr = cc.new_gp64();
                cc.ldr(table_ptr, a64::ptr(val_ptr, offsetof(TValue, value_)));

                a64::Gp key_str = cc.new_gp64();
                a64::Gp k_val_addr = cc.new_gp64();
                cc.mov(k_val_addr, (uint64_t)&p->k[c]);
                cc.ldr(key_str, a64::ptr(k_val_addr, offsetof(TValue, value_)));

                InvokeNode* invoke;
                a64::Gp result_ptr = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getshortstr, FuncSignature::build<const TValue*, Table*, TString*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_arg(1, key_str);
                invoke->set_ret(0, result_ptr);

                cc.ldrb(tt, a64::ptr(result_ptr, offsetof(TValue, tt_)));
                cc.cmp(tt, LUA_VNIL);
                EMIT_BAILOUT(cc.b_eq, pc);

                a64::Gp val = cc.new_gp64();
                cc.ldr(val, a64::ptr(result_ptr, offsetof(TValue, value_)));
                cc.str(val, ptr_ivalue(base, a));

                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_SETTABUP: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                a64::Gp closure = cc.new_gp64();
                cc.ldr(closure, a64::ptr(func_ptr, offsetof(TValue, value_)));

                a64::Gp upval = cc.new_gp64();
                cc.ldr(upval, a64::ptr(closure, offsetof(LClosure, upvals) + a * sizeof(UpVal*)));

                a64::Gp val_ptr = cc.new_gp64();
                cc.ldr(val_ptr, a64::ptr(upval, offsetof(UpVal, v)));

                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, a64::ptr(val_ptr, offsetof(TValue, tt_)));
                cc.cmp(tt, LUA_VTABLE);
                EMIT_BAILOUT(cc.b_ne, pc);

                a64::Gp table_ptr = cc.new_gp64();
                cc.ldr(table_ptr, a64::ptr(val_ptr, offsetof(TValue, value_)));

                a64::Gp mt = cc.new_gp64();
                cc.ldr(mt, a64::ptr(table_ptr, offsetof(Table, metatable)));
                cc.cmp(mt, 0);
                EMIT_BAILOUT(cc.b_ne, pc);

                a64::Gp key_val_ptr = cc.new_gp64();
                cc.mov(key_val_ptr, (uint64_t)&p->k[b]);

                a64::Gp rk_val_ptr = cc.new_gp64();
                if (k_flag) {
                     cc.mov(rk_val_ptr, (uint64_t)&p->k[c]);
                } else {
                     cc.add(rk_val_ptr, base, c * sizeof(StackValue));
                }

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_set, FuncSignature::build<void, lua_State*, Table*, const TValue*, TValue*>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, table_ptr);
                invoke->set_arg(2, key_val_ptr);
                invoke->set_arg(3, rk_val_ptr);
                break;
            }
            case OP_UNM: {
                int b = GETARG_B(i);
                a64::Gp vb = cc.new_gp64();
                cc.ldr(vb, ptr_ivalue(base, b));

                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, ptr_tt(base, b));
                cc.cmp(tt, LUA_VNUMINT);
                EMIT_BAILOUT(cc.b_ne, pc);

                cc.neg(vb, vb);
                cc.str(vb, ptr_ivalue(base, a));
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_NOT: {
                int b = GETARG_B(i);
                a64::Gp tag = cc.new_gp32();
                cc.ldrb(tag, ptr_tt(base, b));

                Label set_true = cc.new_label();
                Label set_false = cc.new_label();

                cc.cmp(tag, LUA_VFALSE);
                cc.b_eq(set_true);

                cc.and_(tag, tag, 0x0F);
                cc.cmp(tag, LUA_TNIL);
                cc.b_eq(set_true);

                a64::Gp val = cc.new_gp32();
                cc.mov(val, LUA_VFALSE);
                cc.strb(val, ptr_tt(base, a));
                cc.b(set_false);

                cc.bind(set_true);
                cc.mov(val, LUA_VTRUE);
                cc.strb(val, ptr_tt(base, a));

                cc.bind(set_false);
                break;
            }
            case OP_LEN: {
                int b = GETARG_B(i);
                a64::Gp tag = cc.new_gp32();
                cc.ldrb(tag, ptr_tt(base, b));

                Label try_string = cc.new_label();
                Label try_table = cc.new_label();
                Label done = cc.new_label();

                cc.cmp(tag, LUA_VSHRSTR);
                cc.b_eq(try_string);
                cc.cmp(tag, LUA_VLNGSTR);
                cc.b_eq(try_string);
                cc.cmp(tag, LUA_VTABLE);
                cc.b_eq(try_table);
                EMIT_BAILOUT(cc.b, pc);

                cc.bind(try_string);
                a64::Gp str_ptr = cc.new_gp64();
                cc.ldr(str_ptr, ptr_ivalue(base, b));
                Label is_shr = cc.new_label();
                Label loaded_len = cc.new_label();
                a64::Gp len = cc.new_gp64();

                cc.cmp(tag, LUA_VSHRSTR);
                cc.b_eq(is_shr);
                cc.ldr(len, a64::ptr(str_ptr, offsetof(TString, u)));
                cc.b(loaded_len);

                cc.bind(is_shr);
                cc.ldrb(len, a64::ptr(str_ptr, offsetof(TString, shrlen)));

                cc.bind(loaded_len);
                cc.str(len, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                cc.b(done);

                cc.bind(try_table);
                a64::Gp table_ptr = cc.new_gp64();
                cc.ldr(table_ptr, ptr_ivalue(base, b));
                a64::Gp mt = cc.new_gp64();
                cc.ldr(mt, a64::ptr(table_ptr, offsetof(Table, metatable)));
                cc.cmp(mt, 0);
                EMIT_BAILOUT(cc.b_ne, pc);

                InvokeNode* invoke;
                a64::Gp res = cc.new_gp64();
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaH_getn, FuncSignature::build<lua_Unsigned, Table*>(CallConvId::kCDecl));
                invoke->set_arg(0, table_ptr);
                invoke->set_ret(0, res);

                cc.str(res, ptr_ivalue(base, a));
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));

                cc.bind(done);
                break;
            }
            case OP_ADDI: {
                int b = GETARG_B(i);
                int sc = GETARG_sC(i);

                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, ptr_tt(base, b));
                cc.cmp(tt, LUA_VNUMINT);
                EMIT_BAILOUT(cc.b_ne, pc);

                a64::Gp vb = cc.new_gp64();
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.adds(vb, vb, sc);
                EMIT_BAILOUT(cc.b_vs, pc);

                cc.str(vb, ptr_ivalue(base, a));
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                cc.b(labels[pc + 2]);
                break;
            }
            case OP_ADD: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                a64::Gp vb = cc.new_gp64();
                a64::Gp vc = cc.new_gp64();
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.ldr(vc, ptr_ivalue(base, c));
                cc.add(vb, vb, vc);
                cc.str(vb, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_SUB: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                a64::Gp vb = cc.new_gp64();
                a64::Gp vc = cc.new_gp64();
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.ldr(vc, ptr_ivalue(base, c));
                cc.sub(vb, vb, vc);
                cc.str(vb, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_MUL: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                a64::Gp vb = cc.new_gp64();
                a64::Gp vc = cc.new_gp64();
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.ldr(vc, ptr_ivalue(base, c));
                cc.mul(vb, vb, vc);
                cc.str(vb, ptr_ivalue(base, a));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a));
                break;
            }
            case OP_EQ: {
                int b = GETARG_B(i);
                a64::Gp va = cc.new_gp64();
                a64::Gp vb = cc.new_gp64();
                cc.ldr(va, ptr_ivalue(base, a));
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.b_ne(dest_false);
                else cc.b_eq(dest_false);
                break;
            }
            case OP_LT: {
                int b = GETARG_B(i);
                a64::Gp va = cc.new_gp64();
                a64::Gp vb = cc.new_gp64();
                cc.ldr(va, ptr_ivalue(base, a));
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.b_ge(dest_false);
                else cc.b_lt(dest_false);
                break;
            }
            case OP_LE: {
                int b = GETARG_B(i);
                a64::Gp va = cc.new_gp64();
                a64::Gp vb = cc.new_gp64();
                cc.ldr(va, ptr_ivalue(base, a));
                cc.ldr(vb, ptr_ivalue(base, b));
                cc.cmp(va, vb);
                Label dest_false = labels[pc + 2];
                if (k_flag) cc.b_gt(dest_false);
                else cc.b_le(dest_false);
                break;
            }
            case OP_EQK: {
                int b = GETARG_B(i);
                TValue* k_ptr = &p->k[b];
                if (ttisinteger(k_ptr)) {
                    lua_Integer k_val = ivalue(k_ptr);

                    a64::Gp tt = cc.new_gp32();
                    cc.ldrb(tt, ptr_tt(base, a));
                    cc.cmp(tt, LUA_VNUMINT);
                    EMIT_BAILOUT(cc.b_ne, pc);

                    a64::Gp val = cc.new_gp64();
                    cc.ldr(val, ptr_ivalue(base, a));

                    a64::Gp k_reg = cc.new_gp64();
                    cc.mov(k_reg, k_val);
                    cc.cmp(val, k_reg);

                    Label dest_false = labels[pc + 2];
                    if (k_flag) cc.b_ne(dest_false);
                    else cc.b_eq(dest_false);
                } else {
                    EMIT_BAILOUT(cc.b, pc);
                }
                break;
            }
            case OP_EQI: {
                int sb = GETARG_sB(i);

                a64::Gp tt = cc.new_gp32();
                cc.ldrb(tt, ptr_tt(base, a));
                cc.cmp(tt, LUA_VNUMINT);
                EMIT_BAILOUT(cc.b_ne, pc);

                a64::Gp val = cc.new_gp64();
                cc.ldr(val, ptr_ivalue(base, a));

                a64::Gp sb_reg = cc.new_gp64();
                cc.mov(sb_reg, sb);
                cc.cmp(val, sb_reg);

                Label dest_false = labels[pc + 2];
                if (k_flag) cc.b_ne(dest_false);
                else cc.b_eq(dest_false);
                break;
            }
            case OP_JMP: {
                int sJ = GETARG_sJ(i);
                cc.b(labels[pc + 1 + sJ]);
                break;
            }
            case OP_TEST: {
                int sJ = GETARG_sJ(p->code[pc+1]);
                Label skip = labels[pc + 2];

                a64::Gp tag = cc.new_gp32();
                cc.ldrb(tag, ptr_tt(base, a));

                if (k_flag == 0) { // Jump if Falsy
                   // If Truthy -> Skip.

                   cc.cmp(tag, LUA_VFALSE);
                   Label stay = cc.new_label();
                   cc.b_eq(stay);

                   cc.and_(tag, tag, 0x0F);
                   cc.cmp(tag, LUA_TNIL);
                   cc.b_eq(stay);

                   cc.b(skip);

                   cc.bind(stay);
                } else { // Jump if Truthy
                   // If Falsy -> Skip.

                   cc.cmp(tag, LUA_VFALSE);
                   cc.b_eq(skip);

                   cc.and_(tag, tag, 0x0F);
                   cc.cmp(tag, LUA_TNIL);
                   cc.b_eq(skip);

                   // Fallthrough
                }
                break;
            }
            case OP_TESTSET: {
                int b = GETARG_B(i);
                int sJ = GETARG_sJ(p->code[pc+1]);
                Label skip = labels[pc + 2];

                a64::Gp tag = cc.new_gp32();
                cc.ldrb(tag, ptr_tt(base, b));

                if (k_flag == 0) { // Jump if Falsy
                     Label try_copy = cc.new_label();
                     cc.cmp(tag, LUA_VFALSE);
                     cc.b_eq(try_copy);

                     a64::Gp tag_masked = cc.new_gp32();
                     cc.and_(tag_masked, tag, 0x0F);
                     cc.cmp(tag_masked, LUA_TNIL);
                     cc.b_eq(try_copy);

                     cc.b(skip); // Truthy -> Skip

                     cc.bind(try_copy);
                     if (a != b) {
                         a64::Gp tmp1 = cc.new_gp64();
                         a64::Gp tmp2 = cc.new_gp64();
                         cc.ldr(tmp1, a64::ptr(base, b * sizeof(StackValue)));
                         cc.ldr(tmp2, a64::ptr(base, b * sizeof(StackValue) + 8));
                         cc.str(tmp1, a64::ptr(base, a * sizeof(StackValue)));
                         cc.str(tmp2, a64::ptr(base, a * sizeof(StackValue) + 8));
                     }
                     // Fallthrough
                } else { // Jump if Truthy
                     cc.cmp(tag, LUA_VFALSE);
                     cc.b_eq(skip);

                     a64::Gp tag_masked = cc.new_gp32();
                     cc.and_(tag_masked, tag, 0x0F);
                     cc.cmp(tag_masked, LUA_TNIL);
                     cc.b_eq(skip);

                     // Truthy -> Copy and Fallthrough
                     if (a != b) {
                         a64::Gp tmp1 = cc.new_gp64();
                         a64::Gp tmp2 = cc.new_gp64();
                         cc.ldr(tmp1, a64::ptr(base, b * sizeof(StackValue)));
                         cc.ldr(tmp2, a64::ptr(base, b * sizeof(StackValue) + 8));
                         cc.str(tmp1, a64::ptr(base, a * sizeof(StackValue)));
                         cc.str(tmp2, a64::ptr(base, a * sizeof(StackValue) + 8));
                     }
                }
                break;
            }
            case OP_CALL: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                if (b == 0) { unsupported = true; break; }
                a64::Gp func_arg = cc.new_gp64();
                cc.add(func_arg, base, a * sizeof(StackValue));
                a64::Gp call_addr = cc.new_gp64();
                cc.mov(call_addr, (uint64_t)&luaD_call);
                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), call_addr, FuncSignature::build<void, lua_State*, StkId, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, func_arg);
                invoke->set_arg(2, c - 1);
                // Reload base
                cc.ldr(ci, a64::ptr(L_reg, offsetof(lua_State, ci)));
                cc.ldr(func_ptr, a64::ptr(ci, offsetof(CallInfo, func) + offsetof(StkIdRel, p)));
                cc.add(base, func_ptr, sizeof(StackValue));
                break;
            }
            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK: {
                cc.brk(0);
                break;
            }
            case OP_FORPREP: {
                int sbx = GETARG_sBx(i);
                int jump_skip = pc + sbx + 1;
                a64::Gp init = cc.new_gp64();
                a64::Gp limit = cc.new_gp64();
                a64::Gp step = cc.new_gp64();
                a64::Gp count = cc.new_gp64();
                cc.ldr(init, ptr_ivalue(base, a));
                cc.ldr(limit, ptr_ivalue(base, a + 1));
                cc.ldr(step, ptr_ivalue(base, a + 2));
                cc.str(init, ptr_ivalue(base, a + 3));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a + 3));
                cc.sub(count, limit, init);
                cc.cmp(count, 0);
                cc.b_lt(labels[jump_skip]);
                cc.str(count, ptr_ivalue(base, a + 1));
                break;
            }
            case OP_FORLOOP: {
                int sbx = GETARG_sBx(i);
                int jump_loop = pc - sbx;
                a64::Gp count = cc.new_gp64();
                cc.ldr(count, ptr_ivalue(base, a + 1));
                Label exit_loop = cc.new_label();
                cc.cmp(count, 0);
                cc.b_le(exit_loop);
                cc.sub(count, count, 1);
                cc.str(count, ptr_ivalue(base, a + 1));
                a64::Gp idx = cc.new_gp64();
                a64::Gp step = cc.new_gp64();
                cc.ldr(idx, ptr_ivalue(base, a));
                cc.ldr(step, ptr_ivalue(base, a + 2));
                cc.add(idx, idx, step);
                cc.str(idx, ptr_ivalue(base, a));
                cc.str(idx, ptr_ivalue(base, a + 3));
                a64::Gp tt = cc.new_gp32();
                cc.mov(tt, LUA_VNUMINT);
                cc.strb(tt, ptr_tt(base, a + 3));
                cc.b(labels[jump_loop]);
                cc.bind(exit_loop);
                break;
            }
            case OP_RETURN: {
                int b = GETARG_B(i);
                int n = b - 1;

                a64::Gp ra = cc.new_gp64();
                cc.add(ra, base, a * sizeof(StackValue));

                if (n >= 0) {
                    a64::Gp new_top = cc.new_gp64();
                    cc.add(new_top, ra, n * sizeof(StackValue));
                    cc.str(new_top, a64::ptr(L_reg, offsetof(lua_State, top)));

                    InvokeNode* invoke;
                    cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                    invoke->set_arg(0, L_reg);
                    invoke->set_arg(1, ci);
                    invoke->set_arg(2, n);
                } else {
                    a64::Gp top_ptr = cc.new_gp64();
                    cc.ldr(top_ptr, a64::ptr(L_reg, offsetof(lua_State, top)));
                    a64::Gp n_reg = cc.new_gp64();
                    cc.sub(n_reg, top_ptr, ra);
                    cc.lsr(n_reg, n_reg, 4);

                    InvokeNode* invoke;
                    cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                    invoke->set_arg(0, L_reg);
                    invoke->set_arg(1, ci);
                    invoke->set_arg(2, n_reg);
                }

                a64::Gp ret_reg = cc.new_gp32();
                cc.mov(ret_reg, 1);
                cc.ret(ret_reg);
                break;
            }
            case OP_RETURN0: {
                a64::Gp ra = cc.new_gp64();
                cc.add(ra, base, a * sizeof(StackValue));
                cc.str(ra, a64::ptr(L_reg, offsetof(lua_State, top)));

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, ci);
                invoke->set_arg(2, 0);

                a64::Gp ret_reg = cc.new_gp32();
                cc.mov(ret_reg, 1);
                cc.ret(ret_reg);
                break;
            }
            case OP_RETURN1: {
                a64::Gp ra = cc.new_gp64();
                cc.add(ra, base, a * sizeof(StackValue));
                a64::Gp new_top = cc.new_gp64();
                cc.add(new_top, ra, sizeof(StackValue));
                cc.str(new_top, a64::ptr(L_reg, offsetof(lua_State, top)));

                InvokeNode* invoke;
                cc.invoke(asmjit::Out(invoke), (uint64_t)&luaD_poscall, FuncSignature::build<void, lua_State*, CallInfo*, int>(CallConvId::kCDecl));
                invoke->set_arg(0, L_reg);
                invoke->set_arg(1, ci);
                invoke->set_arg(2, 1);

                a64::Gp ret_reg = cc.new_gp32();
                cc.mov(ret_reg, 1);
                cc.ret(ret_reg);
                break;
            }
            default: {
                unsupported = true;
                break;
            }
        }
        if (unsupported) break;
    }
    #endif

    if (unsupported) {
        return 0;
    }

    cc.end_func();
    cc.finalize();

    JitFunction func;
    Error err = g_jitRuntime->add(&func, &code);

    if (err != kErrorOk) {
        printf("JIT Error: %d\n", (int)err);
        return 0;
    }

    p->jit_code = (void*)func;
    return 1;
}

extern "C" void jit_free(Proto *p) {
    p->jit_code = NULL;
}

#else

extern "C" void jit_init(void) {
}

extern "C" int jit_compile(lua_State *L, Proto *p) {
    return 0;
}

extern "C" void jit_free(Proto *p) {
    p->jit_code = NULL;
}

#endif
