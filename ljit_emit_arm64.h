#ifndef ljit_emit_arm64_h
#define ljit_emit_arm64_h

#include "lprefix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lua.h"
#include "ldo.h"
#include "lvm.h"
#include "ljit.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#include "lopcodes.h"

/*
** ARM64 JIT Backend
** Directly included by ljit.c
*/

#define JIT_BUFFER_SIZE 4096

typedef struct JitFixup {
  int offset;
  int target_pc;
} JitFixup;

typedef struct JitState {
  unsigned char *code;
  size_t size;
  size_t capacity;
  Proto *p;
  const Instruction *next_pc;
  unsigned char **pc_map;
  JitFixup *fixups;
  size_t fixup_count;
  size_t fixup_capacity;
} JitState;

/* Register aliases (X0-X30) */
#define RA_X0 0
#define RA_X1 1
#define RA_X2 2
#define RA_X3 3
#define RA_X4 4
#define RA_X5 5
#define RA_X6 6
#define RA_X7 7
#define RA_X8 8
#define RA_X19 19
#define RA_X20 20
#define RA_FP 29
#define RA_LR 30
#define RA_SP 31

/* Helper functions */
static void emit_u32(JitState *J, unsigned int u) {
  if (J->size + 4 <= J->capacity) {
    memcpy(J->code + J->size, &u, 4);
    J->size += 4;
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
    J->p = NULL;
    J->next_pc = NULL;
    J->pc_map = NULL;
    J->fixups = NULL;
    J->fixup_count = 0;
    J->fixup_capacity = 0;
  }
  return J;
}

