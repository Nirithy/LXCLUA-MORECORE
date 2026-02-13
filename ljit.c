#define ljit_c
#define LUA_CORE

#include "lprefix.h"
#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "ljit.h"

#if defined(__x86_64__) && defined(__linux__)
#include "ljit_emit_x64.h"
#else
#include "ljit_emit_stub.h"
#endif

/*
** Simple JIT Compiler for LXCLUA
** Currently supports OP_RETURN0 for verification.
*/

void luaJ_prep_return0(lua_State *L, CallInfo *ci) {
  L->top.p = ci->func.p;
}

void luaJ_prep_return1(lua_State *L, CallInfo *ci, int ra) {
  StkId base = ci->func.p + 1;
  setobjs2s(L, base - 1, base + ra);
  L->top.p = base;
}

void luaJ_compile(lua_State *L, Proto *p) {
  if (p->jit_code) return; // Already compiled

  // Simple heuristic: Only compile functions with supported opcodes
  if (p->sizecode < 1) return;

  // First pass: Verify all opcodes are supported
  for (int i = 0; i < p->sizecode; i++) {
    Instruction inst = p->code[i];
    OpCode op = GET_OPCODE(inst);
    switch (op) {
      case OP_ADD:
      case OP_SUB:
      case OP_RETURN0:
      case OP_RETURN1:
      case OP_MMBIN:
        break;
      default:
        return; // Unsupported opcode, abort JIT
    }
  }

  JitState *J = jit_new_state();
  if (!J) return; // Architecture not supported or allocation failed

  if (!jit_begin(J, JIT_BUFFER_SIZE)) {
    jit_free_state(J);
    return;
  }

  // Prologue
  jit_emit_prologue(J);

  // Second pass: Emit code
  for (int i = 0; i < p->sizecode; i++) {
    Instruction inst = p->code[i];
    OpCode op = GET_OPCODE(inst);
    switch (op) {
      case OP_RETURN0:
        jit_emit_op_return0(J);
        break;
      case OP_RETURN1:
        jit_emit_op_return1(J, GETARG_A(inst));
        break;
      case OP_ADD:
        jit_emit_op_add(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]);
        break;
      case OP_SUB:
        jit_emit_op_sub(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]);
        break;
      case OP_MMBIN:
        // Metadata for interpreter, ignored by JIT
        break;
      default:
        break;
    }
  }

  // Epilogue
  jit_emit_epilogue(J);

  // Finalize
  jit_end(J, p);

  jit_free_state(J);
}

void luaJ_freeproto(Proto *p) {
  jit_free_code(p);
}
