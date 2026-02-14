#ifndef ljit_emit_x64_h
#define ljit_emit_x64_h

#include "lprefix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lua.h"
#include "ldo.h"
#include "ljit.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#include "lopcodes.h"

/*
** x64 JIT Backend
** Directly included by ljit.c
*/

#define JIT_BUFFER_SIZE 4096

typedef struct JitState {
  unsigned char *code;
  size_t size;
  size_t capacity;
} JitState;

/* Register aliases for readability */
#define RA_RAX 0
#define RA_RCX 1
#define RA_RDX 2
#define RA_RBX 3
#define RA_RSP 4
#define RA_RBP 5
#define RA_RSI 6
#define RA_RDI 7
#define RA_R8  8
#define RA_R9  9
#define RA_R10 10
#define RA_R11 11
#define RA_R12 12
#define RA_R13 13
#define RA_R14 14
#define RA_R15 15

/* Helper functions */

static void emit_byte(JitState *J, unsigned char b) {
  if (J->size < J->capacity) {
    J->code[J->size++] = b;
  }
}

static void emit_u32(JitState *J, unsigned int u) {
  for (int i = 0; i < 4; i++) {
    emit_byte(J, (u >> (i * 8)) & 0xFF);
  }
}

static void emit_u64(JitState *J, unsigned long long u) {
  for (int i = 0; i < 8; i++) {
    emit_byte(J, (u >> (i * 8)) & 0xFF);
  }
}

static void *alloc_exec_mem(size_t size) {
  void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) return NULL;
  return ptr;
}

static JitState *jit_new_state(void) {
  JitState *J = (JitState *)malloc(sizeof(JitState));
  if (J) {
    J->code = NULL;
    J->size = 0;
    J->capacity = 0;
  }
  return J;
}

static void jit_free_state(JitState *J) {
  if (J) free(J);
}

static int jit_begin(JitState *J, size_t initial_size) {
  unsigned char *mem = (unsigned char *)alloc_exec_mem(initial_size);
  if (!mem) return 0;
  J->code = mem;
  J->size = 0;
  J->capacity = initial_size;
  return 1;
}

static void jit_end(JitState *J, Proto *p) {
  p->jit_code = J->code;
  p->jit_size = J->size;
}

static void jit_free_code(Proto *p) {
  if (p->jit_code) {
    munmap(p->jit_code, JIT_BUFFER_SIZE);
    p->jit_code = NULL;
    p->jit_size = 0;
  }
}

/*
** Instruction Emitters (Instruction Formats)
** These make the JIT logic look like assembly.
*/

// PUSH reg
static void ASM_PUSH_R(JitState *J, int reg) {
  if (reg >= 8) emit_byte(J, 0x41);
  emit_byte(J, 0x50 + (reg & 7));
}

// POP reg
static void ASM_POP_R(JitState *J, int reg) {
  if (reg >= 8) emit_byte(J, 0x41);
  emit_byte(J, 0x58 + (reg & 7));
}

