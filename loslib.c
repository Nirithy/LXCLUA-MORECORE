/*
** $Id: loslib.c $
** Standard Operating System library
** See Copyright Notice in lua.h
*/

#define loslib_c
#define LUA_LIB

#include "lprefix.h"

#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 平台相关系统头文件
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <tlhelp32.h>
#elif defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#else
// Android / Linux 通用
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/syscall.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <linux/seccomp.h>
#endif
#if defined(__APPLE__)
#include <pthread.h>
#endif
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"



/*
** {=======================================================
** List of valid conversion specifiers for the 'strftime' function;
** options are grouped by length; group of length 2 start with '||'.
** ========================================================
*/
#if !defined(LUA_STRFTIMEOPTIONS)	/* { */

#if defined(LUA_USE_WINDOWS)
#define LUA_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYzZ%" \
    "||" "#c#x#d#H#I#j#m#M#S#U#w#W#y#Y"  /* two-char options */
#elif defined(LUA_USE_C89)  /* C89 (only 1-char options) */
#define LUA_STRFTIMEOPTIONS  "aAbBcdHIjmMpSUwWxXyYZ%"
#else  /* C99 specification */
#define LUA_STRFTIMEOPTIONS  "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%" \
    "||" "EcECExEXEyEY" "OdOeOHOIOmOMOSOuOUOVOwOWOy"  /* two-char options */
#endif

#endif					/* } */
/* }======================================================= */


/*
** {=======================================================
** Configuration for time-related stuff
** ========================================================
*/

/*
** type to represent time_t in Lua
*/
#if !defined(LUA_NUMTIME)	/* { */

#define l_timet			lua_Integer
#define l_pushtime(L,t)		lua_pushinteger(L,(lua_Integer)(t))
#define l_gettime(L,arg)	luaL_checkinteger(L, arg)

#else				/* }{ */

#define l_timet			lua_Number
#define l_pushtime(L,t)		lua_pushnumber(L,(lua_Number)(t))
#define l_gettime(L,arg)	luaL_checknumber(L, arg)

#endif				/* } */


#if !defined(l_gmtime)		/* { */
/*
** By default, Lua uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(LUA_USE_POSIX)	/* { */

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else				/* }{ */

/* ISO C definitions */
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)	((void)(r)->tm_sec, localtime(t))

#endif				/* } */

#endif				/* } */

/* }======================================================= */


/*
** {=======================================================
** Configuration for 'tmpnam':
** By default, Lua uses tmpnam except when POSIX is available, where
** it uses mkstemp.
** ========================================================
*/
#if !defined(lua_tmpnam)	/* { */

#if defined(LUA_USE_POSIX)	/* { */

#include <unistd.h>

#define LUA_TMPNAMBUFSIZE	32

#if !defined(LUA_TMPNAMTEMPLATE)
#define LUA_TMPNAMTEMPLATE	"/tmp/lua_XXXXXX"
#endif

