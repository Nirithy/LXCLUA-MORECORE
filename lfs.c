/*
** $Id: lfs.c $
** Filesystem library for Lua
** See Copyright Notice in lua.h
*/

#define lfs_c
#define LUA_LIB

#include "lprefix.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#define chdir _chdir
#define mkdir(p,m) _mkdir(p)
#define rmdir _rmdir
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

/*
** Utility: Check if a path exists
*/
static int fs_exists (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
#if defined(_WIN32)
  lua_pushboolean(L, access(path, F_OK) == 0);
#else
  struct stat sb;
  lua_pushboolean(L, stat(path, &sb) == 0);
#endif
  return 1;
}

/*
** Utility: Check if a path is a directory
*/
static int fs_isdir (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  struct stat sb;
  if (stat(path, &sb) == 0) {
    lua_pushboolean(L, S_ISDIR(sb.st_mode));
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

/*
** Utility: Check if a path is a file
*/
static int fs_isfile (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  struct stat sb;
  if (stat(path, &sb) == 0) {
    lua_pushboolean(L, S_ISREG(sb.st_mode));
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

/*
** fs.ls(path) -> table of files
*/
static int fs_ls (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
#if defined(_WIN32)
  WIN32_FIND_DATA ffd;
  char search_path[MAX_PATH];
  sprintf(search_path, "%s\\*", path);
  HANDLE hFind = FindFirstFile(search_path, &ffd);

  if (hFind == INVALID_HANDLE_VALUE) {
    return luaL_error(L, "cannot open directory %s", path);
  }

  lua_newtable(L);
  int i = 1;
  do {
    const char *name = ffd.cFileName;
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      lua_pushstring(L, name);
      lua_rawseti(L, -2, i++);
    }
  } while (FindNextFile(hFind, &ffd) != 0);

  FindClose(hFind);
#else
  DIR *d;
  struct dirent *dir;
  d = opendir(path);
  if (!d) {
    return luaL_error(L, "cannot open directory %s: %s", path, strerror(errno));
  }

  lua_newtable(L);
  int i = 1;
  while ((dir = readdir(d)) != NULL) {
    if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
      lua_pushstring(L, dir->d_name);
      lua_rawseti(L, -2, i++);
    }
  }
  closedir(d);
#endif
  return 1;
}

/*
** fs.mkdir(path)
*/
static int fs_mkdir (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  int res;
#if defined(_WIN32)
  res = mkdir(path, 0755);
#else
  res = mkdir(path, 0755);
#endif
  if (res == 0) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}

/*
** fs.rm(path) - removes file or empty directory
*/
static int fs_rm (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  if (remove(path) == 0) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}

/*
** fs.currentdir()
*/
static int fs_currentdir (lua_State *L) {
  char buff[1024];
  if (getcwd(buff, 1024) != NULL) {
    lua_pushstring(L, buff);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}

/*
** fs.chdir(path)
*/
static int fs_chdir (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  if (chdir(path) == 0) {
    lua_pushboolean(L, 1);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}

/*
** fs.stat(path) -> table
*/
static int fs_stat (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  struct stat sb;
  if (stat(path, &sb) == 0) {
    lua_newtable(L);

    lua_pushliteral(L, "size");
    lua_pushinteger(L, (lua_Integer)sb.st_size);
    lua_settable(L, -3);

    lua_pushliteral(L, "mtime");
    lua_pushinteger(L, (lua_Integer)sb.st_mtime);
    lua_settable(L, -3);

    lua_pushliteral(L, "ctime");
    lua_pushinteger(L, (lua_Integer)sb.st_ctime);
    lua_settable(L, -3);

    lua_pushliteral(L, "mode");
    lua_pushinteger(L, (lua_Integer)sb.st_mode);
    lua_settable(L, -3);

    lua_pushliteral(L, "isdir");
    lua_pushboolean(L, S_ISDIR(sb.st_mode));
    lua_settable(L, -3);

    lua_pushliteral(L, "isfile");
    lua_pushboolean(L, S_ISREG(sb.st_mode));
    lua_settable(L, -3);

    return 1;
  }
  lua_pushnil(L);
  lua_pushstring(L, strerror(errno));
  return 2;
}

/*
** fs.abs(path) - Absolute path
*/
static int fs_abs (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
#if defined(_WIN32)
  char full[_MAX_PATH];
  if (_fullpath(full, path, _MAX_PATH) != NULL) {
    lua_pushstring(L, full);
  } else {
    lua_pushnil(L);
  }
#else
  char full[PATH_MAX];
  if (realpath(path, full) != NULL) {
    lua_pushstring(L, full);
  } else {
    /* If file doesn't exist, realpath might fail.
       Fallback: concat cwd + path if path is relative?
       For now, just return nil + error */
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
#endif
  return 1;
}

/*
** fs.basename(path)
*/
static int fs_basename (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  const char *base = strrchr(path, '/');
#if defined(_WIN32)
  const char *base2 = strrchr(path, '\\');
  if (base2 > base) base = base2;
#endif
  if (base) {
    lua_pushstring(L, base + 1);
  } else {
    lua_pushstring(L, path);
  }
  return 1;
}

/*
** fs.dirname(path)
*/
static int fs_dirname (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  const char *base = strrchr(path, '/');
#if defined(_WIN32)
  const char *base2 = strrchr(path, '\\');
  if (base2 > base) base = base2;
#endif
  if (base) {
    if (base == path) {
       lua_pushliteral(L, "/");
    } else {
       lua_pushlstring(L, path, base - path);
    }
  } else {
    lua_pushliteral(L, ".");
  }
  return 1;
}

static const luaL_Reg fslib[] = {
  {"ls", fs_ls},
  {"isdir", fs_isdir},
  {"isfile", fs_isfile},
  {"mkdir", fs_mkdir},
  {"rm", fs_rm},
  {"exists", fs_exists},
  {"stat", fs_stat},
  {"currentdir", fs_currentdir},
  {"chdir", fs_chdir},
  {"abs", fs_abs},
  {"basename", fs_basename},
  {"dirname", fs_dirname},
  {NULL, NULL}
};

LUAMOD_API int luaopen_fs (lua_State *L) {
  luaL_newlib(L, fslib);
  return 1;
}
