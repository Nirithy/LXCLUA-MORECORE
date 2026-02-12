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
#define PATH_MAX _MAX_PATH
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h> /* for PATH_MAX */
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FS_PERM_KEY "LUA_FS_PERMISSIONS"

/*
** Internal: Resolve path to absolute.
** Returns 1 on success, 0 on failure.
*/
static int get_absolute_path(const char *path, char *resolved) {
#if defined(_WIN32)
  if (_fullpath(resolved, path, PATH_MAX) != NULL) {
    return 1;
  }
#else
  if (realpath(path, resolved) != NULL) {
    return 1;
  }
#endif
  return 0;
}

/*
** Internal: Check permission
** op: "read" or "write"
*/
static void check_permission(lua_State *L, const char *path, const char *op) {
  lua_getfield(L, LUA_REGISTRYINDEX, FS_PERM_KEY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return;
  }

  /* Check read_only */
  if (strcmp(op, "write") == 0) {
    lua_getfield(L, -1, "read_only");
    if (lua_toboolean(L, -1)) {
      lua_pop(L, 2); /* pop boolean and table */
      luaL_error(L, "fs: permission denied (read-only filesystem)");
    }
    lua_pop(L, 1);
  }

  /* Check root */
  lua_getfield(L, -1, "root");
  if (!lua_isnil(L, -1)) {
    const char *root = lua_tostring(L, -1);
    char abs_path[PATH_MAX];
    int ok = get_absolute_path(path, abs_path);

    /* If direct resolution fails, try to resolve parent (e.g. for mkdir) */
    if (!ok) {
      char parent[PATH_MAX];
      const char *sep = strrchr(path, '/');
#if defined(_WIN32)
      const char *sep2 = strrchr(path, '\\');
      if (sep2 > sep) sep = sep2;
#endif
      if (sep) {
        size_t len = sep - path;
        if (len >= PATH_MAX) len = PATH_MAX - 1;
        if (len == 0) { /* Root dir */
           strncpy(parent, "/", 2);
        } else {
           strncpy(parent, path, len);
           parent[len] = '\0';
        }

        if (get_absolute_path(parent, abs_path)) {
           /* We resolved parent. Now ensure the child component is valid (no '..') */
           /* The part after sep is the new component */
           const char *child = sep + 1;
           if (strstr(child, "..") != NULL) {
              lua_pop(L, 2);
              luaL_error(L, "fs: invalid path component '..'");
           }
           ok = 1;
        }
      } else {
         /* No separator. Parent is CWD. */
         if (getcwd(parent, PATH_MAX) != NULL) {
            if (get_absolute_path(parent, abs_path)) {
               /* Check if path is ".." */
               if (strcmp(path, "..") == 0) {
                  lua_pop(L, 2);
                  luaL_error(L, "fs: invalid path component '..'");
               }
               ok = 1;
            }
         }
      }
    }

    if (!ok) {
       lua_pop(L, 2);
       luaL_error(L, "fs: cannot resolve path '%s' for permission check", path);
    }

    size_t root_len = strlen(root);
    size_t path_len = strlen(abs_path);
    int allowed = 0;

    if (path_len >= root_len && strncmp(abs_path, root, root_len) == 0) {
       if (abs_path[root_len] == '\0' || abs_path[root_len] == '/' || abs_path[root_len] == '\\') {
          allowed = 1;
       }
    }

    if (!allowed) {
      lua_pop(L, 2);
      luaL_error(L, "fs: permission denied (path '%s' is outside root '%s')", abs_path, root);
    }
  }
  lua_pop(L, 2); /* pop root and perm table */
}

/*
** fs.set_permissions(table)
** table: { root = "/path", read_only = bool }
*/
static int fs_set_permissions (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  /* Check if already set */
  lua_getfield(L, LUA_REGISTRYINDEX, FS_PERM_KEY);
  if (!lua_isnil(L, -1)) {
    return luaL_error(L, "fs: permissions already set");
  }
  lua_pop(L, 1);

  /* Validate and Normalize Root */
  lua_getfield(L, 1, "root");
  if (!lua_isnil(L, -1)) {
    const char *root = lua_tostring(L, -1);
    char abs_root[PATH_MAX];
    if (!get_absolute_path(root, abs_root)) {
       return luaL_error(L, "fs: invalid root path '%s'", root);
    }
    /* Replace root in the config table with resolved absolute path */
    lua_pushstring(L, abs_root);
    lua_setfield(L, 1, "root");
  }
  lua_pop(L, 1);

  /* Store in registry */
  lua_pushvalue(L, 1);
  lua_setfield(L, LUA_REGISTRYINDEX, FS_PERM_KEY);

  return 0;
}


/*
** Utility: Check if a path exists
*/
static int fs_exists (lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  check_permission(L, path, "read");
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
  check_permission(L, path, "read");
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
  check_permission(L, path, "read");
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
  check_permission(L, path, "read");
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
  check_permission(L, path, "write");
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
  check_permission(L, path, "write");
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
  check_permission(L, path, "read");
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
  check_permission(L, path, "read");
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
  check_permission(L, path, "read");
  char full[PATH_MAX];
  if (get_absolute_path(path, full)) {
    lua_pushstring(L, full);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
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
  {"set_permissions", fs_set_permissions},
  {NULL, NULL}
};

LUAMOD_API int luaopen_fs (lua_State *L) {
  luaL_newlib(L, fslib);
  return 1;
}
