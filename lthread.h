#ifndef lthread_h
#define lthread_h

#include "lua.h"

#if defined(LUA_USE_WINDOWS)
  #include <windows.h>
#else
  #include <pthread.h>
  #include <time.h>
#endif

typedef struct l_mutex_t {
#if defined(LUA_USE_WINDOWS)
  CRITICAL_SECTION cs;
#else
  pthread_mutex_t mutex;
#endif
} l_mutex_t;

typedef struct l_cond_t {
#if defined(LUA_USE_WINDOWS)
  CONDITION_VARIABLE cond;
#else
  pthread_cond_t cond;
#endif
} l_cond_t;

typedef struct l_rwlock_t {
  l_mutex_t lock; /* Downgraded to recursive mutex to prevent GC deadlocks */
} l_rwlock_t;

typedef struct l_thread_t {
#if defined(LUA_USE_WINDOWS)
  HANDLE thread;
#else
  pthread_t thread;
#endif
} l_thread_t;

/* Thread function signature */
typedef void *(*l_thread_func)(void *arg);

/* Mutex API */
void l_mutex_init(l_mutex_t *m);
void l_mutex_lock(l_mutex_t *m);
void l_mutex_unlock(l_mutex_t *m);
void l_mutex_destroy(l_mutex_t *m);

/* Condition Variable API */
void l_cond_init(l_cond_t *c);
void l_cond_wait(l_cond_t *c, l_mutex_t *m);
void l_cond_signal(l_cond_t *c);
void l_cond_broadcast(l_cond_t *c);
void l_cond_destroy(l_cond_t *c);

/* Read/Write Lock API */
void l_rwlock_init(l_rwlock_t *l);
void l_rwlock_rdlock(l_rwlock_t *l);
void l_rwlock_wrlock(l_rwlock_t *l);
void l_rwlock_unlock(l_rwlock_t *l);
void l_rwlock_destroy(l_rwlock_t *l);

/* Thread API */
int l_thread_create(l_thread_t *t, l_thread_func func, void *arg);
int l_thread_join(l_thread_t t, void **retval);

#endif
