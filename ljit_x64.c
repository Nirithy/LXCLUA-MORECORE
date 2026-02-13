#define ljit_x64_c
#define LUA_CORE

#include "lprefix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "ljit_arch.h"
#include "ldo.h"

#if defined(__x86_64__) && defined(__linux__)

#include <sys/mman.h>
#include <unistd.h>

struct JitState {
  unsigned char *code;
  size_t size;
  size_t capacity;
};

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

JitState *jit_new_state(void) {
  JitState *J = (JitState *)malloc(sizeof(JitState));
  if (J) {
    J->code = NULL;
    J->size = 0;
    J->capacity = 0;
  }
  return J;
}

void jit_free_state(JitState *J) {
  if (J) free(J);
}

int jit_begin(JitState *J, size_t initial_size) {
  unsigned char *mem = (unsigned char *)alloc_exec_mem(initial_size);
  if (!mem) return 0;
  J->code = mem;
  J->size = 0;
  J->capacity = initial_size;
  return 1;
}

void jit_end(JitState *J, Proto *p) {
  // If we wanted to shrink the buffer, we could mremap here, but for now we keep it simple.
  p->jit_code = J->code;
  p->jit_size = J->size;
}

void jit_emit_prologue(JitState *J) {
  /*
  ** x86-64 calling convention (System V AMD64 ABI):
  ** Args: RDI, RSI, RDX, RCX, R8, R9
  ** Return: RAX
  ** Callee-saved: RBX, RBP, R12-R15
  */
  emit_byte(J, 0x55);             // push rbp
  emit_byte(J, 0x48); emit_byte(J, 0x89); emit_byte(J, 0xE5); // mov rbp, rsp
}

void jit_emit_epilogue(JitState *J) {
  emit_byte(J, 0x5D);             // pop rbp
  emit_byte(J, 0xC3);             // ret
}

void jit_emit_op_return0(JitState *J) {
  // Arguments: RDI = L, RSI = ci
  // Call luaD_poscall(L, ci, 0)
  // RDI = L (already set)
  // RSI = ci (already set)
  // RDX = 0
  emit_byte(J, 0x48); emit_byte(J, 0x31); emit_byte(J, 0xD2); // xor rdx, rdx

  // Load address of luaD_poscall to RAX
  emit_byte(J, 0x48); emit_byte(J, 0xB8); // mov rax, imm64
  emit_u64(J, (unsigned long long)(uintptr_t)&luaD_poscall);

  // Call rax
  emit_byte(J, 0xFF); emit_byte(J, 0xD0); // call rax

  // Set return value to 1 (success)
  emit_byte(J, 0xB8);
  emit_byte(J, 0x01); emit_byte(J, 0x00); emit_byte(J, 0x00); emit_byte(J, 0x00); // mov eax, 1
}

void jit_free_code(Proto *p) {
  if (p->jit_code) {
    munmap(p->jit_code, JIT_BUFFER_SIZE);
    p->jit_code = NULL;
    p->jit_size = 0;
  }
}

#else

// Stubs for non-x86_64 or non-Linux
struct JitState {};

JitState *jit_new_state(void) { return NULL; }
void jit_free_state(JitState *J) { (void)J; }
int jit_begin(JitState *J, size_t initial_size) { (void)J; (void)initial_size; return 0; }
void jit_end(JitState *J, Proto *p) { (void)J; (void)p; }
void jit_emit_prologue(JitState *J) { (void)J; }
void jit_emit_epilogue(JitState *J) { (void)J; }
void jit_emit_op_return0(JitState *J) { (void)J; }
void jit_free_code(Proto *p) { (void)p; }

#endif
