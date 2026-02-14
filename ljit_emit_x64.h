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
#include "lvm.h"
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
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4; // REX.R
  if (dst >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0x89);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// ADD dst, src (64-bit)
static void ASM_ADD_RR(JitState *J, int dst, int src) {
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4;
  if (dst >= 8) rex |= 1;
  emit_byte(J, rex);
  emit_byte(J, 0x01);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// SUB dst, src (64-bit)
static void ASM_SUB_RR(JitState *J, int dst, int src) {
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4;
  if (dst >= 8) rex |= 1;
  emit_byte(J, rex);
  emit_byte(J, 0x29);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// MOV reg, imm64
static void ASM_MOV_R_IMM(JitState *J, int reg, unsigned long long imm) {
  unsigned char rex = 0x48;
  if (reg >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0xB8 + (reg & 7));
  emit_u64(J, imm);
}

// MOV reg, imm32
static void ASM_MOV_R_IMM32(JitState *J, int reg, int imm) {
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
  unsigned char rex = 0x48; // REX.W
  if (src >= 8) rex |= 4; // REX.R
  if (base >= 8) rex |= 1;

  emit_byte(J, rex);
  emit_byte(J, 0x89);

  if ((base & 7) == 4) { // RSP or R12
     emit_byte(J, 0x84 | ((src & 7) << 3));
     emit_byte(J, 0x24);
     emit_u32(J, offset);
  } else {
     emit_byte(J, 0x80 | ((src & 7) << 3) | (base & 7));
     emit_u32(J, offset);
  }
}

// MOV reg, [base + offset]
static void ASM_MOV_R_MEM(JitState *J, int dst, int base, int offset) {
  unsigned char rex = 0x48; // REX.W
  if (dst >= 8) rex |= 4; // REX.R
  if (base >= 8) rex |= 1; // REX.B

  emit_byte(J, rex);
  emit_byte(J, 0x8B);

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
  emit_byte(J, 0x81);
  emit_byte(J, 0xC0 + (reg & 7));
  emit_u32(J, imm);
}

// MOV [dst_reg + offset], src_reg
static void ASM_MOV_MEM_OFF_R(JitState *J, int dst_reg, int offset, int src_reg) {
  // Same as ASM_MOV_MEM_R but generalized name
  ASM_MOV_MEM_R(J, dst_reg, offset, src_reg);
}

// MOV dst_reg, [src_reg + offset]
static void ASM_MOV_R_MEM_OFF(JitState *J, int dst_reg, int src_reg, int offset) {
  ASM_MOV_R_MEM(J, dst_reg, src_reg, offset);
}

// CMP reg, imm32
static void ASM_CMP_R_IMM32(JitState *J, int reg, int imm) {
  unsigned char rex = 0x48;
  if (reg >= 8) rex |= 1; // REX.B
  emit_byte(J, rex);
  emit_byte(J, 0x81); // CMP r/m64, imm32
  emit_byte(J, 0xF8 + (reg & 7)); // ModRM: 11 111 reg
  emit_u32(J, imm);
}

// CMP dst, src (64-bit)
static void ASM_CMP_RR(JitState *J, int dst, int src) {
  unsigned char rex = 0x48;
  if (src >= 8) rex |= 4;
  if (dst >= 8) rex |= 1;
  emit_byte(J, rex);
  emit_byte(J, 0x39);
  emit_byte(J, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

// Short Jump if Not Equal (JNE rel8)
static void ASM_JNE(JitState *J, int offset) {
  emit_byte(J, 0x75);
  emit_byte(J, (unsigned char)offset);
}

// Short Jump if Equal (JE rel8)
static void ASM_JE(JitState *J, int offset) {
  emit_byte(J, 0x74);
  emit_byte(J, (unsigned char)offset);
}

// Short Unconditional Jump (JMP rel8)
static void ASM_JMP(JitState *J, int offset) {
  emit_byte(J, 0xEB);
  emit_byte(J, (unsigned char)offset);
}

// Jump near relative (JMP rel32)
static void ASM_JMP_REL32(JitState *J, int offset) {
  emit_byte(J, 0xE9);
  emit_u32(J, offset);
}

// Jump if Less or Equal (JLE rel32) - 0F 8E cd
static void ASM_JLE_REL32(JitState *J, int offset) {
  emit_byte(J, 0x0F);
  emit_byte(J, 0x8E);
  emit_u32(J, offset);
}

// Jump if Greater or Equal (JGE rel32) - 0F 8D cd
static void ASM_JGE_REL32(JitState *J, int offset) {
  emit_byte(J, 0x0F);
  emit_byte(J, 0x8D);
  emit_u32(J, offset);
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
    // Calculate relative offset: target - (instr + 4)
    // instr points to the start of the placeholder (offset in emit_op_jmp/forprep)
    // Actually, JMP rel32 is 5 bytes. The offset passed to add_fixup should be where the 4-byte displacement starts?
    // Let's convention: offset is the start of the instruction + 1 (where the displacement is).
    // So instr_addr points to the displacement.
    // The relative jump is calculated from the *end* of the instruction.
    // End of instruction is instr_addr + 4.
    // So rel = target - (instr_addr + 4).
    int rel = (int)(target_addr - (instr_addr + 4));
    memcpy(instr_addr, &rel, 4);
  }
}

/*
** JIT Logic Implementations
*/

static void jit_emit_prologue(JitState *J) {
  ASM_PUSH_R(J, RA_RBP);
  ASM_MOV_RR(J, RA_RBP, RA_RSP);
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

// Helper: Barrier (Update savedpc and return 0)
static void emit_barrier(JitState *J) {
  // MOV RAX, next_pc - 1 (current instruction)
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)(J->next_pc - 1));
  // MOV [R12 + 32], RAX (ci->u.l.savedpc)
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);
  // MOV RAX, 0
  ASM_XOR_RR(J, RA_RAX, RA_RAX);
  jit_emit_epilogue(J);
}

// Helper: Get address of register A
// R[A] is at ci->func.p + 1 + A
// ci->func.p is at [R12]
// We compute &R[A] in DEST_REG
static void emit_get_reg_addr(JitState *J, int reg_a, int dest_reg) {
  ASM_MOV_R_MEM(J, dest_reg, RA_R12, 0); // dest = func.p
  ASM_ADD_R_IMM32(J, dest_reg, 16 + reg_a * 16); // dest += 16 + A*16
}

static void jit_emit_op_move(JitState *J, int a, int b) {
  // R[A] = R[B]
  // Load &R[B]
  emit_get_reg_addr(J, b, RA_RDX);
  // Load value (2 x 64-bit load)
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 0); // low
  ASM_MOV_R_MEM_OFF(J, RA_R8, RA_RDX, 8);  // high
  // Load &R[A]
  emit_get_reg_addr(J, a, RA_RDX);
  // Store value
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_R8);
}

