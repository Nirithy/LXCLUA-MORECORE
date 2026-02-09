#define lthread_c
#define LUA_CORE

#include "lthread.h"
#include <stdlib.h>

/* Mutex */
void l_mutex_init(l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  InitializeCriticalSection(&m->cs);
#else
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&m->mutex, &attr);
  pthread_mutexattr_destroy(&attr);
#endif
}

void l_mutex_lock(l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  EnterCriticalSection(&m->cs);
#else
  pthread_mutex_lock(&m->mutex);
#endif
}

void l_mutex_unlock(l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  LeaveCriticalSection(&m->cs);
#else
  pthread_mutex_unlock(&m->mutex);
#endif
}

void l_mutex_destroy(l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  DeleteCriticalSection(&m->cs);
#else
  pthread_mutex_destroy(&m->mutex);
#endif
}

/* Condition Variable */
void l_cond_init(l_cond_t *c) {
#if defined(LUA_USE_WINDOWS)
  InitializeConditionVariable(&c->cond);
#else
  pthread_cond_init(&c->cond, NULL);
#endif
}

void l_cond_wait(l_cond_t *c, l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  SleepConditionVariableCS(&c->cond, &m->cs, INFINITE);
#else
  pthread_cond_wait(&c->cond, &m->mutex);
#endif
}

void l_cond_signal(l_cond_t *c) {
#if defined(LUA_USE_WINDOWS)
  WakeConditionVariable(&c->cond);
#else
  pthread_cond_signal(&c->cond);
#endif
}

void l_cond_broadcast(l_cond_t *c) {
#if defined(LUA_USE_WINDOWS)
  WakeAllConditionVariable(&c->cond);
#else
  pthread_cond_broadcast(&c->cond);
#endif
}

void l_cond_destroy(l_cond_t *c) {
#if defined(LUA_USE_WINDOWS)
  /* No destroy needed for CONDITION_VARIABLE */
#else
  pthread_cond_destroy(&c->cond);
#endif
}

/* Read/Write Lock (Mapped to Recursive Mutex) */
void l_rwlock_init(l_rwlock_t *l) {
  l_mutex_init(&l->lock);
}

void l_rwlock_rdlock(l_rwlock_t *l) {
  l_mutex_lock(&l->lock);
}

void l_rwlock_wrlock(l_rwlock_t *l) {
  l_mutex_lock(&l->lock);
}

void l_rwlock_unlock(l_rwlock_t *l) {
  l_mutex_unlock(&l->lock);
}

void l_rwlock_destroy(l_rwlock_t *l) {
  l_mutex_destroy(&l->lock);
}

/* Thread */
int l_thread_create(l_thread_t *t, l_thread_func func, void *arg) {
#if defined(LUA_USE_WINDOWS)
  t->thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
  return (t->thread != NULL) ? 0 : 1;
#else
  return pthread_create(&t->thread, NULL, func, arg);
#endif
}

int l_thread_join(l_thread_t t, void **retval) {
#if defined(LUA_USE_WINDOWS)
  WaitForSingleObject(t.thread, INFINITE);
  if (retval) GetExitCodeThread(t.thread, (LPDWORD)retval); /* This casts 32-bit exit code to pointer... dangerous on 64-bit */
  CloseHandle(t.thread);
  return 0;
#else
  return pthread_join(t.thread, retval);
#endif
}