// MOV dst, src (64-bit)
static void ASM_MOV_RR(JitState *J, int dst, int src) {
  // REX prefix
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4; // REX.R
  if (dst >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0x89);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// MOV reg, imm64
static void ASM_MOV_R_IMM(JitState *J, int reg, unsigned long long imm) {
  // REX prefix
  unsigned char rex = 0x48;
  if (reg >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0xB8 + (reg & 7));
  emit_u64(J, imm);
}

// MOV reg, imm32
static void ASM_MOV_R_IMM32(JitState *J, int reg, int imm) {
  // REX prefix
  unsigned char rex = 0x48;
  if (reg >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0xC7);
  emit_byte(J, 0xC0 + (reg & 7)); // ModRM: 11 000 reg
  emit_u32(J, imm);
}

// CALL reg (indirect call)
static void ASM_CALL_R(JitState *J, int reg) {
  if (reg >= 8) emit_byte(J, 0x41);
  emit_byte(J, 0xFF);
  emit_byte(J, 0xD0 + (reg & 7));
}

// RET
static void ASM_RET(JitState *J) {
  emit_byte(J, 0xC3);
}

// XOR dst, src
static void ASM_XOR_RR(JitState *J, int dst, int src) {
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4;
  if (dst >= 8) rex |= 1;
  emit_byte(J, rex);
  emit_byte(J, 0x31);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// MOV [base], reg
// Implementation specific to [r12] access (common in this JIT)
// or generic [reg], reg
static void ASM_MOV_MEM_R(JitState *J, int base, int offset, int src) {
  // MOV [base + offset], src
  unsigned char rex = 0x49; // Assuming base is R12/R13/etc (r12 is used for ci)
  if (src >= 8) rex |= 4; // REX.R
  // if base >= 8, usually implicit in addressing mode, but here we assume simple indirect
  // Specifically for [R12 + offset]
  // R12 is 1100 (12). REX.B needed? R12 is >= 8. Yes.
  if (base >= 8) rex |= 1;

  emit_byte(J, rex);
  emit_byte(J, 0x89);

  // ModRM
  // Mod = 01 (disp8) or 10 (disp32). Let's assume disp32 for safety or simple logic.
  // Reg = src
  // R/M = base

  // Special case for R12 (requires SIB)
  if ((base & 7) == 4) { // RSP or R12
     // SIB needed
     // ModRM: Mod=10 (disp32), Reg=src, RM=100 (SIB)
     emit_byte(J, 0x84 | ((src & 7) << 3));
     // SIB: Scale=00, Index=100 (none), Base=base&7 (100 -> 4 -> R12)
     emit_byte(J, 0x24);
     emit_u32(J, offset);
  } else {
     // ModRM: Mod=10 (disp32), Reg=src, RM=base
     emit_byte(J, 0x80 | ((src & 7) << 3) | (base & 7));
     emit_u32(J, offset);
  }
}

// MOV reg, [base + offset]
static void ASM_MOV_R_MEM(JitState *J, int dst, int base, int offset) {
  unsigned char rex = 0x49; // REX.W + REX.B (assuming base >= 8 like R12)
  if (dst >= 8) rex |= 4; // REX.R
  if (base >= 8) rex |= 1; // REX.B

  emit_byte(J, rex);
  emit_byte(J, 0x8B);

  // Special case for R12
  if ((base & 7) == 4) {
    emit_byte(J, 0x84 | ((dst & 7) << 3)); // ModRM with SIB
    emit_byte(J, 0x24); // SIB for R12
    emit_u32(J, offset);
  } else {
    emit_byte(J, 0x80 | ((dst & 7) << 3) | (base & 7));
    emit_u32(J, offset);
  }
}

// ADD reg, imm32
static void ASM_ADD_R_IMM32(JitState *J, int reg, int imm) {
  unsigned char rex = 0x48;
  if (reg >= 8) rex |= 1;
  emit_byte(J, rex);

  if (reg == RA_RAX && 0) { // Optimization for RAX? No, generic
    // ...
  }

  emit_byte(J, 0x81);
  emit_byte(J, 0xC0 + (reg & 7));
  emit_u32(J, imm);
}

/*
** JIT Logic Implementations
*/

static void jit_emit_prologue(JitState *J) {
  /*
  ** x86-64 calling convention (System V AMD64 ABI):
  ** Args: RDI, RSI, RDX, RCX, R8, R9
  ** Return: RAX
  ** Callee-saved: RBX, RBP, R12-R15
  */
  ASM_PUSH_R(J, RA_RBP);
  ASM_MOV_RR(J, RA_RBP, RA_RSP);

  // Save L (RDI) and ci (RSI) to callee-saved registers
  ASM_PUSH_R(J, RA_RBX);
  ASM_PUSH_R(J, RA_R12);

  ASM_MOV_RR(J, RA_RBX, RA_RDI); // L -> RBX
  ASM_MOV_RR(J, RA_R12, RA_RSI); // ci -> R12
}

static void jit_emit_epilogue(JitState *J) {
  ASM_POP_R(J, RA_R12);
  ASM_POP_R(J, RA_RBX);
  ASM_POP_R(J, RA_RBP);
  ASM_RET(J);
}

static void jit_emit_op_return0(JitState *J) {
  // Prep stack: luaJ_prep_return0(L, ci)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_prep_return0);
  ASM_CALL_R(J, RA_RAX);

  // Call luaD_poscall(L, ci, 0)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_XOR_RR(J, RA_RDX, RA_RDX); // 0

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaD_poscall);
  ASM_CALL_R(J, RA_RAX);

  // Set return value to 1 (success)
  ASM_MOV_R_IMM32(J, RA_RAX, 1);

  jit_emit_epilogue(J);
}

static void jit_emit_op_return1(JitState *J, int ra) {
  // Prep stack: luaJ_prep_return1(L, ci, ra)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_MOV_R_IMM32(J, RA_RDX, ra);

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_prep_return1);
  ASM_CALL_R(J, RA_RAX);

  // Call luaD_poscall(L, ci, 1)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_MOV_R_IMM32(J, RA_RDX, 1);

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaD_poscall);
  ASM_CALL_R(J, RA_RAX);

  // Set return value to 1 (success)
  ASM_MOV_R_IMM32(J, RA_RAX, 1);

  jit_emit_epilogue(J);
}