#define lua_tmpnam(b,e) { \
        strcpy(b, LUA_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				/* }{ */

/* ISO C definitions */
#define LUA_TMPNAMBUFSIZE	L_tmpnam
#define lua_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				/* } */

#endif				/* } */
/* }======================================================= */


#if !defined(l_system)
#if defined(LUA_USE_IOS)
/* Despite claiming to be ISO C, iOS does not implement 'system'. */
#define l_system(cmd) ((cmd) == NULL ? 0 : -1)
#else
#define l_system(cmd)	system(cmd)  /* default definition */
#endif
#endif


static int os_execute (lua_State *L) {
  const char *cmd = luaL_optstring(L, 1, NULL);
  int stat;
  errno = 0;
  stat = l_system(cmd);
  if (cmd != NULL)
    return luaL_execresult(L, stat);
  else {
    lua_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  errno = 0;
  return luaL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (lua_State *L) {
  const char *fromname = luaL_checkstring(L, 1);
  const char *toname = luaL_checkstring(L, 2);
  errno = 0;
  return luaL_fileresult(L, rename(fromname, toname) == 0, NULL);
}


static int os_tmpname (lua_State *L) {
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  lua_tmpnam(buff, err);
  if (l_unlikely(err))
    return luaL_error(L, "unable to generate a unique filename");
  lua_pushstring(L, buff);
  return 1;
}


static int os_getenv (lua_State *L) {
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (lua_State *L) {
  lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {===========================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** ============================================
*/

/*
** About the overflow check: an overflow cannot occur when time
** is represented by a lua_Integer, because either lua_Integer is
** large enough to represent all int fields or it is not large enough
** to represent a time that cause a field to overflow.  However, if
** times are represented as doubles and lua_Integer is int, then the
** time 0x1.e1853b0d184f6p+55 would cause an overflow when adding 1900
** to compute the year.
*/
static void setfield (lua_State *L, const char *key, int value, int delta) {
  #if (defined(LUA_NUMTIME) && LUA_MAXINTEGER <= INT_MAX)
    if (l_unlikely(value > LUA_MAXINTEGER - delta))
      luaL_error(L, "field '%s' is out-of-bound", key);
  #endif
  lua_pushinteger(L, (lua_Integer)value + delta);
  lua_setfield(L, -2, key);
}


static void setboolfield (lua_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
}


/*
** Set all fields from structure 'tm' in the table on top of the stack
*/
static void setallfields (lua_State *L, struct tm *stm) {
  setfield(L, "year", stm->tm_year, 1900);
  setfield(L, "month", stm->tm_mon, 1);
  setfield(L, "day", stm->tm_mday, 0);
  setfield(L, "hour", stm->tm_hour, 0);
  setfield(L, "min", stm->tm_min, 0);
  setfield(L, "sec", stm->tm_sec, 0);
  setfield(L, "yday", stm->tm_yday, 1);
  setfield(L, "wday", stm->tm_wday, 1);
  setboolfield(L, "isdst", stm->tm_isdst);
}


static int getboolfield (lua_State *L, const char *key) {
  int res;
  res = (lua_getfield(L, -1, key) == LUA_TNIL) ? -1 : lua_toboolean(L, -1);
  lua_pop(L, 1);
  return res;
}


static int getfield (lua_State *L, const char *key, int d, int delta) {
  int isnum;
  int t = lua_getfield(L, -1, key);  /* get field and its type */
  lua_Integer res = lua_tointegerx(L, -1, &isnum);
  if (!isnum) {  /* field is not an integer? */
    if (l_unlikely(t != LUA_TNIL))  /* some other value? */
      return luaL_error(L, "field '%s' is not an integer", key);
    else if (l_unlikely(d < 0))  /* absent field; no default? */
      return luaL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  else {
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res))
      return luaL_error(L, "field '%s' is out-of-bound", key);
    res -= delta;
  }
  lua_pop(L, 1);
  return (int)res;
}


static const char *checkoption (lua_State *L, const char *conv,
                                size_t convlen, char *buff) {
  const char *option = LUA_STRFTIMEOPTIONS;
  unsigned oplen = 1;  /* length of options being checked */
  for (; *option != '\0' && oplen <= convlen; option += oplen) {
    if (*option == '|')  /* next block? */
      oplen++;  /* will check options with next length (+1) */
    else if (memcmp(conv, option, oplen) == 0) {  /* match? */
      memcpy(buff, conv, oplen);  /* copy valid option to buffer */
      buff[oplen] = '\0';
      return conv + oplen;  /* return next item */
    }
  }
  luaL_argerror(L, 1,
    lua_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static time_t l_checktime (lua_State *L, int arg) {
  l_timet t = l_gettime(L, arg);
  luaL_argcheck(L, (time_t)t == t, arg, "time out-of-bounds");
  return (time_t)t;
}


/* maximum size for an individual 'strftime' item */
#define SIZETIMEFMT	250


static int os_date (lua_State *L) {
  size_t slen;
  const char *s = luaL_optlstring(L, 1, "%c", &slen);
  time_t t = luaL_opt(L, l_checktime, 2, time(NULL));
  const char *se = s + slen;  /* 's' end */
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip '!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    return luaL_error(L,
                 "date result cannot be represented in this installation");
  if (strcmp(s, "*t") == 0) {
    lua_createtable(L, 0, 9);  /* 9 = number of fields */
    setallfields(L, stm);
  }
  else {
    char cc[4];  /* buffer for individual conversion specifiers */
    luaL_Buffer b;
    cc[0] = '%';
    luaL_buffinit(L, &b);
    while (s < se) {
      if (*s != '%')  /* not a conversion specifier? */
        luaL_addchar(&b, *s++);
      else {
        size_t reslen;
        char *buff = luaL_prepbuffsize(&b, SIZETIMEFMT);
        s++;  /* skip '%' */
        /* copy specifier to 'cc' */
        s = checkoption(L, s, ct_diff2sz(se - s), cc + 1);
        reslen = strftime(buff, SIZETIMEFMT, cc, stm);
        luaL_addsize(&b, reslen);
      }
    }
    luaL_pushresult(&b);
  }
  return 1;
}


static int os_time (lua_State *L) {
  time_t t;
  if (lua_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);  /* make sure table is at the top */
    ts.tm_year = getfield(L, "year", -1, 1900);
    ts.tm_mon = getfield(L, "month", -1, 1);
    ts.tm_mday = getfield(L, "day", -1, 0);
    ts.tm_hour = getfield(L, "hour", 12, 0);
    ts.tm_min = getfield(L, "min", 0, 0);
    ts.tm_sec = getfield(L, "sec", 0, 0);
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
    setallfields(L, &ts);  /* update fields with normalized values */
  }
  if (t != (time_t)(l_timet)t || t == (time_t)(-1))
    return luaL_error(L,
                  "time result cannot be represented in this installation");
  l_pushtime(L, t);
  return 1;
}


static int os_difftime (lua_State *L) {
  time_t t1 = l_checktime(L, 1);
  time_t t2 = l_checktime(L, 2);
  lua_pushnumber(L, (lua_Number)difftime(t1, t2));
  return 1;
}

/* }=========================================== */


static int os_setlocale (lua_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (lua_State *L) {
  int status;
  if (lua_isboolean(L, 1))
    status = (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)luaL_optinteger(L, 1, EXIT_SUCCESS);
  if (lua_toboolean(L, 2))
    lua_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static int os_sleep (lua_State *L) {
  lua_Number seconds = luaL_checknumber(L, 1);
#ifdef _WIN32
  /* Windows上使用Sleep函数，单位是毫秒 */
  Sleep((DWORD)(seconds * 1000));
#else
  /* Unix上使用nanosleep函数 */
  struct timespec ts;
  ts.tv_sec = (time_t)seconds;
  ts.tv_nsec = (long)((seconds - ts.tv_sec) * 1000000000);
  nanosleep(&ts, NULL);
#endif
  return 0;
}

static int os_usleep (lua_State *L) {
  lua_Integer microseconds = luaL_checkinteger(L, 1);
#ifdef _WIN32
  /* Windows上使用Sleep函数，单位是毫秒，需要转换 */
  Sleep((DWORD)((microseconds + 999) / 1000));
#else
  /* Unix上使用nanosleep函数 */
  struct timespec ts;
  ts.tv_sec = microseconds / 1000000;
  ts.tv_nsec = (microseconds % 1000000) * 1000;
  nanosleep(&ts, NULL);
#endif
  return 0;
}

static int os_getpid (lua_State *L) {
#ifdef _WIN32
  lua_pushinteger(L, GetCurrentProcessId());
#else
  lua_pushinteger(L, getpid());
#endif
  return 1;
}

static int os_randbytes (lua_State *L) {
  lua_Integer n = luaL_checkinteger(L, 1);
  if (n < 1) {
    luaL_argerror(L, 1, "number of bytes must be positive");
  }
  
  char *buffer = (char *)lua_newuserdata(L, n);
  
#ifdef _WIN32
  /* Windows上使用CryptGenRandom生成随机字节 */
  HCRYPTPROV hProv;
  if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
    return luaL_error(L, "cannot acquire crypto context: %lu", GetLastError());
  }
  
  if (!CryptGenRandom(hProv, (DWORD)n, (BYTE *)buffer)) {
    CryptReleaseContext(hProv, 0);
    return luaL_error(L, "cannot generate random bytes: %lu", GetLastError());
  }
  
  CryptReleaseContext(hProv, 0);
#else
  /* Unix上使用/dev/urandom */
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    return luaL_error(L, "cannot open /dev/urandom: %s", strerror(errno));
  }
  
  ssize_t bytes_read = read(fd, buffer, n);
  close(fd);
  
  if (bytes_read != n) {
    return luaL_error(L, "cannot read from /dev/urandom: %s", strerror(errno));
  }
#endif
  
  lua_pushlstring(L, buffer, n);
  return 1;
}

static int os_procname (lua_State *L) {
  char name[128] = {0};
  
#ifdef _WIN32
  /* Windows上使用GetModuleFileName获取进程名 */
  DWORD size = GetModuleFileName(NULL, name, sizeof(name) - 1);
  if (size == 0) {
    return luaL_error(L, "cannot get module file name: %lu", GetLastError());
  }
  
  /* 提取文件名部分 */
  char *last_backslash = strrchr(name, '\\');
  if (last_backslash != NULL) {
    strcpy(name, last_backslash + 1);
  }
#else
  /* Unix上使用/proc/self/comm */
  int fd = open("/proc/self/comm", O_RDONLY);
  if (fd < 0) {
    return luaL_error(L, "cannot open /proc/self/comm: %s", strerror(errno));
  }
  
  ssize_t bytes_read = read(fd, name, sizeof(name) - 1);
  close(fd);
  
  if (bytes_read > 0) {
    // 移除换行符
    if (name[bytes_read - 1] == '\n') {
      name[bytes_read - 1] = '\0';
    }
  }
#endif
  
  lua_pushstring(L, name);
  return 1;
}

static int os_tickcount (lua_State *L) {
#ifdef _WIN32
  /* Windows上使用GetTickCount64获取毫秒数，然后转换为微秒 */
  ULONGLONG tick_ms = GetTickCount64();
  lua_Integer tick = (lua_Integer)tick_ms * 1000LL;
  lua_pushinteger(L, tick);
  return 1;
#else
  /* Unix上使用clock_gettime */
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
    return luaL_error(L, "cannot get clock time: %s", strerror(errno));
  }
  // 转换为微秒
  lua_Integer tick = (lua_Integer)ts.tv_sec * 1000000LL + (lua_Integer)ts.tv_nsec / 1000LL;
  lua_pushinteger(L, tick);
  return 1;
#endif
}

static int os_tracerpid(lua_State *L) { 
#ifdef _WIN32
  /* Windows上不支持tracerpid，返回-1 */
  lua_pushinteger(L, -1); 
  return 1; 
#else
  /* Unix上从/proc/self/status获取 */
  FILE *fp = fopen("/proc/self/status", "r"); 
  if (!fp) { 
    lua_pushnil(L); 
    lua_pushstring(L, strerror(errno)); 
    return 2; 
  } 
  char line[256]; 
  int tracer_pid = -1; 
  while (fgets(line, sizeof(line), fp)) { 
    if (strstr(line, "TracerPid:")) { 
      sscanf(line, "TracerPid:\t%d", &tracer_pid); 
      break; 
    } 
  } 
  fclose(fp); 
  lua_pushinteger(L, tracer_pid); 
  return 1; 
#endif
}

static int os_tid(lua_State *L) { 
#ifdef _WIN32
  /* Windows上使用GetCurrentThreadId获取线程ID */
  DWORD tid = GetCurrentThreadId();
  lua_pushinteger(L, tid); 
  return 1; 
#elif defined(__EMSCRIPTEN__)
  /* Emscripten/WASM环境下返回固定值 */
  lua_pushinteger(L, 1);
  return 1;
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  lua_pushinteger(L, (lua_Integer)tid);
  return 1;
#else
  /* Unix上使用syscall获取线程ID */
  pid_t tid = syscall(SYS_gettid); 
  lua_pushinteger(L, tid); 
  return 1; 
#endif
}

static int os_arg0(lua_State *L) { 
#ifdef _WIN32
  /* Windows上使用GetModuleFileName获取argv[0] */
  char buffer[1024] = {0};
  DWORD size = GetModuleFileName(NULL, buffer, sizeof(buffer) - 1);
  if (size == 0) {
    return luaL_error(L, "cannot get module file name: %lu", GetLastError());
  }
  
  lua_pushstring(L, buffer); 
  return 1; 
#else
  /* Unix上从/proc/self/cmdline获取argv[0] */ 
  int fd = open("/proc/self/cmdline", O_RDONLY); 
  if (fd < 0) { 
    return luaL_error(L, "cannot open /proc/self/cmdline: %s", strerror(errno)); 
  } 
  
  char buffer[1024] = {0}; 
  ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1); 
  close(fd); 
  
  if (bytes_read < 0) { 
    return luaL_error(L, "cannot read from /proc/self/cmdline: %s", strerror(errno)); 
  } 
  
  /* /proc/self/cmdline中的参数以null字符分隔，第一个参数就是argv[0] */ 
  lua_pushstring(L, buffer); 
  return 1; 
#endif
}

static int os_libs(lua_State *L) {
#ifdef _WIN32
  /* Windows上不支持获取共享库列表，返回空表 */
  lua_newtable(L);
  return 1;
#else
  /* Unix上从/proc/self/maps获取已加载的共享库列表 */
  FILE *fp = fopen("/proc/self/maps", "r");
  if (!fp) {
    return luaL_error(L, "cannot open /proc/self/maps: %s", strerror(errno));
  }
  
  lua_newtable(L);
  char line[256];
  int index = 1;
  while (fgets(line, sizeof(line), fp)) {
    /* 查找共享库路径，格式如：7f00000000-7f00010000 r-xp 00000000 08:01 1234567 /usr/lib/libc.so.6 */
    char *path = strchr(line, '/');
    if (path) {
      /* 移除换行符 */
      char *newline = strchr(path, '\n');
      if (newline) {
        *newline = '\0';
      }
      lua_pushstring(L, path);
      lua_rawseti(L, -2, index++);
    }
  }
  
  fclose(fp);
  return 1;
#endif
}

static int os_stacksize(lua_State *L) {
#ifdef _WIN32
  /* Windows上不支持获取栈大小，返回0 */
  lua_pushinteger(L, 0);
  return 1;
#else
  /* Unix上从/proc/self/stat获取第28个字段（stack size） */
  FILE *fp = fopen("/proc/self/stat", "r");
  if (!fp) {
    return luaL_error(L, "cannot open /proc/self/stat: %s", strerror(errno));
  }
  
  char buffer[1024] = {0};
  if (fgets(buffer, sizeof(buffer), fp) == NULL) {
    fclose(fp);
    return luaL_error(L, "cannot read from /proc/self/stat: %s", strerror(errno));
  }
  
  fclose(fp);
  
  /* 解析第28个字段 */
  char *token;
  int field = 0;
  long stacksize = 0;
  
  token = strtok(buffer, " ");
  while (token != NULL) {
    field++;
    if (field == 28) {
      stacksize = atol(token);
      break;
    }
    token = strtok(NULL, " ");
  }
  
  lua_pushinteger(L, stacksize);
  return 1;
#endif
}

static int os_seccomp(lua_State *L) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(__APPLE__)
  /* Windows/Emscripten/macOS上不支持seccomp */
  return luaL_error(L, "seccomp is not supported on this platform");
#else
  /* Unix上的seccomp实现 */
  int mode = luaL_optinteger(L, 1, 0);
  
  if (mode == 0) {
    /* 查询当前 Seccomp 状态 */
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) {
      return luaL_error(L, "cannot open /proc/self/status: %s", strerror(errno));
    }
    
    char line[256];
    int seccomp = -1;
    while (fgets(line, sizeof(line), fp)) {
      if (strstr(line, "Seccomp:")) {
        sscanf(line, "Seccomp:\t%d", &seccomp);
        break;
      }
    }
    
    fclose(fp);
    lua_pushinteger(L, seccomp);
    return 1;
  } else if (mode == 2) {
    /* 启用 seccomp 严格模式 (SECCOMP_SET_MODE_STRICT) */
    #if defined(SECCOMP_SET_MODE_STRICT)
      int ret = prctl(SECCOMP_SET_MODE_STRICT, 0, NULL, 0, 0);
      if (ret == -1) {
        lua_pushnil(L);
        lua_pushinteger(L, errno);
        return 2;
      }
      lua_pushinteger(L, ret);
      return 1;
    #elif defined(__NR_seccomp)
      /* 通过 syscall 调用 seccomp */
      int ret = syscall(__NR_seccomp, SECCOMP_SET_MODE_STRICT, 0, NULL);
      if (ret == -1) {
        lua_pushnil(L);
        lua_pushinteger(L, errno);
        return 2;
      }
      lua_pushinteger(L, ret);
      return 1;
    #else
      return luaL_error(L, "seccomp is not supported on this system");
    #endif
  } else if (mode == 1) {
    /* 启用 seccomp 过滤模式 (SECCOMP_MODE_FILTER) */
    #if defined(SECCOMP_MODE_FILTER)
      return luaL_error(L, "filter mode requires a bpf program, use mode 2 for strict mode");
    #else
      return luaL_error(L, "filter mode is not supported on this system");
    #endif
  } else {
    return luaL_argerror(L, 1, "invalid seccomp mode (0=query, 1=filter, 2=strict)");
  }
#endif
}

static int os_mtime(lua_State *L) {
#ifdef _WIN32
  /* Windows上获取可执行文件的修改时间 */
  char buffer[1024] = {0};
  DWORD size = GetModuleFileName(NULL, buffer, sizeof(buffer) - 1);
  if (size == 0) {
    return luaL_error(L, "cannot get module file name: %lu", GetLastError());
  }
  
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(buffer, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return luaL_error(L, "cannot find file: %lu", GetLastError());
  }
  
  FindClose(hFind);
  
  /* 将FILETIME转换为Unix时间戳 */
  FILETIME ft = findData.ftLastWriteTime;
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  lua_Integer mtime = (uli.QuadPart - 116444736000000000LL) / 10000000LL;
  
  lua_pushinteger(L, mtime);
  return 1;
#else
  /* Unix上获取/proc/self/exe的修改时间 */
  struct stat st;
  if (lstat("/proc/self/exe", &st) == -1) {
    return luaL_error(L, "cannot stat /proc/self/exe: %s", strerror(errno));
  }
  
  lua_pushinteger(L, (lua_Integer)st.st_mtime);
  return 1;
#endif
}

static int os_syscall(lua_State *L) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
  /* Windows/Emscripten上不支持syscall */
  return luaL_error(L, "syscall is not supported on this platform");
#else
  /* Unix上的syscall实现 */
  /* 极简封装系统调用入口 */
  long syscall_num = luaL_checkinteger(L, 1);
  long arg1 = luaL_optinteger(L, 2, 0);
  long arg2 = luaL_optinteger(L, 3, 0);
  long arg3 = luaL_optinteger(L, 4, 0);
  long arg4 = luaL_optinteger(L, 5, 0);
  long arg5 = luaL_optinteger(L, 6, 0);
  long arg6 = luaL_optinteger(L, 7, 0);
  
  long result = syscall(syscall_num, arg1, arg2, arg3, arg4, arg5, arg6);
  if (result == -1) {
    lua_pushnil(L);
    lua_pushinteger(L, errno);
    return 2;
  }
  
  lua_pushinteger(L, result);
  return 1;
#endif
}

