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

#if defined(__x86_64__) || defined(_M_X64)
  #if defined(__linux__) || defined(__APPLE__)
    #include "ljit_emit_x64.h"
  #else
    #include "ljit_emit_stub.h"
  #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
  #if defined(__linux__) || defined(__APPLE__)
    #include "ljit_emit_arm64.h"
  #else
    #include "ljit_emit_stub.h"
  #endif
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
      case OP_MOVE: jit_emit_op_move(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_LOADI: jit_emit_op_loadi(J, GETARG_A(inst), GETARG_sBx(inst)); break;
      case OP_LOADF: jit_emit_op_loadf(J, GETARG_A(inst), GETARG_sBx(inst)); break;
      case OP_LOADK: jit_emit_op_loadk(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_LOADKX: jit_emit_op_loadkx(J, GETARG_A(inst)); break;
      case OP_LOADFALSE: jit_emit_op_loadfalse(J, GETARG_A(inst)); break;
      case OP_LFALSESKIP: jit_emit_op_lfalseskip(J, GETARG_A(inst)); break;
      case OP_LOADTRUE: jit_emit_op_loadtrue(J, GETARG_A(inst)); break;
      case OP_LOADNIL: jit_emit_op_loadnil(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_GETUPVAL: jit_emit_op_getupval(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_SETUPVAL: jit_emit_op_setupval(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_GETTABUP: jit_emit_op_gettabup(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_GETTABLE: jit_emit_op_gettable(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_GETI: jit_emit_op_geti(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_GETFIELD: jit_emit_op_getfield(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETTABUP: jit_emit_op_settabup(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETTABLE: jit_emit_op_settable(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETI: jit_emit_op_seti(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETFIELD: jit_emit_op_setfield(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_NEWTABLE: jit_emit_op_newtable(J, GETARG_A(inst), GETARG_vB(inst), GETARG_vC(inst), GETARG_k(inst)); break;
      case OP_SELF: jit_emit_op_self(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_ADDI: jit_emit_op_addi(J, GETARG_A(inst), GETARG_B(inst), GETARG_sC(inst), &p->code[i+1]); break;
      case OP_ADDK: jit_emit_op_addk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SUBK: jit_emit_op_subk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_MULK: jit_emit_op_mulk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_MODK: jit_emit_op_modk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_POWK: jit_emit_op_powk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_DIVK: jit_emit_op_divk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_IDIVK: jit_emit_op_idivk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BANDK: jit_emit_op_bandk(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BORK: jit_emit_op_bork(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BXORK: jit_emit_op_bxork(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SHLI: jit_emit_op_shli(J, GETARG_A(inst), GETARG_B(inst), GETARG_sC(inst), &p->code[i+1]); break;
      case OP_SHRI: jit_emit_op_shri(J, GETARG_A(inst), GETARG_B(inst), GETARG_sC(inst), &p->code[i+1]); break;
      case OP_ADD: jit_emit_op_add(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SUB: jit_emit_op_sub(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_MUL: jit_emit_op_mul(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_MOD: jit_emit_op_mod(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_POW: jit_emit_op_pow(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_DIV: jit_emit_op_div(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_IDIV: jit_emit_op_idiv(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BAND: jit_emit_op_band(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BOR: jit_emit_op_bor(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_BXOR: jit_emit_op_bxor(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SHL: jit_emit_op_shl(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SHR: jit_emit_op_shr(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), &p->code[i+1]); break;
      case OP_SPACESHIP: jit_emit_op_spaceship(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_MMBIN: break; // Metadata
      case OP_MMBINI: break; // Metadata
      case OP_MMBINK: break; // Metadata
      case OP_UNM: jit_emit_op_unm(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_BNOT: jit_emit_op_bnot(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_NOT: jit_emit_op_not(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_LEN: jit_emit_op_len(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_CONCAT: jit_emit_op_concat(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_CLOSE: jit_emit_op_close(J, GETARG_A(inst)); break;
      case OP_TBC: jit_emit_op_tbc(J, GETARG_A(inst)); break;
      case OP_JMP: jit_emit_op_jmp(J, GETARG_sJ(inst)); break;
      case OP_EQ: jit_emit_op_eq(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_LT: jit_emit_op_lt(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_LE: jit_emit_op_le(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_EQK: jit_emit_op_eqk(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_EQI: jit_emit_op_eqi(J, GETARG_A(inst), GETARG_sB(inst), GETARG_k(inst)); break;
      case OP_LTI: jit_emit_op_lti(J, GETARG_A(inst), GETARG_sB(inst), GETARG_k(inst)); break;
      case OP_LEI: jit_emit_op_lei(J, GETARG_A(inst), GETARG_sB(inst), GETARG_k(inst)); break;
      case OP_GTI: jit_emit_op_gti(J, GETARG_A(inst), GETARG_sB(inst), GETARG_k(inst)); break;
      case OP_GEI: jit_emit_op_gei(J, GETARG_A(inst), GETARG_sB(inst), GETARG_k(inst)); break;
      case OP_TEST: jit_emit_op_test(J, GETARG_A(inst), GETARG_k(inst)); break;
      case OP_TESTSET: jit_emit_op_testset(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_CALL: jit_emit_op_call(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_TAILCALL: jit_emit_op_tailcall(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), GETARG_k(inst)); break;
      case OP_RETURN: jit_emit_op_return(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), GETARG_k(inst)); break;
      case OP_RETURN0: jit_emit_op_return0(J); break;
      case OP_RETURN1: jit_emit_op_return1(J, GETARG_A(inst)); break;
      case OP_FORLOOP: jit_emit_op_forloop(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_FORPREP: jit_emit_op_forprep(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_TFORPREP: jit_emit_op_tforprep(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_TFORCALL: jit_emit_op_tforcall(J, GETARG_A(inst), GETARG_C(inst)); break;
      case OP_TFORLOOP: jit_emit_op_tforloop(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_SETLIST: jit_emit_op_setlist(J, GETARG_A(inst), GETARG_vB(inst), GETARG_vC(inst), GETARG_k(inst)); break;
      case OP_CLOSURE: jit_emit_op_closure(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_VARARG: jit_emit_op_vararg(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), GETARG_k(inst)); break;
      case OP_GETVARG: jit_emit_op_getvarg(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_ERRNNIL: jit_emit_op_errnnil(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_VARARGPREP: jit_emit_op_varargprep(J, GETARG_A(inst)); break;
      case OP_IS: jit_emit_op_is(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), GETARG_k(inst)); break;
      case OP_TESTNIL: jit_emit_op_testnil(J, GETARG_A(inst), GETARG_B(inst), GETARG_k(inst)); break;
      case OP_NEWCLASS: jit_emit_op_newclass(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_INHERIT: jit_emit_op_inherit(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_GETSUPER: jit_emit_op_getsuper(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETMETHOD: jit_emit_op_setmethod(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETSTATIC: jit_emit_op_setstatic(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_NEWOBJ: jit_emit_op_newobj(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_GETPROP: jit_emit_op_getprop(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SETPROP: jit_emit_op_setprop(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_INSTANCEOF: jit_emit_op_instanceof(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst), GETARG_k(inst)); break;
      case OP_IMPLEMENT: jit_emit_op_implement(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_SETIFACEFLAG: jit_emit_op_setifaceflag(J, GETARG_A(inst)); break;
      case OP_ADDMETHOD: jit_emit_op_addmethod(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_IN: jit_emit_op_in(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_SLICE: jit_emit_op_slice(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_NOP: jit_emit_op_nop(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_CASE: jit_emit_op_case(J, GETARG_A(inst), GETARG_B(inst), GETARG_C(inst)); break;
      case OP_NEWCONCEPT: jit_emit_op_newconcept(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_NEWNAMESPACE: jit_emit_op_newnamespace(J, GETARG_A(inst), GETARG_Bx(inst)); break;
      case OP_LINKNAMESPACE: jit_emit_op_linknamespace(J, GETARG_A(inst), GETARG_B(inst)); break;
      case OP_EXTRAARG: break;
      default: break;
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