static void emit_arith_common(JitState *J, int ra, int rb, int rc, const Instruction *next_pc, int op) {
  // Update savedpc: mov [r12 + 32], next_pc
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX); // ci->u.l.savedpc is at offset 32 (8*4 pointers?)

  // Call luaO_arith(L, op, &rb, &rc, &ra)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L

  ASM_MOV_R_IMM32(J, RA_RSI, op); // op

  // Calculate addresses of registers
  // base = [r12] + 16 (ci->func.p is at 0, func object is at func.p, base is func.p + 1)

  // RDX = &R[rb]
  ASM_MOV_R_MEM(J, RA_RDX, RA_R12, 0); // mov rdx, [r12] (ci->func.p)
  ASM_ADD_R_IMM32(J, RA_RDX, 16 + rb * 16); // base + rb

  // RCX = &R[rc]
  ASM_MOV_R_MEM(J, RA_RCX, RA_R12, 0); // mov rcx, [r12]
  ASM_ADD_R_IMM32(J, RA_RCX, 16 + rc * 16);

  // R8 = &R[ra]
  ASM_MOV_R_MEM(J, RA_R8, RA_R12, 0); // mov r8, [r12]
  ASM_ADD_R_IMM32(J, RA_R8, 16 + ra * 16);

  // Call luaO_arith
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaO_arith);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_add(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) {
  emit_arith_common(J, ra, rb, rc, next_pc, LUA_OPADD);
}

static void jit_emit_op_sub(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) {
  emit_arith_common(J, ra, rb, rc, next_pc, LUA_OPSUB);
}

static void emit_unary_arith_common(JitState *J, int ra, int rb, const Instruction *next_pc, int op) {
  // Update savedpc: mov [r12 + 32], next_pc
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);

  // Call luaO_arith(L, op, &rb, &rb, &ra)
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L

  ASM_MOV_R_IMM32(J, RA_RSI, op); // op

  // RDX = &R[rb]
  ASM_MOV_R_MEM(J, RA_RDX, RA_R12, 0); // ci->func.p
  ASM_ADD_R_IMM32(J, RA_RDX, 16 + rb * 16); // base + rb

  // RCX = &R[rb] (same as RDX)
  ASM_MOV_RR(J, RA_RCX, RA_RDX);

  // R8 = &R[ra]
  ASM_MOV_R_MEM(J, RA_R8, RA_R12, 0); // ci->func.p
  ASM_ADD_R_IMM32(J, RA_R8, 16 + ra * 16);

  // Call luaO_arith
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaO_arith);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_mul(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPMUL);
}

static void jit_emit_op_mod(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPMOD);
}

static void jit_emit_op_pow(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPPOW);
}

static void jit_emit_op_div(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPDIV);
}

static void jit_emit_op_idiv(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPIDIV);
}

static void jit_emit_op_band(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPBAND);
}

static void jit_emit_op_bor(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPBOR);
}

static void jit_emit_op_bxor(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPBXOR);
}