static int os_aname(lua_State *L) {
#ifdef _WIN32
  /* Windows上返回"Windows" */
  lua_pushstring(L, "Windows");
  return 1;
#elif defined(__APPLE__)
  lua_pushstring(L, "Darwin");
  return 1;
#else
  /* Unix上从/proc/sys/kernel/ostype获取系统代号 */
  FILE *fp = fopen("/proc/sys/kernel/ostype", "r");
  if (!fp) {
    return luaL_error(L, "cannot open /proc/sys/kernel/ostype: %s", strerror(errno));
  }
  
  char buffer[128] = {0};
  if (fgets(buffer, sizeof(buffer), fp) == NULL) {
    fclose(fp);
    return luaL_error(L, "cannot read from /proc/sys/kernel/ostype: %s", strerror(errno));
  }
  
  fclose(fp);
  
  /* 移除换行符 */
  char *newline = strchr(buffer, '\n');
  if (newline) {
    *newline = '\0';
  }
  
  lua_pushstring(L, buffer);
  return 1;
#endif
}

static int os_fsuid(lua_State *L) {
#ifdef _WIN32
  /* Windows上不支持文件系统用户ID，返回0和true */
  lua_pushinteger(L, 0);
  lua_pushboolean(L, 1);
  return 2;
#else
  /* Unix上获取进程的文件系统用户ID */
  uid_t ruid = getuid();
  uid_t euid = geteuid();
  lua_pushinteger(L, euid);
  lua_pushboolean(L, ruid == euid);
  return 2;
#endif
}