static void jit_free_state(JitState *J) {
  if (J) {
    if (J->pc_map) free(J->pc_map);
    if (J->fixups) free(J->fixups);
    free(J);
  }
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
** Instruction Emitters (ARM64)
*/

// RET
static void ASM_RET(JitState *J) {
  emit_u32(J, 0xD65F03C0);
}

// MOV Xd, Xn
static void emit_mov_r_r(JitState *J, int d, int n) {
  // ORR Xd, XZR, Xn: 0xAA0003E0 | (n << 16) | d
  emit_u32(J, 0xAA0003E0 | (n << 16) | d);
}

// MOV Xd, #imm (64-bit)
static void emit_mov_r_imm(JitState *J, int d, unsigned long long imm) {
  int first = 1;
  for (int i = 0; i < 4; i++) {
    unsigned int chunk = (imm >> (i * 16)) & 0xFFFF;
    if (chunk != 0 || (imm == 0 && i == 0)) {
      if (first) {
        // MOVZ Xd, #chunk, LSL #(i*16)
        // 0xD2800000 | (hw << 21) | (imm16 << 5) | d
        emit_u32(J, 0xD2800000 | (i << 21) | (chunk << 5) | d);
        first = 0;
      } else {
        // MOVK Xd, #chunk, LSL #(i*16)
        // 0xF2800000 | (hw << 21) | (imm16 << 5) | d
        emit_u32(J, 0xF2800000 | (i << 21) | (chunk << 5) | d);
      }
    }
  }
}

// LDR Xt, [Xn, #offset] (64-bit)
static void emit_ldr_r_mem(JitState *J, int t, int n, int offset) {
  // 0xF9400000 | ((offset/8) << 10) | (n << 5) | t
  if (offset < 0 || offset > 32760 || (offset % 8 != 0)) {
     emit_mov_r_imm(J, RA_X8, offset); // X8 = offset
     // LDR Xt, [Xn, X8]
     // 0xF8606800 | (8 << 16) | (n << 5) | t
     emit_u32(J, 0xF8606800 | (RA_X8 << 16) | (n << 5) | t);
  } else {
     emit_u32(J, 0xF9400000 | ((offset/8) << 10) | (n << 5) | t);
  }
}

// STR Xt, [Xn, #offset] (64-bit)
static void emit_str_r_mem(JitState *J, int t, int n, int offset) {
  // 0xF9000000 | ((offset/8) << 10) | (n << 5) | t
  if (offset < 0 || offset > 32760 || (offset % 8 != 0)) {
     emit_mov_r_imm(J, RA_X8, offset);
     // STR Xt, [Xn, X8]
     // 0xF8206800 | (8 << 16) | (n << 5) | t
     emit_u32(J, 0xF8206800 | (RA_X8 << 16) | (n << 5) | t);
  } else {
     emit_u32(J, 0xF9000000 | ((offset/8) << 10) | (n << 5) | t);
  }
}

// LDR Wt, [Xn, #offset] (32-bit)
static void emit_ldr_w_mem(JitState *J, int t, int n, int offset) {
  // 0xB9400000 | ((offset/4) << 10) | (n << 5) | t
  if (offset < 0 || offset > 16380 || (offset % 4 != 0)) {
     emit_mov_r_imm(J, RA_X8, offset);
     // LDR Wt, [Xn, X8]
     // 0xB8606800 | (8 << 16) | (n << 5) | t
     emit_u32(J, 0xB8606800 | (RA_X8 << 16) | (n << 5) | t);
  } else {
     emit_u32(J, 0xB9400000 | ((offset/4) << 10) | (n << 5) | t);
  }
}

// STR Wt, [Xn, #offset] (32-bit)
static void emit_str_w_mem(JitState *J, int t, int n, int offset) {
  // 0xB9000000 | ((offset/4) << 10) | (n << 5) | t
  if (offset < 0 || offset > 16380 || (offset % 4 != 0)) {
     emit_mov_r_imm(J, RA_X8, offset);
     // STR Wt, [Xn, X8]
     // 0xB8206800 | (8 << 16) | (n << 5) | t
     emit_u32(J, 0xB8206800 | (RA_X8 << 16) | (n << 5) | t);
  } else {
     emit_u32(J, 0xB9000000 | ((offset/4) << 10) | (n << 5) | t);
  }
}

// ADD Xd, Xn, #imm (12-bit)
static void emit_add_r_r_imm(JitState *J, int d, int n, int imm) {
  if (imm > 4095) {
    emit_mov_r_imm(J, RA_X8, imm);
    // ADD Xd, Xn, X8
    // 0x8B000000 | (8 << 16) | (n << 5) | d
    emit_u32(J, 0x8B000000 | (RA_X8 << 16) | (n << 5) | d);
  } else {
    emit_u32(J, 0x91000000 | (imm << 10) | (n << 5) | d);
  }
}

// ADD Xd, Xn, Xm
static void emit_add_r_r_r(JitState *J, int d, int n, int m) {
  // 0x8B000000 | (m << 16) | (n << 5) | d
  emit_u32(J, 0x8B000000 | (m << 16) | (n << 5) | d);
}

// SUB Xd, Xn, Xm (64-bit)
static void emit_sub_r_r_r(JitState *J, int d, int n, int m) {
  // 0xCB000000 | (m << 16) | (n << 5) | d
  emit_u32(J, 0xCB000000 | (m << 16) | (n << 5) | d);
}

// CMP Xn, Xm (64-bit) -> SUBS XZR, Xn, Xm
static void emit_cmp_r_r(JitState *J, int n, int m) {
  // 0xEB00001F | (m << 16) | (n << 5)
  emit_u32(J, 0xEB00001F | (m << 16) | (n << 5));
}

// CMP Xn, #0 (64-bit) -> SUBS XZR, Xn, XZR
static void emit_cmp_r_zero(JitState *J, int n) {
    // 0xEB1F001F | (n << 5)
    emit_u32(J, 0xEB1F001F | (n << 5));
}

// BLR Xn
static void emit_blr(JitState *J, int n) {
  // 0xD63F0000 | (n << 5)
  emit_u32(J, 0xD63F0000 | (n << 5));
}

// Get Reg Address: Dest = func.p + 16 + reg * 16
static void emit_get_reg_addr(JitState *J, int reg_a, int dest_reg) {
  // LDR dest, [X20, #0] (load func.p)
  emit_ldr_r_mem(J, dest_reg, RA_X20, 0);
  // ADD dest, dest, #(16 + reg_a * 16)
  emit_add_r_r_imm(J, dest_reg, dest_reg, 16 + reg_a * 16);
}

// CMP Wn, #imm (12-bit)
static void emit_cmp_w_imm(JitState *J, int n, int imm) {
  // SUBS WZR, Wn, #imm
  // 0x71000000 | (imm << 10) | (n << 5) | 31
  if (imm > 4095) {
     emit_mov_r_imm(J, RA_X8, imm);
     // CMP Wn, W8 -> SUBS WZR, Wn, W8
     // 0x6B000000 | (8 << 16) | (n << 5) | 31
     emit_u32(J, 0x6B00001F | (RA_X8 << 16) | (n << 5));
  } else {
     emit_u32(J, 0x7100001F | (imm << 10) | (n << 5));
  }
}

// B.cond label (offset is instruction count)
static void emit_b_cond(JitState *J, int cond, int offset) {
  // 0x54000000 | (offset << 5) | cond
  // Offset is 19 bits signed.
  emit_u32(J, 0x54000000 | ((offset & 0x7FFFF) << 5) | cond);
}

static void jit_add_fixup(JitState *J, int offset, int target_pc) {
  if (J->fixup_count == J->fixup_capacity) {
    size_t new_cap = J->fixup_capacity == 0 ? 16 : J->fixup_capacity * 2;
    J->fixups = (JitFixup *)realloc(J->fixups, new_cap * sizeof(JitFixup));
    J->fixup_capacity = new_cap;
  }
  J->fixups[J->fixup_count].offset = offset;
  J->fixups[J->fixup_count].target_pc = target_pc;
  J->fixup_count++;
}

static void jit_patch_fixups(JitState *J) {
  for (size_t i = 0; i < J->fixup_count; i++) {
    int offset = J->fixups[i].offset;
    int target_pc = J->fixups[i].target_pc;
    unsigned char *target_addr = J->pc_map[target_pc];
    unsigned char *instr_addr = J->code + offset;
    // B instruction is at instr_addr.
    // Offset is (target - instr) / 4.
    int rel = (int)(target_addr - instr_addr) / 4;
    // B opcode: 0x14000000 | (rel & 0x03FFFFFF).
    unsigned int inst = 0x14000000 | (rel & 0x03FFFFFF);
    memcpy(instr_addr, &inst, 4);
  }
}

/*
** JIT Logic Implementations
*/

static void jit_emit_prologue(JitState *J) {
  // STP X29, X30, [SP, #-16]!  (0xA9BF7BFD)
  emit_u32(J, 0xA9BF7BFD);
  // MOV X29, SP (0x910003FD)
  emit_u32(J, 0x910003FD);

  // Save L (X0) and ci (X1) to Callee-saved regs X19, X20
  // STP X19, X20, [SP, #-16]! (0xA9BF53F3)
  emit_u32(J, 0xA9BF53F3);

  // MOV X19, X0 (0xAA0003F3)
  emit_u32(J, 0xAA0003F3);
  // MOV X20, X1 (0xAA0103F4)
  emit_u32(J, 0xAA0103F4);
}

static void jit_emit_epilogue(JitState *J) {
  // Restore X19, X20
  // LDP X19, X20, [SP], #16 (0xA8C153F3)
  emit_u32(J, 0xA8C153F3);

  // Restore FP, LR
  // LDP X29, X30, [SP], #16 (0xA8C17BFD)
  emit_u32(J, 0xA8C17BFD);

  ASM_RET(J);
}

// Barrier
static void emit_barrier(JitState *J) {
  // Load next_pc into X0
  emit_mov_r_imm(J, RA_X0, (unsigned long long)(uintptr_t)(J->next_pc - 1));
  // Store X0 into ci->u.l.savedpc (offset 32 of ci which is X20)
  emit_str_r_mem(J, RA_X0, RA_X20, 32);
  // Return 0
  emit_mov_r_imm(J, RA_X0, 0);
  jit_emit_epilogue(J);
}

static void emit_update_savedpc(JitState *J) {
  emit_mov_r_imm(J, RA_X0, (unsigned long long)(uintptr_t)(J->next_pc - 1));
  emit_str_r_mem(J, RA_X0, RA_X20, 32);
}

static void jit_emit_op_move(JitState *J, int a, int b) {
  // Load &R[B]
  emit_get_reg_addr(J, b, RA_X2);
  // Load value (TValue is 16 bytes)
  emit_ldr_r_mem(J, RA_X3, RA_X2, 0); // low
  emit_ldr_r_mem(J, RA_X4, RA_X2, 8); // high
  // Load &R[A]
  emit_get_reg_addr(J, a, RA_X2);
  // Store value
  emit_str_r_mem(J, RA_X3, RA_X2, 0);
  emit_str_r_mem(J, RA_X4, RA_X2, 8);
}

static void jit_emit_op_loadi(JitState *J, int a, int sbx) {
  emit_get_reg_addr(J, a, RA_X2);
  emit_mov_r_imm(J, RA_X3, (long long)sbx);
  emit_str_r_mem(J, RA_X3, RA_X2, 0);
  emit_mov_r_imm(J, RA_X3, LUA_VNUMINT);
  emit_str_w_mem(J, RA_X3, RA_X2, 8);
}

static void jit_emit_op_loadf(JitState *J, int a, int sbx) {
  emit_get_reg_addr(J, a, RA_X2);
  double d = (double)sbx;
  unsigned long long u;
  memcpy(&u, &d, 8);
  emit_mov_r_imm(J, RA_X3, u);
  emit_str_r_mem(J, RA_X3, RA_X2, 0);
  emit_mov_r_imm(J, RA_X3, LUA_VNUMFLT);
  emit_str_w_mem(J, RA_X3, RA_X2, 8);
}

static void jit_emit_op_loadk(JitState *J, int a, int bx) {
  if (!J->p) { emit_barrier(J); return; }
  TValue *k = &J->p->k[bx];
  emit_mov_r_imm(J, RA_X2, (unsigned long long)(uintptr_t)k);

  emit_ldr_r_mem(J, RA_X3, RA_X2, 0);
  emit_ldr_r_mem(J, RA_X4, RA_X2, 8);

  emit_get_reg_addr(J, a, RA_X2);
  emit_str_r_mem(J, RA_X3, RA_X2, 0);
  emit_str_r_mem(J, RA_X4, RA_X2, 8);
}

static void jit_emit_op_loadkx(JitState *J, int a) { emit_barrier(J); }

static void jit_emit_op_loadfalse(JitState *J, int a) {
  emit_get_reg_addr(J, a, RA_X2);
  emit_mov_r_imm(J, RA_X3, LUA_VFALSE);
  emit_str_w_mem(J, RA_X3, RA_X2, 8);
}

static void jit_emit_op_lfalseskip(JitState *J, int a) { emit_barrier(J); }

static void jit_emit_op_loadtrue(JitState *J, int a) {
  emit_get_reg_addr(J, a, RA_X2);
  emit_mov_r_imm(J, RA_X3, LUA_VTRUE);
  emit_str_w_mem(J, RA_X3, RA_X2, 8);
}

static void jit_emit_op_loadnil(JitState *J, int a, int b) {
  emit_get_reg_addr(J, a, RA_X2);
  emit_mov_r_imm(J, RA_X3, LUA_VNIL);
  for (int i = 0; i <= b; i++) {
     emit_str_w_mem(J, RA_X3, RA_X2, 8 + i*16);
  }
}
static void jit_emit_op_getupval(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_setupval(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_gettabup(JitState *J, int a, int b, int c) { emit_barrier(J); }

static void jit_emit_op_gettable(JitState *J, int a, int b, int c) {
  // Update savedpc
  emit_update_savedpc(J);

  emit_mov_r_r(J, RA_X0, RA_X19); // L

  emit_get_reg_addr(J, b, RA_X1); // t
  emit_get_reg_addr(J, c, RA_X2); // key
  emit_get_reg_addr(J, a, RA_X3); // val

  emit_mov_r_imm(J, RA_X4, 0); // slot

  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_finishget);
  emit_blr(J, RA_X8);
}

static void jit_emit_op_geti(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_getfield(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_settabup(JitState *J, int a, int b, int c) { emit_barrier(J); }

static void jit_emit_op_settable(JitState *J, int a, int b, int c) {
  emit_update_savedpc(J);

  emit_mov_r_r(J, RA_X0, RA_X19); // L

  emit_get_reg_addr(J, a, RA_X1); // t
  emit_get_reg_addr(J, b, RA_X2); // key
  emit_get_reg_addr(J, c, RA_X3); // val

  emit_mov_r_imm(J, RA_X4, 0); // slot

  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_finishset);
  emit_blr(J, RA_X8);
}
static void jit_emit_op_seti(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_setfield(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_newtable(JitState *J, int a, int vb, int vc, int k) { emit_barrier(J); }
static void jit_emit_op_self(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_addi(JitState *J, int a, int b, int sc, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_addk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_subk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_mulk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_modk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_powk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_divk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_idivk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_bandk(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_bork(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_bxork(JitState *J, int a, int b, int c, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_shli(JitState *J, int a, int b, int sc, const Instruction *next) { emit_barrier(J); }
static void jit_emit_op_shri(JitState *J, int a, int b, int sc, const Instruction *next) { emit_barrier(J); }

static void emit_arith_common(JitState *J, int ra, int rb, int rc, const Instruction *next_pc, int op) {
  // Update savedpc
  emit_mov_r_imm(J, RA_X0, (unsigned long long)(uintptr_t)next_pc);
  emit_str_r_mem(J, RA_X0, RA_X20, 32);

  // X0 = L
  emit_mov_r_r(J, RA_X0, RA_X19);
  // X1 = op
  emit_mov_r_imm(J, RA_X1, op);

  // X2 = &R[rb]
  emit_get_reg_addr(J, rb, RA_X2);

  // X3 = &R[rc]
  emit_get_reg_addr(J, rc, RA_X3);

  // X4 = &R[ra]
  emit_get_reg_addr(J, ra, RA_X4);

  // Call luaO_arith
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaO_arith);
  emit_blr(J, RA_X8);
}

static void jit_emit_op_add(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPADD);
}
static void jit_emit_op_sub(JitState *J, int a, int b, int c, const Instruction *next) {
  emit_arith_common(J, a, b, c, next, LUA_OPSUB);
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

static void jit_emit_op_spaceship(JitState *J, int a, int b, int c) { emit_barrier(J); }

static void emit_unary_arith_common(JitState *J, int ra, int rb, const Instruction *next_pc, int op) {
  emit_mov_r_imm(J, RA_X0, (unsigned long long)(uintptr_t)next_pc);
  emit_str_r_mem(J, RA_X0, RA_X20, 32);

  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X1, op);

  emit_get_reg_addr(J, rb, RA_X2); // p1
  emit_mov_r_r(J, RA_X3, RA_X2);   // p2 = p1

  emit_get_reg_addr(J, ra, RA_X4); // res

  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaO_arith);
  emit_blr(J, RA_X8);
}

static void jit_emit_op_unm(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPUNM);
}
static void jit_emit_op_bnot(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPBNOT);
}
static void jit_emit_op_not(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_len(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_concat(JitState *J, int a, int b) { emit_barrier(J); }

static void jit_emit_op_jmp(JitState *J, int sj) {
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 + sj;

  if (sj < 0) {
    unsigned char *target = J->pc_map[target_pc_index];
    int rel = (int)(target - (J->code + J->size)) / 4;
    emit_u32(J, 0x14000000 | (rel & 0x03FFFFFF)); // B rel
  } else {
    jit_add_fixup(J, J->size, target_pc_index);
    emit_u32(J, 0x14000000); // B #0 (placeholder)
  }
}

static void emit_branch_on_k(JitState *J, int k, int sj) {
  emit_cmp_w_imm(J, RA_X0, k);

  // B.NE Mismatch (Skip JMP)
  int branch_pos = J->size;
  emit_b_cond(J, 1, 0); // NE = 1

  // Match: Execute JMP
  int op_jmp_index = (int)(J->next_pc - J->p->code);
  int target_pc_index = op_jmp_index + 1 + sj;

  if (sj < 0) { // Backward
     unsigned char *target = J->pc_map[target_pc_index];
     int rel = (int)(target - (J->code + J->size)) / 4;
     emit_u32(J, 0x14000000 | (rel & 0x03FFFFFF)); // B rel
  } else { // Forward
     jit_add_fixup(J, J->size, target_pc_index);
     emit_u32(J, 0x14000000); // B #0
  }

  // Mismatch label
  int skip_pos = J->size;
  int offset = (skip_pos - branch_pos) / 4;
  unsigned int *code = (unsigned int *)(J->code + branch_pos);
  *code |= ((offset & 0x7FFFF) << 5);
}

static void jit_emit_op_eq(JitState *J, int a, int b, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, b, RA_X2); // Arg3 = b
  emit_get_reg_addr(J, a, RA_X1); // Arg2 = a
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_equalobj);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lt(JitState *J, int a, int b, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, b, RA_X2); // Arg3 = b
  emit_get_reg_addr(J, a, RA_X1); // Arg2 = a
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_lessthan);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_le(JitState *J, int a, int b, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, b, RA_X2); // Arg3 = b
  emit_get_reg_addr(J, a, RA_X1); // Arg2 = a
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_lessequal);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_eqk(JitState *J, int a, int b, int k, int sj) {
  if (!J->p) { emit_barrier(J); return; }
  emit_update_savedpc(J);
  TValue *rb = &J->p->k[b];
  emit_mov_r_imm(J, RA_X2, (unsigned long long)(uintptr_t)rb); // Arg3 = K[b]
  emit_get_reg_addr(J, a, RA_X1); // Arg2 = a
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaV_equalobj);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_eqi(JitState *J, int a, int sb, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, a, RA_X1); // Arg2 = a
  emit_mov_r_imm(J, RA_X2, sb);   // Arg3 = sb
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_eqi);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lti(JitState *J, int a, int sb, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, a, RA_X1);
  emit_mov_r_imm(J, RA_X2, sb);
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_lti);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lei(JitState *J, int a, int sb, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, a, RA_X1);
  emit_mov_r_imm(J, RA_X2, sb);
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_lei);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_gti(JitState *J, int a, int sb, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, a, RA_X1);
  emit_mov_r_imm(J, RA_X2, sb);
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_gti);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_gei(JitState *J, int a, int sb, int k, int sj) {
  emit_update_savedpc(J);
  emit_get_reg_addr(J, a, RA_X1);
  emit_mov_r_imm(J, RA_X2, sb);
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_gei);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_test(JitState *J, int a, int k, int sj) {
  emit_get_reg_addr(J, a, RA_X1); // Arg 1 is TValue*
  // emit_mov_r_imm(J, RA_X0, ...); // X0 is unused by helper
  // Helper is luaJ_istrue(TValue*) -> int
  // Wait, signature is int luaJ_istrue(TValue*).
  // So arg in X0.
  emit_get_reg_addr(J, a, RA_X0);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_istrue);
  emit_blr(J, RA_X8);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_testset(JitState *J, int a, int b, int k, int sj) {
  emit_barrier(J);
}