static void jit_emit_op_loadi(JitState *J, int a, int sbx) {
  // R[A] = (integer)sbx
  emit_get_reg_addr(J, a, RA_RDX);
  // val.i = sbx
  ASM_MOV_R_IMM(J, RA_RCX, (long long)sbx);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX);
  // tt_ = LUA_VNUMINT (tag)
  // LUA_VNUMINT is (LUA_TNUMBER | (0 << 4)) which is LUA_TNUMBER (3).
  // But wait, makevariant(LUA_TNUMBER, 0)
  // LUA_TNUMBER is 3.
  // We need to write the tag byte. TValue layout: value (8), tag (1), padding.
  // Actually on 64-bit, TValue is struct { Value value_; lu_byte tt_; }
  // Value is union.
  // tt_ is at offset 8.
  ASM_MOV_R_IMM32(J, RA_RCX, LUA_VNUMINT);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_RCX); // Writes 4 bytes (ok, tt_ is byte but padding follows)
}

static void jit_emit_op_loadf(JitState *J, int a, int sbx) {
  // R[A] = (float)sbx
  emit_get_reg_addr(J, a, RA_RDX);
  // We need to load float value.
  // sbx is int. Cast to double.
  double d = (double)sbx;
  unsigned long long u;
  memcpy(&u, &d, 8);
  ASM_MOV_R_IMM(J, RA_RCX, u);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX);
  // Tag
  ASM_MOV_R_IMM32(J, RA_RCX, LUA_VNUMFLT);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_RCX);
}

