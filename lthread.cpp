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

int l_mutex_trylock(l_mutex_t *m) {
#if defined(LUA_USE_WINDOWS)
  return TryEnterCriticalSection(&m->cs) ? 0 : 1;
#else
  return (pthread_mutex_trylock(&m->mutex) == 0) ? 0 : 1;
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

int l_cond_wait_timeout(l_cond_t *c, l_mutex_t *m, long ms) {
#if defined(LUA_USE_WINDOWS)
  if (SleepConditionVariableCS(&c->cond, &m->cs, ms)) return 0;
  return (GetLastError() == ERROR_TIMEOUT) ? LTHREAD_TIMEDOUT : -1;
#else
  struct timespec ts;
#if defined(__EMSCRIPTEN__)
  struct timeval tv;
  gettimeofday(&tv, NULL);
  ts.tv_sec = tv.tv_sec;
  ts.tv_nsec = tv.tv_usec * 1000;
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (ms % 1000) * 1000000;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
  return (pthread_cond_timedwait(&c->cond, &m->mutex, &ts) == ETIMEDOUT) ? LTHREAD_TIMEDOUT : 0;
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

/* Read/Write Lock with Writer Recursion */
void l_rwlock_init(l_rwlock_t *l) {
#if defined(__cplusplus)
  l->writer_thread_id.store(0);
#else
  atomic_init(&l->writer_thread_id, 0);
#endif
  l->write_recursion = 0;
#if defined(LUA_USE_WINDOWS)
  InitializeSRWLock(&l->lock);
#else
  pthread_rwlock_init(&l->lock, NULL);
#endif
}

void l_rwlock_rdlock(l_rwlock_t *l) {
  size_t self = l_thread_selfid();
  /* Check if we are the writer (recursion) */
#if defined(__cplusplus)
  if (l->writer_thread_id.load() == self) {
#else
  if (atomic_load(&l->writer_thread_id) == self) {
#endif
    /* If we are the writer, we already have exclusive access.
       Just treat this as another level of write lock (since we already block everyone else). */
    l->write_recursion++;
    return;
  }

#if defined(LUA_USE_WINDOWS)
  AcquireSRWLockShared(&l->lock);
#else
  pthread_rwlock_rdlock(&l->lock);
#endif
}

void l_rwlock_wrlock(l_rwlock_t *l) {
  size_t self = l_thread_selfid();
#if defined(__cplusplus)
  if (l->writer_thread_id.load() == self) {
#else
  if (atomic_load(&l->writer_thread_id) == self) {
#endif
    l->write_recursion++;
    return;
  }

#if defined(LUA_USE_WINDOWS)
  AcquireSRWLockExclusive(&l->lock);
#else
  pthread_rwlock_wrlock(&l->lock);
#endif

  /* We now hold the lock. Set ID. */
#if defined(__cplusplus)
  l->writer_thread_id.store(self);
#else
  atomic_store(&l->writer_thread_id, self);
#endif
  l->write_recursion = 1;
}

void l_rwlock_unlock(l_rwlock_t *l) {
  size_t self = l_thread_selfid();
#if defined(__cplusplus)
  if (l->writer_thread_id.load() == self) {
#else
  if (atomic_load(&l->writer_thread_id) == self) {
#endif
    l->write_recursion--;
    if (l->write_recursion == 0) {
      /* Releasing the write lock */
#if defined(__cplusplus)
      l->writer_thread_id.store(0);
#else
      atomic_store(&l->writer_thread_id, 0);
#endif
#if defined(LUA_USE_WINDOWS)
      ReleaseSRWLockExclusive(&l->lock);
#else
      pthread_rwlock_unlock(&l->lock);
#endif
    }
    /* Else: We just decremented recursion count. Do nothing else. */
  } else {
    /* We must be a reader */
#if defined(LUA_USE_WINDOWS)
    ReleaseSRWLockShared(&l->lock);
#else
    pthread_rwlock_unlock(&l->lock);
#endif
  }
}

void l_rwlock_destroy(l_rwlock_t *l) {
#if defined(LUA_USE_WINDOWS)
  /* SRWLock doesn't need destroy */
#else
  pthread_rwlock_destroy(&l->lock);
#endif
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

size_t l_thread_selfid(void) {
#if defined(LUA_USE_WINDOWS)
  return (size_t)GetCurrentThreadId();
#else
  return (size_t)pthread_self();
#endif
}

size_t l_thread_getid(l_thread_t *t) {
#if defined(LUA_USE_WINDOWS)
  return (size_t)GetThreadId(t->thread);
#else
  return (size_t)t->thread;
#endif
}
