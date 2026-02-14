/*
** $Id: lsmgrlib.c $
** ShareUserID Manager Library
** See Copyright Notice in lua.h
*/

#define lsmgrlib_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// Windows 专用头文件
#include <windows.h>
#include <io.h>
#else
// 非 Windows（安卓/Linux/macOS）专用头文件
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif


#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* 引入luajava头文件 */
#ifdef __ANDROID__
#include "../luajava/luajava.h"
#endif


#define SHARED_USER_ID "com.difierline.lua.shared"
#define SHARED_DIR_NAME "shared"

/* 全局变量，用于存储应用的files目录路径 */
static char *app_files_dir = NULL;
static char *shared_data_dir = NULL;

/* 初始化应用目录路径 */
static void init_app_dirs(lua_State *L) {
  if (app_files_dir != NULL) {
    return;  /* 已经初始化过 */
  }

#ifdef _WIN32
  /* Windows：使用当前目录 */
  app_files_dir = strdup("./");

#elif defined(__ANDROID__)
  /* 安卓：通过 Java activity 获取应用私有目录 */
  lua_getglobal(L, "activity");
  if (isJavaObject(L, -1)) {
    lua_pushstring(L, "getFilesDir");
    lua_gettable(L, -2);
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -3);
      lua_call(L, 1, 1);

      lua_pushstring(L, "getAbsolutePath");
      lua_gettable(L, -2);
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, -3);
        lua_call(L, 1, 1);

        if (lua_isstring(L, -1)) {
          const char *path_cstr = lua_tostring(L, -1);
          app_files_dir = strdup(path_cstr);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  /* 安卓兜底路径 */
  if (app_files_dir == NULL) {
    app_files_dir = strdup("/data/user/0/com.difierline.lua/files/");
  }

#else
  /* Linux/macOS 等非 Windows、非安卓 */
  const char *home = getenv("HOME");
  if (home != NULL) {
    size_t len = strlen(home) + 16;
    app_files_dir = (char *)malloc(len);
    snprintf(app_files_dir, len, "%s/.lxclua/", home);
  } else {
    app_files_dir = strdup("./");
  }
#endif

  /* 构建共享目录路径 */
  size_t len = strlen(app_files_dir) + strlen(SHARED_DIR_NAME) + 2;
  shared_data_dir = (char *)malloc(len);
  snprintf(shared_data_dir, len, "%s%s/", app_files_dir, SHARED_DIR_NAME);
  
  (void)L;  /* 避免未使用参数警告 */
}


/* 辅助函数：递归创建目录 */
#ifdef _WIN32
/* Windows上的mode_t类型定义 */
typedef unsigned int mode_t;
#endif