static void jit_emit_op_loadk(JitState *J, int a, int bx) {
  // R[A] = K[bx]
  // K is J->p->k
  if (!J->p) { emit_barrier(J); return; } // Safety
  TValue *k = &J->p->k[bx];
  // Bake address of k
  ASM_MOV_R_IMM(J, RA_RSI, (unsigned long long)(uintptr_t)k);

  // Load K value
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RSI, 0);
  ASM_MOV_R_MEM_OFF(J, RA_R8, RA_RSI, 8);

  // Store to R[A]
  emit_get_reg_addr(J, a, RA_RDX);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_R8);
}

static void jit_emit_op_loadkx(JitState *J, int a) {
  emit_barrier(J); // Too complex (extra arg)
}

static void jit_emit_op_loadnil(JitState *J, int a, int b) {
  // R[A]...R[A+B] = nil
  // Just do loop in C? No, unroll or loop in ASM.
  // For simplicity, unroll for small B, or just do one.
  // Implementation: Barrier for now? No, simple to loop.
  // But let's just do A to A+B.
  // b is count.
  emit_get_reg_addr(J, a, RA_RDX); // R[A] address
  ASM_MOV_R_IMM32(J, RA_RCX, LUA_VNIL); // Tag

  for (int i = 0; i <= b; i++) {
     // Store tag to [RDX + 8 + i*16]
     ASM_MOV_MEM_OFF_R(J, RA_RDX, 8 + i*16, RA_RCX);
     // Value doesn't matter for nil, but good to zero it? Not strictly required.
  }
}

static void jit_emit_op_loadfalse(JitState *J, int a) {
  emit_get_reg_addr(J, a, RA_RDX);
  ASM_MOV_R_IMM32(J, RA_RCX, LUA_VFALSE);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_RCX);
}

static void jit_emit_op_loadtrue(JitState *J, int a) {
  emit_get_reg_addr(J, a, RA_RDX);
  ASM_MOV_R_IMM32(J, RA_RCX, LUA_VTRUE);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 8, RA_RCX);
}

static void jit_emit_op_lfalseskip(JitState *J, int a) { emit_barrier(J); }

static void jit_emit_op_gettable(JitState *J, int a, int b, int c) {
  // luaV_finishget(L, R[B], R[C], R[A], NULL)
  // L: RDI (RBX)
  // t: RSI (&R[B])
  // key: RDX (&R[C])
  // val: RCX (&R[A])
  // slot: R8 (0)

  // Update savedpc first because luaV_finishget can error
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)J->next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);

  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L

  emit_get_reg_addr(J, b, RA_RSI); // t
  emit_get_reg_addr(J, c, RA_RDX); // key
  emit_get_reg_addr(J, a, RA_RCX); // val

  ASM_XOR_RR(J, RA_R8, RA_R8); // slot = NULL

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_finishget);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_settable(JitState *J, int a, int b, int c) {
  // luaV_finishset(L, R[A], R[B], R[C], NULL)
  // L: RDI
  // t: RSI (&R[A])
  // key: RDX (&R[B])
  // val: RCX (&R[C])
  // slot: R8 (NULL)

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)J->next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);

  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  emit_get_reg_addr(J, a, RA_RSI); // t
  emit_get_reg_addr(J, b, RA_RDX); // key
  emit_get_reg_addr(J, c, RA_RCX); // val
  ASM_XOR_RR(J, RA_R8, RA_R8); // slot = NULL

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_finishset);
  ASM_CALL_R(J, RA_RAX);
}

// Barriers for Control Flow
static void jit_emit_op_jmp(JitState *J, int sj) {
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 + sj;

  if (sj < 0) {
    // Backward jump
    unsigned char *target = J->pc_map[target_pc_index];
    emit_byte(J, 0xE9);
    int rel = (int)(target - (J->code + J->size + 4));
    emit_u32(J, rel);
  } else {
    // Forward jump (placeholder)
    emit_byte(J, 0xE9);
    jit_add_fixup(J, J->size, target_pc_index);
    emit_u32(J, 0);
  }
}

