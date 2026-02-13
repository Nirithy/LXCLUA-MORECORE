#ifndef ljit_emit_stub_h
#define ljit_emit_stub_h

#include "ljit.h"

/*
** Stub JIT Backend for unsupported architectures
** Directly included by ljit.c
*/

typedef struct JitState JitState;

static JitState *jit_new_state(void) { return NULL; }
static void jit_free_state(JitState *J) { (void)J; }
static int jit_begin(JitState *J, size_t initial_size) { (void)J; (void)initial_size; return 0; }
static void jit_end(JitState *J, Proto *p) { (void)J; (void)p; }
static void jit_free_code(Proto *p) { (void)p; }

static void jit_emit_prologue(JitState *J) { (void)J; }
static void jit_emit_epilogue(JitState *J) { (void)J; }
static void jit_emit_op_return0(JitState *J) { (void)J; }
static void jit_emit_op_return1(JitState *J, int ra) { (void)J; (void)ra; }
static void jit_emit_op_add(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) { (void)J; (void)ra; (void)rb; (void)rc; (void)next_pc; }
static void jit_emit_op_sub(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) { (void)J; (void)ra; (void)rb; (void)rc; (void)next_pc; }

#endif