static int mkdir_recursive(const char *path, mode_t mode) {
#ifdef _WIN32
  /* Windows上的实现，使用CreateDirectory函数 */
  char *p = strdup(path);
  char *q = p;
  
  /* 跳过开头的路径分隔符 */
  while (*q == '/' || *q == '\\') {
    q++;
  }
  
  /* 递归创建目录 */
  while ((q = strpbrk(q, "/\\")) != NULL) {
    *q = '\0';
    if (*p != '\0') {
      if (!CreateDirectory(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        free(p);
        return -1;
      }
    }
    *q = '\\';
    q++;
    while (*q == '/' || *q == '\\') {
      q++;
    }
  }
  
  /* 创建最后一级目录 */
  if (*p != '\0') {
    if (!CreateDirectory(p, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
      free(p);
      return -1;
    }
  }
  
  free(p);
  return 0;
#else
  /* Unix上的实现 */
  char *p = strdup(path);
  char *q = p;
  int result = 0;
  
  /* 跳过开头的路径分隔符 */
  while (*q == '/') {
    q++;
  }
  
  /* 递归创建目录 */
  while ((q = strchr(q, '/')) != NULL) {
    *q = '\0';
    if (*p != '\0') {
      result = mkdir(p, mode);
      /* 检查错误，但是如果目录已经存在，忽略错误 */
      if (result != 0 && errno != EEXIST) {
        free(p);
        return result;
      }
    }
    *q = '/';
    q++;
    while (*q == '/') {
      q++;
    }
  }
  
  /* 创建最后一级目录 */
  if (*p != '\0') {
    result = mkdir(p, mode);
    /* 如果目录已经存在，返回成功 */
    if (result != 0 && errno == EEXIST) {
      result = 0;
    }
  }
  
  free(p);
  return result;
#endif
}

/* 辅助函数：通配符匹配 */
static int wildcard_match(const char *pattern, const char *str) {
  const char *p = pattern;
  const char *s = str;
  
  while (*p && *s) {
    if (*p == '*') {
      /* 处理通配符 * */
      if (*(p + 1) == '\0') {
        return 1; /* 模式以 * 结尾，匹配任意字符串 */
      }
      
      /* 递归匹配 * 后面的模式 */
      while (*s) {
        if (wildcard_match(p + 1, s)) {
          return 1;
        }
        s++;
      }
      return 0;
    } else if (*p == '?') {
      /* 处理通配符 ?，匹配任意单个字符 */
      s++;
      p++;
    } else if (*p == *s) {
      /* 普通字符匹配 */
      s++;
      p++;
    } else {
      return 0;
    }
  }
  
  /* 处理剩余的 * */
  while (*p == '*') {
    p++;
  }
  
  return *p == '\0' && *s == '\0';
}

/* 辅助函数：递归搜索文件 */
static void find_files_recursive(const char *base_path, const char *pattern, int recursive, lua_State *L, int result_table) {
#ifdef _WIN32
  /* Windows上的实现，使用FindFirstFile和FindNextFile函数 */
  char search_path[512];
  char entry_path[512];
  
  /* 构建搜索路径 */
  snprintf(search_path, sizeof(search_path), "%s\\*", base_path);
  
  /* 开始搜索 */
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(search_path, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }
  
  /* 遍历搜索结果 */
  do {
    /* 跳过 . 和 .. 目录 */
    if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
      continue;
    }
    
    /* 构建完整路径 */
    snprintf(entry_path, sizeof(entry_path), "%s\\%s", base_path, findData.cFileName);
    
    /* 检查是否匹配模式 */
    if (wildcard_match(pattern, findData.cFileName)) {
      /* 添加到结果表 */
      lua_newtable(L);
      
      /* 计算相对路径 */
      char *shared_data_dir_ptr = strstr(entry_path, shared_data_dir);
      const char *relative_path = shared_data_dir_ptr ? (shared_data_dir_ptr + strlen(shared_data_dir)) : entry_path;
      
      lua_pushstring(L, "path");
      lua_pushstring(L, relative_path);
      lua_rawset(L, -3);
      
      lua_pushstring(L, "name");
      lua_pushstring(L, findData.cFileName);
      lua_rawset(L, -3);
      
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        lua_pushstring(L, "type");
        lua_pushstring(L, "directory");
        lua_rawset(L, -3);
      } else {
        lua_pushstring(L, "type");
        lua_pushstring(L, "file");
        lua_rawset(L, -3);
        
        /* 获取文件大小 */
        LARGE_INTEGER fileSize;
        fileSize.LowPart = findData.nFileSizeLow;
        fileSize.HighPart = findData.nFileSizeHigh;
        lua_pushstring(L, "size");
        lua_pushinteger(L, (lua_Integer)fileSize.QuadPart);
        lua_rawset(L, -3);
      }
      
      /* 添加到结果表 */
      lua_rawseti(L, result_table, luaL_len(L, result_table) + 1);
    }
    
    /* 递归搜索子目录 */
    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && recursive) {
      find_files_recursive(entry_path, pattern, recursive, L, result_table);
    }
  } while (FindNextFile(hFind, &findData));
  
  /* 关闭搜索句柄 */
  FindClose(hFind);
