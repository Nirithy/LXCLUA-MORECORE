#include "jit_backend.h"
#include <stdio.h>
#include <vector>

// Handle C11 _Atomic in C++
#ifdef __cplusplus
#include <atomic>
#define _Atomic(T) std::atomic<T>
#endif

// Define LUA_CORE to access internal structs
#define LUA_CORE
#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"

// Undefine to avoid pollution
#undef _Atomic

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
    jit_init();

    CodeHolder code;
    code.init(g_jitRuntime->environment());

    #ifdef JIT_ARCH_X86
    x86::Compiler cc(&code);
    FuncNode* func_node = cc.add_func(FuncSignature::build<int, void*>(CallConvId::kCDecl));

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

    for (int pc = 0; pc < p->sizecode; pc++) {
        cc.bind(labels[pc]);

        Instruction i = p->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);

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
            case OP_ADD: {
                int b = GETARG_B(i);
                int c = GETARG_C(i);

                x86::Gp vb = cc.new_gp64();
                x86::Gp vc = cc.new_gp64();

                cc.mov(vb, ptr_ivalue(base, b));
                cc.mov(vc, ptr_ivalue(base, c));

                cc.add(vb, vc);

                cc.mov(ptr_ivalue(base, a), vb);
                cc.mov(ptr_tt(base, a), LUA_VNUMINT);
                cc.jmp(labels[pc + 2]);
                break;
            }
            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK: {
                // Fallback instructions for metamethods
                // Since we assume fast path in OP_ADD, these should be unreachable
                // or we can just trap.
                cc.int3();
                break;
            }
            case OP_FORPREP: {
                long long bx = GETARG_Bx_64(i);
                long long jump_skip = pc + bx + 1;

                x86::Gp init = cc.new_gp64();
                x86::Gp limit = cc.new_gp64();
                x86::Gp step = cc.new_gp64();
                x86::Gp count = cc.new_gp64();

                cc.mov(init, ptr_ivalue(base, a));
                cc.mov(limit, ptr_ivalue(base, a + 1));
                cc.mov(step, ptr_ivalue(base, a + 2));

                cc.mov(ptr_ivalue(base, a + 3), init);
                cc.mov(ptr_tt(base, a + 3), LUA_VNUMINT);

                cc.mov(count, limit);
                cc.sub(count, init);

                cc.cmp(count, 0);
                cc.jl(labels[jump_skip]);

                cc.mov(ptr_ivalue(base, a + 1), count);
                break;
            }
            case OP_FORLOOP: {
                long long bx = GETARG_Bx_64(i);
                long long jump_loop = pc - bx;

                x86::Gp count = cc.new_gp64();
                cc.mov(count, ptr_ivalue(base, a + 1));

                Label exit_loop = cc.new_label();

                cc.cmp(count, 0);
                cc.jle(exit_loop);

                cc.dec(count);
                cc.mov(ptr_ivalue(base, a + 1), count);

                x86::Gp idx = cc.new_gp64();
                x86::Gp step = cc.new_gp64();

                cc.mov(idx, ptr_ivalue(base, a));
                cc.mov(step, ptr_ivalue(base, a + 2));

                cc.add(idx, step);
                cc.mov(ptr_ivalue(base, a), idx);
                cc.mov(ptr_ivalue(base, a + 3), idx);
                cc.mov(ptr_tt(base, a + 3), LUA_VNUMINT);

                cc.jmp(labels[jump_loop]);

                cc.bind(exit_loop);
                break;
            }
            case OP_RETURN:
            case OP_RETURN0:
            case OP_RETURN1: {
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

    // Load base = L->ci->func.p + 1
    a64::Gp ci = cc.new_gpz("ci");
    cc.ldr(ci, a64::ptr(L_reg, offsetof(lua_State, ci)));

    a64::Gp func_ptr = cc.new_gpz("func");
    cc.ldr(func_ptr, a64::ptr(ci, offsetof(CallInfo, func) + offsetof(StkIdRel, p)));

    cc.add(base, func_ptr, sizeof(StackValue));

    std::vector<Label> labels(p->sizecode);
    for (int i = 0; i < p->sizecode; i++) {
        labels[i] = cc.new_label();
    }

    bool unsupported = false;

    for (int pc = 0; pc < p->sizecode; pc++) {
        cc.bind(labels[pc]);

        Instruction i = p->code[pc];
        OpCode op = GET_OPCODE(i);
        int a = GETARG_A(i);

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
            case OP_MMBIN:
            case OP_MMBINI:
            case OP_MMBINK: {
                cc.brk(0);
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
            case OP_RETURN:
            case OP_RETURN0:
            case OP_RETURN1: {
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