static void jit_emit_op_call(JitState *J, int a, int b, int c) {
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_r(J, RA_X1, RA_X20);
  emit_mov_r_imm(J, RA_X2, a);
  emit_mov_r_imm(J, RA_X3, b);
  emit_mov_r_imm(J, RA_X4, c);
  emit_mov_r_imm(J, RA_X5, (unsigned long long)(uintptr_t)J->next_pc);

  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_call_helper);
  emit_blr(J, RA_X8);
}

static void jit_emit_op_tailcall(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }
static void jit_emit_op_return(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }

static void jit_emit_op_forprep(JitState *J, int a, int bx) {
  // Check tags (int)
  emit_get_reg_addr(J, a, RA_X2);

  // Check R[A] tag (offset 8)
  emit_ldr_w_mem(J, RA_X3, RA_X2, 8);
  emit_cmp_w_imm(J, RA_X3, LUA_VNUMINT);
  emit_b_cond(J, 1, 0); int p1 = J->size - 4; // 1 = NE

  // Check R[A+1] tag (offset 24)
  emit_ldr_w_mem(J, RA_X3, RA_X2, 16 + 8);
  emit_cmp_w_imm(J, RA_X3, LUA_VNUMINT);
  emit_b_cond(J, 1, 0); int p2 = J->size - 4;

  // Check R[A+2] tag (offset 40)
  emit_ldr_w_mem(J, RA_X3, RA_X2, 32 + 8);
  emit_cmp_w_imm(J, RA_X3, LUA_VNUMINT);
  emit_b_cond(J, 1, 0); int p3 = J->size - 4;

  // R[A].i -= R[A+2].i
  emit_ldr_r_mem(J, RA_X3, RA_X2, 0); // R[A]
  emit_ldr_r_mem(J, RA_X4, RA_X2, 32); // R[A+2]
  emit_sub_r_r_r(J, RA_X3, RA_X3, RA_X4);
  emit_str_r_mem(J, RA_X3, RA_X2, 0);

  // Jump to Target: pc += Bx + 1
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 + bx + 1;
  jit_add_fixup(J, J->size, target_pc_index);
  emit_u32(J, 0x14000000); // B #0

  // Barrier (Mismatch)
  int barrier_pos = J->size;
  // Fix conditional jumps (offset is instruction count)
  unsigned int *code = (unsigned int *)(J->code + p1);
  *code |= (((barrier_pos - p1)/4) & 0x7FFFF) << 5;
  code = (unsigned int *)(J->code + p2);
  *code |= (((barrier_pos - p2)/4) & 0x7FFFF) << 5;
  code = (unsigned int *)(J->code + p3);
  *code |= (((barrier_pos - p3)/4) & 0x7FFFF) << 5;

  emit_barrier(J);
}