#else
  /* Unix上的实现 */
  DIR *dir = opendir(base_path);
  if (dir == NULL) {
    return;
  }
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    
    char entry_path[512];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", base_path, entry->d_name);
    
    struct stat st;
    if (stat(entry_path, &st) != 0) {
      continue;
    }
    
    /* 检查是否匹配模式 */
    if (wildcard_match(pattern, entry->d_name)) {
      /* 添加到结果表 */
      lua_newtable(L);
      
      /* 计算相对路径 */
      char *shared_data_dir_ptr = strstr(entry_path, shared_data_dir);
      const char *relative_path = shared_data_dir_ptr ? (shared_data_dir_ptr + strlen(shared_data_dir)) : entry_path;
      
      lua_pushstring(L, "path");
      lua_pushstring(L, relative_path);
      lua_rawset(L, -3);
      
      lua_pushstring(L, "name");
      lua_pushstring(L, entry->d_name);
      lua_rawset(L, -3);
      
      if (S_ISDIR(st.st_mode)) {
        lua_pushstring(L, "type");
        lua_pushstring(L, "directory");
        lua_rawset(L, -3);
      } else {
        lua_pushstring(L, "type");
        lua_pushstring(L, "file");
        lua_rawset(L, -3);
        
        lua_pushstring(L, "size");
        lua_pushinteger(L, (lua_Integer)st.st_size);
        lua_rawset(L, -3);
      }
      
      /* 添加到结果表 */
      lua_rawseti(L, result_table, luaL_len(L, result_table) + 1);
    }
    
    /* 递归搜索子目录 */
    if (S_ISDIR(st.st_mode) && recursive) {
      find_files_recursive(entry_path, pattern, recursive, L, result_table);
    }
  }
  
  closedir(dir);
#endif
}

static int ensure_shared_dir_exists(lua_State *L) {
  /* 确保共享目录存在 */
  init_app_dirs(L);
  
  /* 递归创建共享目录 */
  return mkdir_recursive(shared_data_dir, 0777);  /* 777权限允许同一userid的应用访问 */
}

static int smgr_mkdir(lua_State *L) {
  /* 创建目录 */
  init_app_dirs(L);
  const char *dirname = luaL_checkstring(L, 1);
  char full_path[256];
  snprintf(full_path, sizeof(full_path), "%s%s", shared_data_dir, dirname);
  
  /* 递归创建目录 */
  int result = mkdir_recursive(full_path, 0777);
  lua_pushboolean(L, result == 0);
  return 1;
}


static int smgr_getuserid (lua_State *L) {
  /* 获取当前应用的sharedUserId */
  lua_pushstring(L, SHARED_USER_ID);
  return 1;
}


static int smgr_hasshareduserid (lua_State *L) {
  /* 检查是否有shareuserid */
  lua_pushboolean(L, 1);  /* 已在AndroidManifest.xml中配置了sharedUserId */
  return 1;
}


static int smgr_getdatadir (lua_State *L) {
  /* 获取共享数据目录路径 */
  init_app_dirs(L);
  ensure_shared_dir_exists(L);
  lua_pushstring(L, shared_data_dir);
  return 1;
}


