#define ljit_c
#define LUA_CORE

#include "lprefix.h"
#include <sys/mman.h>
#include <unistd.h>
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

/*
** Simple JIT Compiler for LXCLUA
** Currently supports OP_RETURN0 for verification.
*/

#define JIT_BUFFER_SIZE 4096

#if defined(__x86_64__) && defined(__linux__)
typedef struct JitState {
  unsigned char *code;
  size_t size;
  size_t capacity;
} JitState;

static void emit_byte(JitState *J, unsigned char b) {
  if (J->size < J->capacity) {
    J->code[J->size++] = b;
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

void luaJ_compile(lua_State *L, Proto *p) {
  if (p->jit_code) return; // Already compiled

  // Simple heuristic: Only compile small functions with supported opcodes
  // Check if first instruction is OP_RETURN0
  if (p->sizecode < 1) return;

  Instruction inst = p->code[0];
  OpCode op = GET_OPCODE(inst);

  // For this PoC, only support functions starting with RETURN0
  if (op != OP_RETURN0) return;

  size_t capacity = JIT_BUFFER_SIZE;
  unsigned char *mem = (unsigned char *)alloc_exec_mem(capacity);
  if (!mem) return;

  JitState J;
  J.code = mem;
  J.size = 0;
  J.capacity = capacity;

  /*
  ** x86-64 calling convention (System V AMD64 ABI):
  ** Args: RDI, RSI, RDX, RCX, R8, R9
  ** Return: RAX
  ** Callee-saved: RBX, RBP, R12-R15
  **
  ** JitFunction signature: int (*)(lua_State *L, CallInfo *ci)
  ** RDI = L
  ** RSI = ci
  */

  // Prologue
  emit_byte(&J, 0x55);             // push rbp
  emit_byte(&J, 0x48); emit_byte(&J, 0x89); emit_byte(&J, 0xE5); // mov rbp, rsp

  // Arguments: RDI = L, RSI = ci
  // Call luaD_poscall(L, ci, 0)
  // RDI = L (already set)
  // RSI = ci (already set)
  // RDX = 0
  emit_byte(&J, 0x48); emit_byte(&J, 0x31); emit_byte(&J, 0xD2); // xor rdx, rdx

  // Load address of luaD_poscall to RAX
  emit_byte(&J, 0x48); emit_byte(&J, 0xB8); // mov rax, imm64
  emit_u64(&J, (unsigned long long)(uintptr_t)&luaD_poscall);

  // Call rax
  emit_byte(&J, 0xFF); emit_byte(&J, 0xD0); // call rax

  // Set return value to 1 (success)
  emit_byte(&J, 0xB8);
  emit_byte(&J, 0x01); emit_byte(&J, 0x00); emit_byte(&J, 0x00); emit_byte(&J, 0x00); // mov eax, 1

  // Epilogue
  emit_byte(&J, 0x5D);             // pop rbp
  emit_byte(&J, 0xC3);             // ret

  p->jit_code = mem;
  p->jit_size = J.size;
}

void luaJ_freeproto(Proto *p) {
  if (p->jit_code) {
    munmap(p->jit_code, JIT_BUFFER_SIZE);
    p->jit_code = NULL;
    p->jit_size = 0;
  }
}
#else
void luaJ_compile(lua_State *L, Proto *p) {
  (void)L; (void)p;
}
void luaJ_freeproto(Proto *p) {
  (void)p;
}
#endif