static void jit_emit_op_forloop(JitState *J, int a, int bx) {
  emit_get_reg_addr(J, a, RA_X2); // Base

  // ADD R[A], R[A+2]
  emit_ldr_r_mem(J, RA_X3, RA_X2, 0); // idx
  emit_ldr_r_mem(J, RA_X4, RA_X2, 32); // step
  emit_add_r_r_r(J, RA_X3, RA_X3, RA_X4);
  emit_str_r_mem(J, RA_X3, RA_X2, 0); // Update idx

  // Load limit
  emit_ldr_r_mem(J, RA_X5, RA_X2, 16); // limit

  // Check step sign (X4)
  emit_cmp_r_zero(J, RA_X4);

  // Branch Negative
  emit_b_cond(J, 11, 0); int p_neg = J->size - 4; // 11 = LT

  // Positive: CMP idx(X3), limit(X5).
  emit_cmp_r_r(J, RA_X3, RA_X5);
  // B.LE Match
  emit_b_cond(J, 13, 0); int p_match_pos = J->size - 4; // 13 = LE

  // B Exit
  emit_u32(J, 0x14000000); int p_exit = J->size - 4;

  // Negative:
  int neg_pos = J->size;
  // Fix LT branch
  unsigned int *c = (unsigned int *)(J->code + p_neg);
  *c |= (((neg_pos - p_neg)/4) & 0x7FFFF) << 5;

  // CMP idx, limit
  emit_cmp_r_r(J, RA_X3, RA_X5);
  // B.GE Match
  emit_b_cond(J, 10, 0); int p_match_neg = J->size - 4; // 10 = GE

  // Exit
  int exit_pos = J->size;
  // Fix B Exit
  c = (unsigned int *)(J->code + p_exit);
  *c |= ((exit_pos - p_exit)/4 & 0x03FFFFFF);

  // B OverMatch
  emit_u32(J, 0x14000000); int p_over = J->size - 4;

  // Match:
  int match_pos = J->size;
  // Fix LE/GE branches
  c = (unsigned int *)(J->code + p_match_pos);
  *c |= (((match_pos - p_match_pos)/4) & 0x7FFFF) << 5;
  c = (unsigned int *)(J->code + p_match_neg);
  *c |= (((match_pos - p_match_neg)/4) & 0x7FFFF) << 5;

  // Update R[A+3] = R[A] (X3)
  emit_str_r_mem(J, RA_X3, RA_X2, 48);
  // Update tag
  emit_ldr_r_mem(J, RA_X6, RA_X2, 8);
  emit_str_r_mem(J, RA_X6, RA_X2, 56);

  // Jump Target (Backward): pc -= Bx
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 - bx;
  unsigned char *target = J->pc_map[target_pc_index];
  int rel = (int)(target - (J->code + J->size)) / 4;
  emit_u32(J, 0x14000000 | (rel & 0x03FFFFFF));

  // OverMatch
  int over_pos = J->size;
  c = (unsigned int *)(J->code + p_over);
  *c |= ((over_pos - p_over)/4 & 0x03FFFFFF);
}