static int os_getppid(lua_State *L) {
#ifdef _WIN32
  /* Windows上使用CreateToolhelp32Snapshot获取父进程ID */
  DWORD ppid = 0;
  DWORD pid = GetCurrentProcessId();
  
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(hSnapshot, &pe32)) {
      do {
        if (pe32.th32ProcessID == pid) {
          ppid = pe32.th32ParentProcessID;
          break;
        }
      } while (Process32Next(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
  }
  
  lua_pushinteger(L, ppid);
  return 1;
#else
  /* Unix上使用getppid获取父进程ID */
  pid_t ppid = getppid();
  lua_pushinteger(L, ppid);
  return 1;
#endif
}

static int os_prctl(lua_State *L) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__) || defined(__APPLE__)
  /* Windows/Emscripten/macOS上不支持prctl */
  return luaL_error(L, "prctl is not supported on this platform");
#else
  /**
   * 封装 prctl 系统调用
   * @param L Lua 状态机
   * @return int 返回值数量
   */
  int option = luaL_checkinteger(L, 1);
  
  int result;
  
  /* PR_SET_NAME (15) - 设置进程名，需要字符串参数 */
  if (option == 15) {
    const char *name = luaL_checkstring(L, 2);
    
    /* 进程名长度限制（通常为 15 字符，不含 null 终止符） */
    #define PR_SET_NAME_MAX_LEN 15
    char name_buf[PR_SET_NAME_MAX_LEN + 1];
    strncpy(name_buf, name, PR_SET_NAME_MAX_LEN);
    name_buf[PR_SET_NAME_MAX_LEN] = '\0';
    
    result = prctl(option, name_buf, 0, 0, 0);
  }
  /* PR_GET_NAME (16) - 获取进程名，返回字符串 */
  else if (option == 16) {
    char name_buf[17];
    result = prctl(option, name_buf, 0, 0, 0);
    if (result == 0) {
      lua_pushstring(L, name_buf);
      return 1;
    }
  }
  /* 其他命令，参数为整数 */
  else {
    unsigned long arg2 = luaL_optinteger(L, 2, 0);
    unsigned long arg3 = luaL_optinteger(L, 3, 0);
    unsigned long arg4 = luaL_optinteger(L, 4, 0);
    unsigned long arg5 = luaL_optinteger(L, 5, 0);
    
    result = prctl(option, arg2, arg3, arg4, arg5);
  }
  
  if (result == -1) {
    lua_pushnil(L);
    lua_pushinteger(L, errno);
    return 2;
  }
  
  lua_pushinteger(L, result);
  return 1;
#endif
}


static const luaL_Reg syslib[] = {
  {"aname",     os_aname},
  {"arg0",      os_arg0},
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"fsuid",     os_fsuid},
  {"getenv",    os_getenv},
  {"getpid",    os_getpid},
  {"getppid",   os_getppid},
  {"libs",      os_libs},
  {"mtime",     os_mtime},
  {"prctl",     os_prctl},
  {"procname",  os_procname},
  {"randbytes", os_randbytes},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"seccomp",   os_seccomp},
  {"setlocale", os_setlocale},
  {"sleep",     os_sleep},
  {"stacksize", os_stacksize},
  {"syscall",   os_syscall},
  {"tickcount", os_tickcount},
  {"tid",       os_tid},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {"tracerpid", os_tracerpid},
  {"usleep",    os_usleep},
  {NULL, NULL}
};

/* }=========================================== */



LUAMOD_API int luaopen_os (lua_State *L) {
  luaL_newlib(L, syslib);
  return 1;
}