static void jit_emit_op_shl(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPSHL);
}

static void jit_emit_op_shr(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPSHR);
}

static void jit_emit_op_unm(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPUNM);
}

static void jit_emit_op_bnot(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPBNOT);
}

/* Stubs for other opcodes */
static void jit_emit_op_move(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_loadi(JitState *J, int a, int sbx) { (void)J; }
static void jit_emit_op_loadf(JitState *J, int a, int sbx) { (void)J; }
static void jit_emit_op_loadk(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_loadkx(JitState *J, int a) { (void)J; }
static void jit_emit_op_loadfalse(JitState *J, int a) { (void)J; }
static void jit_emit_op_lfalseskip(JitState *J, int a) { (void)J; }
static void jit_emit_op_loadtrue(JitState *J, int a) { (void)J; }
static void jit_emit_op_loadnil(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_getupval(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_setupval(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_gettabup(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_gettable(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_geti(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_getfield(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_settabup(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_settable(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_seti(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_setfield(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_newtable(JitState *J, int a, int vb, int vc, int k) { (void)J; }
static void jit_emit_op_self(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_addi(JitState *J, int a, int b, int sc, const Instruction *next) { (void)J; }
static void jit_emit_op_addk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_subk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_mulk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_modk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_powk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_divk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_idivk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_bandk(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_bork(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_bxork(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_shli(JitState *J, int a, int b, int sc, const Instruction *next) { (void)J; }
static void jit_emit_op_shri(JitState *J, int a, int b, int sc, const Instruction *next) { (void)J; }
static void jit_emit_op_spaceship(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_not(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_len(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_concat(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_close(JitState *J, int a) { (void)J; }
static void jit_emit_op_tbc(JitState *J, int a) { (void)J; }
static void jit_emit_op_jmp(JitState *J, int sj) { (void)J; }
static void jit_emit_op_eq(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_lt(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_le(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_eqk(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_eqi(JitState *J, int a, int sb, int k) { (void)J; }
static void jit_emit_op_lti(JitState *J, int a, int sb, int k) { (void)J; }
static void jit_emit_op_lei(JitState *J, int a, int sb, int k) { (void)J; }
static void jit_emit_op_gti(JitState *J, int a, int sb, int k) { (void)J; }
static void jit_emit_op_gei(JitState *J, int a, int sb, int k) { (void)J; }
static void jit_emit_op_test(JitState *J, int a, int k) { (void)J; }
static void jit_emit_op_testset(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_call(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_tailcall(JitState *J, int a, int b, int c, int k) { (void)J; }
static void jit_emit_op_return(JitState *J, int a, int b, int c, int k) { (void)J; }
static void jit_emit_op_forloop(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_forprep(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_tforprep(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_tforcall(JitState *J, int a, int c) { (void)J; }
static void jit_emit_op_tforloop(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_setlist(JitState *J, int a, int vb, int vc, int k) { (void)J; }
static void jit_emit_op_closure(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_vararg(JitState *J, int a, int b, int c, int k) { (void)J; }
static void jit_emit_op_getvarg(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_errnnil(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_varargprep(JitState *J, int a) { (void)J; }
static void jit_emit_op_is(JitState *J, int a, int b, int c, int k) { (void)J; }
static void jit_emit_op_testnil(JitState *J, int a, int b, int k) { (void)J; }
static void jit_emit_op_newclass(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_inherit(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_getsuper(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_setmethod(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_setstatic(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_newobj(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_getprop(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_setprop(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_instanceof(JitState *J, int a, int b, int c, int k) { (void)J; }
static void jit_emit_op_implement(JitState *J, int a, int b) { (void)J; }
static void jit_emit_op_setifaceflag(JitState *J, int a) { (void)J; }
static void jit_emit_op_addmethod(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_in(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_slice(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_nop(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_case(JitState *J, int a, int b, int c) { (void)J; }
static void jit_emit_op_newconcept(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_newnamespace(JitState *J, int a, int bx) { (void)J; }
static void jit_emit_op_linknamespace(JitState *J, int a, int b) { (void)J; }

#endif