static void emit_branch_on_k(JitState *J, int k, int sj) {
  ASM_CMP_R_IMM32(J, RA_RAX, k);

  // JNE Skip
  ASM_JNE(J, 0);
  int patch_pos = J->size - 1;

  // Execute JMP path
  const Instruction *target_jmp = J->next_pc + sj + 1;
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)target_jmp);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);
  ASM_XOR_RR(J, RA_RAX, RA_RAX);
  jit_emit_epilogue(J);

  // Skip JMP path
  int skip_pos = J->size;
  J->code[patch_pos] = (unsigned char)(skip_pos - (patch_pos + 1));

  const Instruction *target_skip = J->next_pc + 1;
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)target_skip);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);
  ASM_XOR_RR(J, RA_RAX, RA_RAX);
  jit_emit_epilogue(J);
}

static void jit_emit_op_eq(JitState *J, int a, int b, int k, int sj) {
  emit_get_reg_addr(J, b, RA_RDX);
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_equalobj);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lt(JitState *J, int a, int b, int k, int sj) {
  emit_get_reg_addr(J, b, RA_RDX);
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_lessthan);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_le(JitState *J, int a, int b, int k, int sj) {
  emit_get_reg_addr(J, b, RA_RDX);
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_lessequal);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_eqk(JitState *J, int a, int b, int k, int sj) {
  if (!J->p) { emit_barrier(J); return; }
  TValue *rb = &J->p->k[b];
  ASM_MOV_R_IMM(J, RA_RDX, (unsigned long long)(uintptr_t)rb);
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaV_equalobj);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_eqi(JitState *J, int a, int sb, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM32(J, RA_RDX, sb);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_eqi);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lti(JitState *J, int a, int sb, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM32(J, RA_RDX, sb);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_lti);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_lei(JitState *J, int a, int sb, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM32(J, RA_RDX, sb);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_lei);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_gti(JitState *J, int a, int sb, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM32(J, RA_RDX, sb);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_gti);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_gei(JitState *J, int a, int sb, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM32(J, RA_RDX, sb);
  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_gei);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_test(JitState *J, int a, int k, int sj) {
  emit_get_reg_addr(J, a, RA_RSI);
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_istrue);
  ASM_CALL_R(J, RA_RAX);
  emit_branch_on_k(J, k, sj);
}

static void jit_emit_op_testset(JitState *J, int a, int b, int k, int sj) {
  emit_barrier(J);
}

static void jit_emit_op_call(JitState *J, int a, int b, int c) {
  // Helper args: (L, ci, ra_idx, b, c, next_pc)
  // Register mapping for x64 call:
  // 1: RDI = L (from RBX)
  // 2: RSI = ci (from R12)
  // 3: RDX = ra_idx
  // 4: RCX = b
  // 5: R8 = c
  // 6: R9 = next_pc (J->next_pc)

  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_MOV_R_IMM32(J, RA_RDX, a);
  ASM_MOV_R_IMM32(J, RA_RCX, b);
  ASM_MOV_R_IMM32(J, RA_R8, c);
  ASM_MOV_R_IMM(J, RA_R9, (unsigned long long)(uintptr_t)J->next_pc);

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_call_helper);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_tailcall(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }
static void jit_emit_op_return(JitState *J, int a, int b, int c, int k) { emit_barrier(J); }

