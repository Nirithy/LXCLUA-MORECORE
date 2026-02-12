/*
** $Id: lproclib.c $
** Process manipulation library for Lua (Linux only)
** See Copyright Notice in lua.h
*/

/* Define _GNU_SOURCE for process_vm_readv/writev */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define lproclib_c
#define LUA_LIB

#include "lprefix.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


#ifdef __linux__

#include <sys/uio.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
** Process handle userdatum
*/
typedef struct {
  pid_t pid;
  int closed;
} ProcessHandle;

#define PROCESS_METATABLE "ProcessHandle"

/*
** Check process handle
*/
static ProcessHandle *checkprocess (lua_State *L) {
  void *ud = luaL_checkudata(L, 1, PROCESS_METATABLE);
  luaL_argcheck(L, ud != NULL, 1, "`process' expected");
  return (ProcessHandle *)ud;
}

/*
** Process.open(pid)
*/
static int l_process_open (lua_State *L) {
  pid_t pid = (pid_t)luaL_checkinteger(L, 1);
  ProcessHandle *p = (ProcessHandle *)lua_newuserdatauv(L, sizeof(ProcessHandle), 0);
  p->pid = pid;
  p->closed = 0;
  luaL_getmetatable(L, PROCESS_METATABLE);
  lua_setmetatable(L, -2);
  return 1;
}

/*
** Process:close()
*/
static int l_process_close (lua_State *L) {
  ProcessHandle *p = checkprocess(L);
  p->closed = 1;
  return 0;
}

/*
** Process:read(addr, size)
*/
static int l_process_read (lua_State *L) {
  ProcessHandle *p = checkprocess(L);
  if (p->closed) return luaL_error(L, "process handle is closed");

  void *addr;
  if (lua_ispointer(L, 2)) {
    addr = (void *)lua_topointer(L, 2);
  } else {
    addr = (void *)(uintptr_t)luaL_checkinteger(L, 2);
  }

  size_t size = (size_t)luaL_checkinteger(L, 3);

  luaL_Buffer b;
  char *buff = luaL_buffinitsize(L, &b, size);

  struct iovec local[1];
  struct iovec remote[1];

  local[0].iov_base = buff;
  local[0].iov_len = size;
  remote[0].iov_base = addr;
  remote[0].iov_len = size;

  ssize_t nread = process_vm_readv(p->pid, local, 1, remote, 1, 0);

  if (nread < 0) {
    return luaL_error(L, "process_vm_readv failed: %s", strerror(errno));
  }

  luaL_pushresultsize(&b, nread);
  return 1;
}

/*
** Process:write(addr, data)
*/
static int l_process_write (lua_State *L) {
  ProcessHandle *p = checkprocess(L);
  if (p->closed) return luaL_error(L, "process handle is closed");

  void *addr;
  if (lua_ispointer(L, 2)) {
    addr = (void *)lua_topointer(L, 2);
  } else {
    addr = (void *)(uintptr_t)luaL_checkinteger(L, 2);
  }

  size_t len;
  const char *data = luaL_checklstring(L, 3, &len);

  struct iovec local[1];
  struct iovec remote[1];

  local[0].iov_base = (void *)data;
  local[0].iov_len = len;
  remote[0].iov_base = addr;
  remote[0].iov_len = len;

  ssize_t nwritten = process_vm_writev(p->pid, local, 1, remote, 1, 0);

  if (nwritten < 0) {
    return luaL_error(L, "process_vm_writev failed: %s", strerror(errno));
  }

  lua_pushinteger(L, nwritten);
  return 1;
}

/*
** Process.getpid()
*/
static int l_process_getpid (lua_State *L) {
  lua_pushinteger(L, getpid());
  return 1;
}

/*
** Library functions
*/
static const luaL_Reg processlib[] = {
  {"open", l_process_open},
  {"getpid", l_process_getpid},
  {NULL, NULL}
};

/*
** Metatable methods
*/
static const luaL_Reg process_methods[] = {
  {"read", l_process_read},
  {"write", l_process_write},
  {"close", l_process_close},
  {"__gc", l_process_close},
  {NULL, NULL}
};

LUAMOD_API int luaopen_process (lua_State *L) {
  luaL_newmetatable(L, PROCESS_METATABLE);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_setfuncs(L, process_methods, 0);
  lua_pop(L, 1);

  luaL_newlib(L, processlib);
  return 1;
}

#else

/* Dummy implementation for non-Linux platforms */
LUAMOD_API int luaopen_process (lua_State *L) {
  luaL_error(L, "process library is only available on Linux");
  return 0;
}

#endif