static int smgr_readfile (lua_State *L) {
  /* 读取共享文件 */
  init_app_dirs(L);
  const char *filename = luaL_checkstring(L, 1);
  char filepath[256];
  snprintf(filepath, sizeof(filepath), "%s%s", shared_data_dir, filename);
  
  FILE *file = fopen(filepath, "r");
  if (file == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  /* 获取文件大小 */
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  
  /* 读取文件内容 */
  char *buffer = (char *)malloc(size + 1);
  if (buffer == NULL) {
    fclose(file);
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  size_t read_size = fread(buffer, 1, size, file);
  buffer[read_size] = '\0';
  fclose(file);
  
  lua_pushlstring(L, buffer, read_size);
  free(buffer);
  return 1;
}


static int smgr_writefile (lua_State *L) {
  /* 写入共享文件 */
  init_app_dirs(L);
  const char *filename = luaL_checkstring(L, 1);
  const char *content = luaL_checkstring(L, 2);
  char filepath[256];
  char dirpath[256];
  
  ensure_shared_dir_exists(L);
  snprintf(filepath, sizeof(filepath), "%s%s", shared_data_dir, filename);
  
  /* 创建父目录 */
  strncpy(dirpath, filepath, sizeof(dirpath) - 1);
  dirpath[sizeof(dirpath) - 1] = '\0';
  char *last_slash = strrchr(dirpath, '/');
  if (last_slash != NULL) {
    *last_slash = '\0';
    mkdir_recursive(dirpath, 0777);
  }
  
  FILE *file = fopen(filepath, "w");
  if (file == NULL) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  size_t written = fwrite(content, 1, strlen(content), file);
  fclose(file);
  
  if (written == strlen(content)) {
    lua_pushboolean(L, 1);
    return 1;
  } else {
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
}


static int smgr_deletefile (lua_State *L) {
  /* 删除共享文件 */
  init_app_dirs(L);
  const char *filename = luaL_checkstring(L, 1);
  char filepath[256];
  snprintf(filepath, sizeof(filepath), "%s%s", shared_data_dir, filename);
  
  int result = remove(filepath);
  if (result == 0) {
    lua_pushboolean(L, 1);
    return 1;
  } else {
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
}


static int smgr_listfiles (lua_State *L) {
  /* 列出目录中的文件和目录，返回包含类型信息的表 */
  init_app_dirs(L);
  ensure_shared_dir_exists(L);
  
  /* 获取要列出的目录名，默认为空（根目录） */
  const char *dirname = luaL_optstring(L, 1, "");
  char dirpath[256];
  char search_path[512];
  char entry_path[512];
  
  /* 构建目录路径 */
  if (*dirname == '\0') {
    /* 列出根目录 */
    strncpy(dirpath, shared_data_dir, sizeof(dirpath) - 1);
  } else {
    /* 列出指定子目录 */
    snprintf(dirpath, sizeof(dirpath), "%s%s", shared_data_dir, dirname);
    
    /* 确保路径以'/'结尾 */
    size_t len = strlen(dirpath);
    if (len > 0 && dirpath[len - 1] != '/') {
      strncat(dirpath, "/", sizeof(dirpath) - len - 1);
    }
  }
  
  /* 创建结果表 */
  lua_newtable(L);
  int index = 1;
  
#ifdef _WIN32
  /* Windows上的实现，使用FindFirstFile和FindNextFile函数 */
  /* 构建搜索路径 */
  snprintf(search_path, sizeof(search_path), "%s\\*", dirpath);
  
  /* 开始搜索 */
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(search_path, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(GetLastError()));
    return 2;
  }
  
  /* 遍历搜索结果 */
  do {
    /* 跳过 . 和 .. 目录 */
    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
      /* 创建条目表 */
      lua_newtable(L);
      
      /* 添加名称 */
      lua_pushstring(L, "name");
      lua_pushstring(L, findData.cFileName);
      lua_rawset(L, -3);
      
      /* 检查是文件还是目录 */
      snprintf(entry_path, sizeof(entry_path), "%s\\%s", dirpath, findData.cFileName);
      
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        /* 是目录 */
        lua_pushstring(L, "type");
        lua_pushstring(L, "directory");
        lua_rawset(L, -3);
      } else {
        /* 是文件 */
        lua_pushstring(L, "type");
        lua_pushstring(L, "file");
        lua_rawset(L, -3);
        
        /* 添加文件大小 */
        LARGE_INTEGER fileSize;
        fileSize.LowPart = findData.nFileSizeLow;
        fileSize.HighPart = findData.nFileSizeHigh;
        lua_pushstring(L, "size");
        lua_pushinteger(L, (lua_Integer)fileSize.QuadPart);
        lua_rawset(L, -3);
      }
      
      /* 将条目添加到结果表 */
      lua_rawseti(L, -2, index++);
    }
  } while (FindNextFile(hFind, &findData));
  
  /* 关闭搜索句柄 */
  FindClose(hFind);
#else
  /* Unix上的实现 */
  DIR *dir = opendir(dirpath);
  if (dir == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  struct dirent *entry;
  
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      /* 创建条目表 */
      lua_newtable(L);
      
      /* 添加名称 */
      lua_pushstring(L, "name");
      lua_pushstring(L, entry->d_name);
      lua_rawset(L, -3);
      
      /* 检查是文件还是目录 */
      snprintf(entry_path, sizeof(entry_path), "%s%s", dirpath, entry->d_name);
      
      struct stat st;
      if (stat(entry_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
          /* 是目录 */
          lua_pushstring(L, "type");
          lua_pushstring(L, "directory");
          lua_rawset(L, -3);
        } else {
          /* 是文件 */
          lua_pushstring(L, "type");
          lua_pushstring(L, "file");
          lua_rawset(L, -3);
          
          /* 添加文件大小 */
          lua_pushstring(L, "size");
          lua_pushinteger(L, (lua_Integer)st.st_size);
          lua_rawset(L, -3);
        }
      } else {
        /* 无法获取信息，默认为文件 */
        lua_pushstring(L, "type");
        lua_pushstring(L, "file");
        lua_rawset(L, -3);
      }
      
      /* 将条目添加到结果表 */
      lua_rawseti(L, -2, index++);
    }
  }
  
  /* 关闭目录 */
  closedir(dir);
#endif
  
  return 1;
}