static void jit_emit_op_forprep(JitState *J, int a, int bx) {
  // Check tags: R[A], R[A+1], R[A+2] must be integers
  emit_get_reg_addr(J, a, RA_RDX);

  // Check R[A].tt_
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 8);
  ASM_CMP_R_IMM32(J, RA_RCX, LUA_VNUMINT);
  ASM_JNE(J, 0); int p1 = J->size - 1;

  // Check R[A+1].tt_
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 16 + 8);
  ASM_CMP_R_IMM32(J, RA_RCX, LUA_VNUMINT);
  ASM_JNE(J, 0); int p2 = J->size - 1;

  // Check R[A+2].tt_
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 32 + 8);
  ASM_CMP_R_IMM32(J, RA_RCX, LUA_VNUMINT);
  ASM_JNE(J, 0); int p3 = J->size - 1;

  // R[A].i -= R[A+2].i
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 0);
  ASM_MOV_R_MEM_OFF(J, RA_R8, RA_RDX, 32);
  ASM_SUB_RR(J, RA_RCX, RA_R8);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX);

  // Jump to Target: pc += Bx + 1
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 + bx;
  emit_byte(J, 0xE9);
  jit_add_fixup(J, J->size, target_pc_index);
  emit_u32(J, 0);

  // Mismatch label (barrier)
  int barrier_pos = J->size;
  J->code[p1] = (unsigned char)(barrier_pos - (p1 + 1));
  J->code[p2] = (unsigned char)(barrier_pos - (p2 + 1));
  J->code[p3] = (unsigned char)(barrier_pos - (p3 + 1));

  emit_barrier(J);
}

static void jit_emit_op_forloop(JitState *J, int a, int bx) {
  // fprintf(stderr, "Emit FORLOOP A=%d Bx=%d\n", a, bx);
  emit_get_reg_addr(J, a, RA_RDX);

  // ADD R[A], R[A+2]
  ASM_MOV_R_MEM_OFF(J, RA_RCX, RA_RDX, 0); // idx
  ASM_MOV_R_MEM_OFF(J, RA_R8, RA_RDX, 32); // step
  ASM_ADD_RR(J, RA_RCX, RA_R8);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 0, RA_RCX); // Update R[A]

  // Compare R[A] with R[A+1] (limit)
  ASM_MOV_R_MEM_OFF(J, RA_R9, RA_RDX, 16); // limit

  // Check step sign
  ASM_CMP_R_IMM32(J, RA_R8, 0);

  // JL Negative
  emit_byte(J, 0x7C);
  emit_byte(J, 0); int p_neg = J->size - 1;

  // Positive step: CMP idx, limit. If idx <= limit (JLE), Match.
  ASM_CMP_RR(J, RA_RCX, RA_R9);
  emit_byte(J, 0x7E); // JLE rel8
  emit_byte(J, 0); int p_match_pos = J->size - 1;

  // JMP Exit (if idx > limit)
  emit_byte(J, 0xEB);
  emit_byte(J, 0); int p_exit_pos = J->size - 1;

  // Negative step
  int neg_pos = J->size;
  J->code[p_neg] = (unsigned char)(neg_pos - (p_neg + 1));

  // CMP idx, limit. If idx >= limit (JGE), Match.
  ASM_CMP_RR(J, RA_RCX, RA_R9);
  emit_byte(J, 0x7D); // JGE rel8
  emit_byte(J, 0); int p_match_neg = J->size - 1;

  // Exit (Fallthrough for negative mismatch)
  int exit_pos = J->size;
  J->code[p_exit_pos] = (unsigned char)(exit_pos - (p_exit_pos + 1));

  // Done with loop (fallthrough to next instruction)
  // We need to skip the "Match" block which follows.
  // JMP OverMatch
  emit_byte(J, 0xEB);
  emit_byte(J, 0); int p_over_match = J->size - 1;

  // Match Block
  int match_pos = J->size;
  J->code[p_match_pos] = (unsigned char)(match_pos - (p_match_pos + 1));
  J->code[p_match_neg] = (unsigned char)(match_pos - (p_match_neg + 1));

  // Update R[A+3] = R[A] (idx is in RCX)
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 48, RA_RCX);
  // Copy tag from R[A] (RDX+8) to R[A+3] (RDX+56)
  ASM_MOV_R_MEM_OFF(J, RA_R8, RA_RDX, 8);
  ASM_MOV_MEM_OFF_R(J, RA_RDX, 56, RA_R8);

  // Jump to Loop Target: pc -= Bx
  int current_pc_index = (int)(J->next_pc - J->p->code - 1);
  int target_pc_index = current_pc_index + 1 - bx;
  unsigned char *target = J->pc_map[target_pc_index];

  // JMP rel32
  emit_byte(J, 0xE9);
  int rel = (int)(target - (J->code + J->size + 4));
  emit_u32(J, rel);

  // OverMatch
  int over_match_pos = J->size;
  J->code[p_over_match] = (unsigned char)(over_match_pos - (p_over_match + 1));
}