static void jit_emit_op_return0(JitState *J) {
  // luaJ_prep_return0
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_r(J, RA_X1, RA_X20);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_prep_return0);
  emit_blr(J, RA_X8);

  // luaD_poscall(L, ci, 0)
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_r(J, RA_X1, RA_X20);
  emit_mov_r_imm(J, RA_X2, 0);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaD_poscall);
  emit_blr(J, RA_X8);

  // Return 1
  emit_mov_r_imm(J, RA_X0, 1);
  jit_emit_epilogue(J);
}

static void jit_emit_op_return1(JitState *J, int ra) {
  // luaJ_prep_return1
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_r(J, RA_X1, RA_X20);
  emit_mov_r_imm(J, RA_X2, ra);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaJ_prep_return1);
  emit_blr(J, RA_X8);

  // luaD_poscall
  emit_mov_r_r(J, RA_X0, RA_X19);
  emit_mov_r_r(J, RA_X1, RA_X20);
  emit_mov_r_imm(J, RA_X2, 1);
  emit_mov_r_imm(J, RA_X8, (unsigned long long)(uintptr_t)&luaD_poscall);
  emit_blr(J, RA_X8);

  // Return 1
  emit_mov_r_imm(J, RA_X0, 1);
  jit_emit_epilogue(J);
}
static void jit_emit_op_tforprep(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_tforcall(JitState *J, int a, int c) { emit_barrier(J); }
static void jit_emit_op_tforloop(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_setlist(JitState *J, int a, int vb, int vc, int k) { emit_barrier(J); }
static void jit_emit_op_closure(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_vararg(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }
static void jit_emit_op_getvarg(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_errnnil(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_varargprep(JitState *J, int a) { emit_barrier(J); }
static void jit_emit_op_is(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }
static void jit_emit_op_testnil(JitState *J, int a, int b, int k) { emit_barrier(J); }
static void jit_emit_op_newclass(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_inherit(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_getsuper(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_setmethod(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_setstatic(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_newobj(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_getprop(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_setprop(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_instanceof(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }
static void jit_emit_op_implement(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_setifaceflag(JitState *J, int a) { emit_barrier(J); }
static void jit_emit_op_addmethod(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_in(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_slice(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_nop(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_case(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_newconcept(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_newnamespace(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_linknamespace(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_close(JitState *J, int a) { emit_barrier(J); }
static void jit_emit_op_tbc(JitState *J, int a) { emit_barrier(J); }

#endif