static int smgr_fileexists (lua_State *L) {
  /* 检查文件是否存在 */
  init_app_dirs(L);
  const char *filename = luaL_checkstring(L, 1);
  char filepath[256];
  
  /* 构建文件路径 */
  snprintf(filepath, sizeof(filepath), "%s%s", shared_data_dir, filename);
  
#ifdef _WIN32
  /* Windows上的实现，使用GetFileAttributes函数 */
  DWORD attr = GetFileAttributes(filepath);
  lua_pushboolean(L, attr != INVALID_FILE_ATTRIBUTES);
#else
  /* Unix上的实现，使用stat函数 */
  struct stat st;
  int result = stat(filepath, &st);
  lua_pushboolean(L, result == 0);
#endif
  
  return 1;
}


static int smgr_getfilesize (lua_State *L) {
  /* 获取文件大小 */
  init_app_dirs(L);
  const char *filename = luaL_checkstring(L, 1);
  char filepath[256];
  
  /* 构建文件路径 */
  snprintf(filepath, sizeof(filepath), "%s%s", shared_data_dir, filename);
  
#ifdef _WIN32
  /* Windows上的实现，使用FindFirstFile函数 */
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(filepath, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    lua_pushnil(L);
    lua_pushstring(L, strerror(GetLastError()));
    return 2;
  }
  
  /* 获取文件大小 */
  LARGE_INTEGER fileSize;
  fileSize.LowPart = findData.nFileSizeLow;
  fileSize.HighPart = findData.nFileSizeHigh;
  
  /* 关闭搜索句柄 */
  FindClose(hFind);
  
  lua_pushinteger(L, (lua_Integer)fileSize.QuadPart);
#else
  /* Unix上的实现，使用stat函数 */
  struct stat st;
  int result = stat(filepath, &st);
  if (result == 0) {
    lua_pushinteger(L, st.st_size);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
#endif
  
  return 1;
}


static int smgr_copyfile (lua_State *L) {
  /* 复制文件 */
  init_app_dirs(L);
  const char *src = luaL_checkstring(L, 1);
  const char *dest = luaL_checkstring(L, 2);
  
  char srcpath[256], destpath[256], destdirpath[256];
  snprintf(srcpath, sizeof(srcpath), "%s%s", shared_data_dir, src);
  snprintf(destpath, sizeof(destpath), "%s%s", shared_data_dir, dest);
  
  /* 创建目标文件的父目录 */
  strncpy(destdirpath, destpath, sizeof(destdirpath) - 1);
  destdirpath[sizeof(destdirpath) - 1] = '\0';
  char *last_slash = strrchr(destdirpath, '/');
  if (last_slash != NULL) {
    *last_slash = '\0';
    mkdir_recursive(destdirpath, 0777);
  }
  
  FILE *srcfile = fopen(srcpath, "r");
  if (srcfile == NULL) {
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  FILE *destfile = fopen(destpath, "w");
  if (destfile == NULL) {
    fclose(srcfile);
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  
  char buffer[4096];
  size_t read_size;
  while ((read_size = fread(buffer, 1, sizeof(buffer), srcfile)) > 0) {
    if (fwrite(buffer, 1, read_size, destfile) != read_size) {
      fclose(srcfile);
      fclose(destfile);
      lua_pushboolean(L, 0);
      lua_pushstring(L, strerror(errno));
      return 2;
    }
  }
  
  fclose(srcfile);
  fclose(destfile);
  lua_pushboolean(L, 1);
  return 1;
}


static int smgr_renamefile (lua_State *L) {
  /* 重命名文件 */
  init_app_dirs(L);
  const char *oldname = luaL_checkstring(L, 1);
  const char *newname = luaL_checkstring(L, 2);
  
  char oldpath[256], newpath[256], newdirpath[256];
  snprintf(oldpath, sizeof(oldpath), "%s%s", shared_data_dir, oldname);
  snprintf(newpath, sizeof(newpath), "%s%s", shared_data_dir, newname);
  
  /* 创建新文件的父目录 */
  strncpy(newdirpath, newpath, sizeof(newdirpath) - 1);
  newdirpath[sizeof(newdirpath) - 1] = '\0';
  char *last_slash = strrchr(newdirpath, '/');
  if (last_slash != NULL) {
    *last_slash = '\0';
    mkdir_recursive(newdirpath, 0777);
  }
  
  int result = rename(oldpath, newpath);
  if (result == 0) {
    lua_pushboolean(L, 1);
    return 1;
  } else {
    lua_pushboolean(L, 0);
    lua_pushstring(L, strerror(errno));
    return 2;
  }
}


static int smgr_getpackagename (lua_State *L) {
  /* 获取当前包名 */
  lua_pushstring(L, "com.difierline.lua");
  return 1;
}

static int smgr_find (lua_State *L) {
  /* 搜索文件和目录 */
  init_app_dirs(L);
  
  /* 参数解析 */
  const char *pattern = luaL_checkstring(L, 1);
  const char *base_dir = luaL_optstring(L, 2, "");
  int recursive = luaL_opt(L, lua_toboolean, 3, 1); /* 默认递归搜索 */
  
  /* 构建完整的基础路径 */
  char full_base_path[256];
  if (*base_dir == '\0') {
    strncpy(full_base_path, shared_data_dir, sizeof(full_base_path) - 1);
  } else {
    snprintf(full_base_path, sizeof(full_base_path), "%s%s", shared_data_dir, base_dir);
    
    /* 确保路径以'/'结尾 */
    size_t len = strlen(full_base_path);
    if (len > 0 && full_base_path[len - 1] != '/') {
      strncat(full_base_path, "/", sizeof(full_base_path) - len - 1);
    }
  }
  
  /* 检查基础路径是否存在 */
#ifdef _WIN32
  /* Windows上的实现，使用GetFileAttributes函数 */
  DWORD attr = GetFileAttributes(full_base_path);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
    lua_pushnil(L);
    lua_pushstring(L, "搜索路径不存在或不是目录");
    return 2;
  }
#else
  /* Unix上的实现，使用stat函数 */
  struct stat st;
  if (stat(full_base_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
    lua_pushnil(L);
    lua_pushstring(L, "搜索路径不存在或不是目录");
    return 2;
  }
#endif
  
  /* 创建结果表 */
  lua_newtable(L);
  int result_table = lua_gettop(L);
  
  /* 开始递归搜索 */
  find_files_recursive(full_base_path, pattern, recursive, L, result_table);
  
  return 1;
}


static const luaL_Reg smgr_funcs[] = {
  {"getuserid", smgr_getuserid},
  {"hasshareduserid", smgr_hasshareduserid},
  {"getdatadir", smgr_getdatadir},
  {"readfile", smgr_readfile},
  {"writefile", smgr_writefile},
  {"deletefile", smgr_deletefile},
  {"listfiles", smgr_listfiles},
  {"fileexists", smgr_fileexists},
  {"getfilesize", smgr_getfilesize},
  {"copyfile", smgr_copyfile},
  {"renamefile", smgr_renamefile},
  {"getpackagename", smgr_getpackagename},
  {"mkdir", smgr_mkdir},
  {"find", smgr_find},
  {NULL, NULL}
};


LUAMOD_API int luaopen_smgr (lua_State *L) {
  /* 初始化应用目录 */
  init_app_dirs(L);
  ensure_shared_dir_exists(L);
  
  luaL_newlib(L, smgr_funcs);
  return 1;
}