static void jit_emit_op_tforprep(JitState *J, int a, int bx) { emit_barrier(J); }
static void jit_emit_op_tforcall(JitState *J, int a, int c) { emit_barrier(J); }
static void jit_emit_op_tforloop(JitState *J, int a, int bx) { emit_barrier(J); }

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
  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_MOV_R_IMM32(J, RA_RDX, ra);

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaJ_prep_return1);
  ASM_CALL_R(J, RA_RAX);

  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_RR(J, RA_RSI, RA_R12); // ci
  ASM_MOV_R_IMM32(J, RA_RDX, 1);

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaD_poscall);
  ASM_CALL_R(J, RA_RAX);

  ASM_MOV_R_IMM32(J, RA_RAX, 1);
  jit_emit_epilogue(J);
}

static void emit_arith_common(JitState *J, int ra, int rb, int rc, const Instruction *next_pc, int op) {
  // Update savedpc: mov [r12 + 32], next_pc
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);

  ASM_MOV_RR(J, RA_RDI, RA_RBX); // L
  ASM_MOV_R_IMM32(J, RA_RSI, op); // op

  ASM_MOV_R_MEM(J, RA_RDX, RA_R12, 0);
  ASM_ADD_R_IMM32(J, RA_RDX, 16 + rb * 16); // &R[rb]

  ASM_MOV_R_MEM(J, RA_RCX, RA_R12, 0);
  ASM_ADD_R_IMM32(J, RA_RCX, 16 + rc * 16); // &R[rc]

  ASM_MOV_R_MEM(J, RA_R8, RA_R12, 0);
  ASM_ADD_R_IMM32(J, RA_R8, 16 + ra * 16); // &R[ra]

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaO_arith);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_add(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) {
  emit_arith_common(J, ra, rb, rc, next_pc, LUA_OPADD);
}
static void jit_emit_op_sub(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) {
  emit_arith_common(J, ra, rb, rc, next_pc, LUA_OPSUB);
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

static void emit_unary_arith_common(JitState *J, int ra, int rb, const Instruction *next_pc, int op) {
  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)next_pc);
  ASM_MOV_MEM_R(J, RA_R12, 32, RA_RAX);

  ASM_MOV_RR(J, RA_RDI, RA_RBX);
  ASM_MOV_R_IMM32(J, RA_RSI, op);

  ASM_MOV_R_MEM(J, RA_RDX, RA_R12, 0);
  ASM_ADD_R_IMM32(J, RA_RDX, 16 + rb * 16); // rb

  ASM_MOV_RR(J, RA_RCX, RA_RDX); // rc = rb

  ASM_MOV_R_MEM(J, RA_R8, RA_R12, 0);
  ASM_ADD_R_IMM32(J, RA_R8, 16 + ra * 16); // ra

  ASM_MOV_R_IMM(J, RA_RAX, (unsigned long long)(uintptr_t)&luaO_arith);
  ASM_CALL_R(J, RA_RAX);
}

static void jit_emit_op_unm(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPUNM);
}
static void jit_emit_op_bnot(JitState *J, int a, int b, const Instruction *next) {
  emit_unary_arith_common(J, a, b, next, LUA_OPBNOT);
}

/* Stubs or Barriers for others */
static void jit_emit_op_getupval(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_setupval(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_gettabup(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_settabup(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_geti(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_seti(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_getfield(JitState *J, int a, int b, int c) { emit_barrier(J); }
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
static void jit_emit_op_spaceship(JitState *J, int a, int b, int c) { emit_barrier(J); }
static void jit_emit_op_not(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_len(JitState *J, int a, int b) { emit_barrier(J); }
static void jit_emit_op_concat(JitState *J, int a, int b) { emit_barrier(J); }
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
