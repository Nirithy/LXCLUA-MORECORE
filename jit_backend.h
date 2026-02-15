#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include "lua.h"
#include "lobject.h"
#include "lstate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize JIT engine */
void jit_init(void);

/* Compile a function prototype */
/* Returns 1 on success, 0 on failure */
int jit_compile(lua_State *L, Proto *p);

/* Free JIT resources for a function */
void jit_free(Proto *p);

/* Type for JIT compiled function */
/* Returns 1 if execution finished (return executed), 0 if fallback needed */
typedef int (*JitFunction)(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
