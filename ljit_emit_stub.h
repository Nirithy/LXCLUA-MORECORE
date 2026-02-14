#ifndef ljit_emit_stub_h
#define ljit_emit_stub_h

#include "ljit.h"

/*
** Stub JIT Backend for unsupported architectures
** Directly included by ljit.c
*/

#define JIT_BUFFER_SIZE 4096

typedef struct JitState {
  unsigned char *code;
  size_t size;
  size_t capacity;
  Proto *p;
  const Instruction *next_pc;
} JitState;

static JitState *jit_new_state(void) { return NULL; }
static void jit_free_state(JitState *J) { (void)J; }
static int jit_begin(JitState *J, size_t initial_size) { (void)J; (void)initial_size; return 0; }
static void jit_end(JitState *J, Proto *p) { (void)J; (void)p; }
static void jit_free_code(Proto *p) { (void)p; }

static void jit_emit_prologue(JitState *J) { (void)J; }
static void jit_emit_epilogue(JitState *J) { (void)J; }
static void emit_barrier(JitState *J) { (void)J; }
static void jit_emit_op_return0(JitState *J) { (void)J; }
static void jit_emit_op_return1(JitState *J, int ra) { (void)J; (void)ra; }
static void jit_emit_op_add(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) { (void)J; (void)ra; (void)rb; (void)rc; (void)next_pc; }
static void jit_emit_op_sub(JitState *J, int ra, int rb, int rc, const Instruction *next_pc) { (void)J; (void)ra; (void)rb; (void)rc; (void)next_pc; }

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
static void jit_emit_op_mul(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_mod(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_pow(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_div(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_idiv(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_band(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_bor(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_bxor(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_shl(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_shr(JitState *J, int a, int b, int c, const Instruction *next) { (void)J; }
static void jit_emit_op_unm(JitState *J, int a, int b, const Instruction *next) { (void)J; }
static void jit_emit_op_bnot(JitState *J, int a, int b, const Instruction *next) { (void)J; }

#endif
