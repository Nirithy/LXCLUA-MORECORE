#ifndef ljit_arch_h
#define ljit_arch_h

#include "lua.h"
#include "lobject.h"

/*
** Architecture Abstract Interface for JIT
** Separates core JIT logic from architecture-specific implementation.
*/

#define JIT_BUFFER_SIZE 4096

typedef struct JitState JitState;

/*
** Lifecycle of JitState
*/
JitState *jit_new_state(void);
void jit_free_state(JitState *J);

/*
** Compilation phases
*/
int jit_begin(JitState *J, size_t initial_size);
void jit_end(JitState *J, Proto *p);

/*
** Code Generation
*/
void jit_emit_prologue(JitState *J);
void jit_emit_epilogue(JitState *J);

/*
** Opcode Implementations
*/
void jit_emit_op_return0(JitState *J);

/*
** Memory Management
*/
void jit_free_code(Proto *p);

#endif
