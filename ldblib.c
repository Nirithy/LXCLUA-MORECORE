/*
** $Id: ldblib.c $
** Interface from Lua to its debug API
** See Copyright Notice in lua.h
*/

#define ldblib_c
#define LUA_LIB

#include "lprefix.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lobject.h"
#include "lstate.h"


/*
** The hook table at registry[HOOKKEY] maps threads to their current
** hook function.
*/
static const char *const HOOKKEY = "_HOOKKEY";

/*
** The breakpoint table at registry[BREAKPOINTKEY] maps threads to their current
** hook function.
*/
static const char *const BREAKPOINTKEY = "_BREAKPOINTKEY";

/*
** Debug state for controlling execution (step, continue, etc.)
** Stored as a table in registry[DEBUGSTATEKEY] with fields:
** - mode: 0=run, 1=step, 2=next, 3=finish
** - target_level: target stack level for 'finish' command
*/
static const char *const DEBUGSTATEKEY = "_DEBUGSTATEKEY";

/*
** Debug output callback function at registry[DEBUGOUTPUTKEY]
** If set, this function will be called when a breakpoint is hit
*/
static const char *const DEBUGOUTPUTKEY = "_DEBUGOUTPUTKEY";


/*
** If L1 != L, L1 can be in any state, and therefore there are no
** guarantees about its stack space; any push in L1 must be
** checked.
*/
static void checkstack (lua_State *L, lua_State *L1, int n) {
  if (l_unlikely(L != L1 && !lua_checkstack(L1, n)))
    luaL_error(L, "stack overflow");
}


static int db_getregistry (lua_State *L) {
  lua_pushvalue(L, LUA_REGISTRYINDEX);
  return 1;
}


static int db_getmetatable (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    lua_pushnil(L);  /* no metatable */
  }
  return 1;
}


static int db_setmetatable (lua_State *L) {
  int t = lua_type(L, 2);
  luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;  /* return 1st argument */
}


static int db_getuservalue (lua_State *L) {
  int n = (int)luaL_optinteger(L, 2, 1);
  if (lua_type(L, 1) != LUA_TUSERDATA)
    luaL_pushfail(L);
  else if (lua_getiuservalue(L, 1, n) != LUA_TNONE) {
    lua_pushboolean(L, 1);
    return 2;
  }
  return 1;
}


static int db_setuservalue (lua_State *L) {
  int n = (int)luaL_optinteger(L, 3, 1);
  luaL_checktype(L, 1, LUA_TUSERDATA);
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  if (!lua_setiuservalue(L, 1, n))
    luaL_pushfail(L);
  return 1;
}


/*
** Auxiliary function used by several library functions: check for
** an optional thread as function's first argument and set 'arg' with
** 1 if this argument is present (so that functions can skip it to
** access their other arguments)
*/
static lua_State *getthread (lua_State *L, int *arg) {
  if (lua_isthread(L, 1)) {
    *arg = 1;
    return lua_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;  /* function will operate over current thread */
  }
}


/*
** Variations of 'lua_settable', used by 'db_getinfo' to put results
** from 'lua_getinfo' into result table. Key is always a string;
** value can be a string, an int, or a boolean.
*/
static void settabss (lua_State *L, const char *k, const char *v) {
  lua_pushstring(L, v);
  lua_setfield(L, -2, k);
}

static void settabsi (lua_State *L, const char *k, int v) {
  lua_pushinteger(L, v);
  lua_setfield(L, -2, k);
}

static void settabsb (lua_State *L, const char *k, int v) {
  lua_pushboolean(L, v);
  lua_setfield(L, -2, k);
}


/*
** In function 'db_getinfo', the call to 'lua_getinfo' may push
** results on the stack; later it creates the result table to put
** these objects. Function 'treatstackoption' puts the result from
** 'lua_getinfo' on top of the result table so that it can call
** 'lua_setfield'.
*/
static void treatstackoption (lua_State *L, lua_State *L1, const char *fname) {
  if (L == L1)
    lua_rotate(L, -2, 1);  /* exchange object and table */
  else
    lua_xmove(L1, L, 1);  /* move object to the "main" stack */
  lua_setfield(L, -2, fname);  /* put object into table */
}


/*
** Calls 'lua_getinfo' and collects all results in a new table.
** L1 needs stack space for an optional input (function) plus
** two optional outputs (function and line table) from function
** 'lua_getinfo'.
*/
static int db_getinfo (lua_State *L) {
  lua_Debug ar;
  int arg;
  lua_State *L1 = getthread(L, &arg);
  const char *options = luaL_optstring(L, arg+2, "flnSrtuh");
  checkstack(L, L1, 3);
  luaL_argcheck(L, options[0] != '>', arg + 2, "invalid option '>'");
  if (lua_isfunction(L, arg + 1)) {  /* info about a function? */
    options = lua_pushfstring(L, ">%s", options);  /* add '>' to 'options' */
    lua_pushvalue(L, arg + 1);  /* move function to 'L1' stack */
    lua_xmove(L, L1, 1);
  }
  else if (lua_isuserdata(L, arg + 1) || lua_islightuserdata(L, arg + 1)) {
    lua_newtable(L);
    settabsb(L, "func", 0);
    if (strchr(options, 'f')) {
      lua_pushnil(L);
      lua_setfield(L, -2, "func");
    }
    if (strchr(options, 'h'))
      settabsb(L, "ishotfixed", 0);
    return 1;
  }
  else {  /* stack level */
    if (!lua_getstack(L1, (int)luaL_checkinteger(L, arg + 1), &ar)) {
      luaL_pushfail(L);  /* level out of range */
      return 1;
    }
  }
  if (!lua_getinfo(L1, options, &ar))
    return luaL_argerror(L, arg+2, "invalid option");
  lua_newtable(L);  /* table to collect results */
  if (strchr(options, 'S')) {
    lua_pushlstring(L, ar.source, ar.srclen);
    lua_setfield(L, -2, "source");
    settabss(L, "short_src", ar.short_src);
    settabsi(L, "linedefined", ar.linedefined);
    settabsi(L, "lastlinedefined", ar.lastlinedefined);
    settabss(L, "what", ar.what);
  }
  if (strchr(options, 'l'))
    settabsi(L, "currentline", ar.currentline);
  if (strchr(options, 'u')) {
    settabsi(L, "nups", ar.nups);
    settabsi(L, "nparams", ar.nparams);
    settabsb(L, "isvararg", ar.isvararg);
  }
  if (strchr(options, 'n')) {
    settabss(L, "name", ar.name);
    settabss(L, "namewhat", ar.namewhat);
  }
  if (strchr(options, 'r')) {
    settabsi(L, "ftransfer", ar.ftransfer);
    settabsi(L, "ntransfer", ar.ntransfer);
  }
  if (strchr(options, 't'))
    settabsb(L, "istailcall", ar.istailcall);
  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");
  if (strchr(options, 'h'))
    settabsb(L, "ishotfixed", ar.ishotfixed);
  if (strchr(options, 'k'))
    settabsb(L, "islocked", ar.islocked);
  if (strchr(options, 'T'))
    settabsb(L, "istampered", ar.istampered);
  return 1;  /* return table */
}


static int db_getlocal (lua_State *L) {
  int arg;
  lua_State *L1 = getthread(L, &arg);
  int nvar = (int)luaL_checkinteger(L, arg + 2);  /* local-variable index */
  if (lua_isfunction(L, arg + 1)) {  /* function argument? */
    lua_pushvalue(L, arg + 1);  /* push function */
    lua_pushstring(L, lua_getlocal(L, NULL, nvar));  /* push local name */
    return 1;  /* return only name (there is no value) */
  }
  else {  /* stack-level argument */
    lua_Debug ar;
    const char *name;
    int level = (int)luaL_checkinteger(L, arg + 1);
    if (l_unlikely(!lua_getstack(L1, level, &ar)))  /* out of range? */
      return luaL_argerror(L, arg+1, "level out of range");
    checkstack(L, L1, 1);
    name = lua_getlocal(L1, &ar, nvar);
    if (name) {
      lua_xmove(L1, L, 1);  /* move local value */
      lua_pushstring(L, name);  /* push name */
      lua_rotate(L, -2, 1);  /* re-order */
      return 2;
    }
    else {
      luaL_pushfail(L);  /* no name (nor value) */
      return 1;
    }
  }
}


static int db_setlocal (lua_State *L) {
  int arg;
  const char *name;
  lua_State *L1 = getthread(L, &arg);
  lua_Debug ar;
  int level = (int)luaL_checkinteger(L, arg + 1);
  int nvar = (int)luaL_checkinteger(L, arg + 2);
  if (l_unlikely(!lua_getstack(L1, level, &ar)))  /* out of range? */
    return luaL_argerror(L, arg+1, "level out of range");
  luaL_checkany(L, arg+3);
  lua_settop(L, arg+3);
  checkstack(L, L1, 1);
  lua_xmove(L, L1, 1);
  name = lua_setlocal(L1, &ar, nvar);
  if (name == NULL)
    lua_pop(L1, 1);  /* pop value (if not popped by 'lua_setlocal') */
  lua_pushstring(L, name);
  return 1;
}


/*
** get (if 'get' is true) or set an upvalue from a closure
*/
static int auxupvalue (lua_State *L, int get) {
  const char *name;
  int n = (int)luaL_checkinteger(L, 2);  /* upvalue index */
  luaL_checktype(L, 1, LUA_TFUNCTION);  /* closure */
  name = get ? lua_getupvalue(L, 1, n) : lua_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  lua_pushstring(L, name);
  lua_insert(L, -(get+1));  /* no-op if get is false */
  return get + 1;
}


static int db_getupvalue (lua_State *L) {
  return auxupvalue(L, 1);
}


static int db_setupvalue (lua_State *L) {
  luaL_checkany(L, 3);
  return auxupvalue(L, 0);
}


/*
** Check whether a given upvalue from a given closure exists and
** returns its index
*/
static void *checkupval (lua_State *L, int argf, int argnup, int *pnup) {
  void *id;
  int nup = (int)luaL_checkinteger(L, argnup);  /* upvalue index */
  luaL_checktype(L, argf, LUA_TFUNCTION);  /* closure */
  id = lua_upvalueid(L, argf, nup);
  if (pnup) {
    luaL_argcheck(L, id != NULL, argnup, "invalid upvalue index");
    *pnup = nup;
  }
  return id;
}


static int db_upvalueid (lua_State *L) {
  void *id = checkupval(L, 1, 2, NULL);
  if (id != NULL)
    lua_pushlightuserdata(L, id);
  else
    luaL_pushfail(L);
  return 1;
}


static int db_upvaluejoin (lua_State *L) {
  int n1, n2;
  checkupval(L, 1, 2, &n1);
  checkupval(L, 3, 4, &n2);
  luaL_argcheck(L, !lua_iscfunction(L, 1), 1, "Lua function expected");
  luaL_argcheck(L, !lua_iscfunction(L, 3), 3, "Lua function expected");
  lua_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


/*
** Call hook function registered at hook table for the current
** thread (if there is one)
*/
static void hookf (lua_State *L, lua_Debug *ar) {
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  
  /* 检查断点和调试状态（仅在线事件时） */
  if (ar->event == LUA_HOOKLINE && ar->currentline >= 0) {
    int should_stop = 0;
    int top = lua_gettop(L);  /* 保存栈顶位置 */
    char source_key[512];
    const char *source;
    
    /* 获取完整的调试信息 */
    lua_getinfo(L, "S", ar);
    
    source = ar->source ? ar->source : "";
    
    /* 移除 @ 前缀 */
    if (source[0] == '@') {
      source++;
    }
    
    /* 只使用文件名部分，移除路径 */
    const char *filename = source;
    const char *last_sep = strrchr(source, '/');
    if (last_sep) {
      filename = last_sep + 1;
    }
    last_sep = strrchr(source, '\\');
    if (last_sep && last_sep + 1 > filename) {
      filename = last_sep + 1;
    }
    
    snprintf(source_key, sizeof(source_key), "%s:%d", filename, ar->currentline);
    
    /* 检查断点 */
    lua_getfield(L, LUA_REGISTRYINDEX, BREAKPOINTKEY);
    if (!lua_isnil(L, -1)) {
      lua_getfield(L, -1, source_key);
      if (!lua_isnil(L, -1) && lua_istable(L, -1)) {
        /* 检查断点是否启用 */
        lua_getfield(L, -1, "enabled");
        if (lua_toboolean(L, -1)) {
          should_stop = 1;
        }
        lua_pop(L, 1);  /* 移除 enabled */
      }
      lua_pop(L, 1);  /* 移除断点条目 */
    }
    lua_pop(L, 1);  /* 移除断点表 */
    
    /* 检查调试模式 (step/next/finish) */
    if (!should_stop) {
      lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
      if (!lua_isnil(L, -1) && lua_istable(L, -1)) {
        lua_getfield(L, -1, "mode");
        int mode = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        
        if (mode == 1) {
          /* step 模式：每行都停 */
          should_stop = 1;
        } else if (mode == 2) {
          /* next 模式 */
          lua_getfield(L, -1, "target_level");
          int target_level = (int)lua_tointeger(L, -1);
          lua_pop(L, 1);
          if (target_level == 0) {
            should_stop = 1;
          }
        } else if (mode == 3) {
          /* finish 模式 */
          lua_getfield(L, -1, "target_level");
          int target_level = (int)lua_tointeger(L, -1);
          lua_pop(L, 1);
          if (target_level == 0) {
            should_stop = 1;
            /* 重置模式 */
            lua_pushinteger(L, 0);
            lua_setfield(L, -2, "mode");
          }
        }
      }
      lua_pop(L, 1);  /* 移除 debug state */
    }
    
    /* 如果需要停止，输出断点信息 */
    if (should_stop) {
      const char *short_src = ar->short_src ? ar->short_src : source;
      
      /* 检查是否有自定义的输出回调函数 */
      lua_getfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
      if (!lua_isnil(L, -1) && lua_isfunction(L, -1)) {
        /* 调用自定义输出函数 */
        lua_pushstring(L, "breakpoint");
        lua_pushstring(L, short_src);
        lua_pushinteger(L, ar->currentline);
        lua_pcall(L, 3, 0, 0);  /* 调用函数，忽略错误 */
      } else {
        /* 使用默认输出 */
        fprintf(stderr, "Breakpoint at %s:%d\n", short_src, ar->currentline);
      }
      lua_pop(L, 1);  /* 弹出输出函数或 nil */
    }
    
    /* 恢复栈顶 */
    lua_settop(L, top);
  }
  
  /* 调用用户注册的钩子函数 */
  lua_getfield(L, LUA_REGISTRYINDEX, HOOKKEY);
  lua_pushthread(L);
  if (lua_rawget(L, -2) == LUA_TFUNCTION) {  /* is there a hook function? */
    lua_pushstring(L, hooknames[(int)ar->event]);  /* push event name */
    if (ar->currentline >= 0)
      lua_pushinteger(L, ar->currentline);  /* push current line */
    else lua_pushnil(L);
    lua_assert(lua_getinfo(L, "lS", ar));
    lua_call(L, 2, 0);  /* call hook function */
  }
  lua_pop(L, 1);  /* 弹出钩子表 */
}


/*
** Convert a string mask (for 'sethook') into a bit mask
*/
static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
  if (strchr(smask, 'r')) mask |= LUA_MASKRET;
  if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
  if (count > 0) mask |= LUA_MASKCOUNT;
  return mask;
}


/*
** Convert a bit mask (for 'gethook') into a string mask
*/
static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & LUA_MASKCALL) smask[i++] = 'c';
  if (mask & LUA_MASKRET) smask[i++] = 'r';
  if (mask & LUA_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (lua_State *L) {
  int arg, mask, count;
  lua_Hook func;
  lua_State *L1 = getthread(L, &arg);
  if (lua_isnoneornil(L, arg+1)) {  /* no hook? */
    lua_settop(L, arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = luaL_checkstring(L, arg+2);
    luaL_checktype(L, arg+1, LUA_TFUNCTION);
    count = (int)luaL_optinteger(L, arg + 3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  if (!luaL_getsubtable(L, LUA_REGISTRYINDEX, HOOKKEY)) {
    /* table just created; initialize it */
    lua_pushliteral(L, "k");
    lua_setfield(L, -2, "__mode");  /** hooktable.__mode = "k" */
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);  /* metatable(hooktable) = hooktable */
  }
  checkstack(L, L1, 1);
  lua_pushthread(L1); lua_xmove(L1, L, 1);  /* key (thread) */
  lua_pushvalue(L, arg + 1);  /* value (hook function) */
  lua_rawset(L, -3);  /* hooktable[L1] = new Lua hook */
  lua_sethook(L1, func, mask, count);
  return 0;
}


static int db_gethook (lua_State *L) {
  int arg;
  lua_State *L1 = getthread(L, &arg);
  char buff[5];
  int mask = lua_gethookmask(L1);
  lua_Hook hook = lua_gethook(L1);
  if (hook == NULL) {  /* no hook? */
    luaL_pushfail(L);
    return 1;
  }
  else if (hook != hookf)  /* external hook? */
    lua_pushliteral(L, "external hook");
  else {  /* hook table must exist */
    lua_getfield(L, LUA_REGISTRYINDEX, HOOKKEY);
    checkstack(L, L1, 1);
    lua_pushthread(L1); lua_xmove(L1, L, 1);
    lua_rawget(L, -2);   /* 1st result = hooktable[L1] */
    lua_remove(L, -2);  /* remove hook table */
  }
  lua_pushstring(L, unmakemask(mask, buff));  /* 2nd result = mask */
  lua_pushinteger(L, lua_gethookcount(L1));  /* 3rd result = count */
  return 3;
}


static int db_debug (lua_State *L) {
  for (;;) {
    char buffer[250];
    lua_writestringerror("%s", "lua_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == NULL ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (luaL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        lua_pcall(L, 0, 0, 0))
      lua_writestringerror("%s\n", luaL_tolstring(L, -1, NULL));
    lua_settop(L, 0);  /* remove eventual returns */
  }
}


static int db_traceback (lua_State *L) {
  int arg;
  lua_State *L1 = getthread(L, &arg);
  const char *msg = lua_tostring(L, arg + 1);
  if (msg == NULL && !lua_isnoneornil(L, arg + 1))  /* non-string 'msg'? */
    lua_pushvalue(L, arg + 1);  /* return it untouched */
  else {
    int level = (int)luaL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    luaL_traceback(L, L1, msg, level);
  }
  return 1;
}


/*
** Breakpoint Management Functions
*/

/* Helper: Get or create breakpoint table */
static void ensure_breakpoint_table (lua_State *L) {
  luaL_getsubtable(L, LUA_REGISTRYINDEX, BREAKPOINTKEY);
}

/* Helper: Get or create debug state table */
static void ensure_debug_state (lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushinteger(L, 0);  /* mode */
    lua_setfield(L, -2, "mode");
    lua_pushinteger(L, 0);  /* target_level */
    lua_setfield(L, -2, "target_level");
    lua_setfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  } else {
    lua_pop(L, 1);
  }
}

/*
** debug.setbreakpoint(source, line, [condition])
** 设置断点
** @param source 源文件路径或函数
** @param line 行号
** @param condition 可选的断点条件（Lua 代码字符串）
** @return 返回断点 ID
*/
static int db_setbreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  const char *condition = luaL_optstring(L, 3, NULL);
  
  ensure_breakpoint_table(L);
  
  /* 只使用文件名部分，移除路径 */
  const char *filename = source;
  const char *last_sep = strrchr(source, '/');
  if (last_sep) {
    filename = last_sep + 1;
  }
  last_sep = strrchr(source, '\\');
  if (last_sep && last_sep + 1 > filename) {
    filename = last_sep + 1;
  }
  
  /* 创建断点键：filename:line */
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", filename, line);
  
  /* 检查是否已存在断点 */
  lua_getfield(L, -1, key);
  int exists = !lua_isnil(L, -1);
  lua_pop(L, 1);
  
  /* 创建断点表 */
  lua_newtable(L);
  lua_pushstring(L, filename);
  lua_setfield(L, -2, "source");
  lua_pushinteger(L, line);
  lua_setfield(L, -2, "line");
  lua_pushboolean(L, 1);  /* enabled */
  lua_setfield(L, -2, "enabled");
  if (condition) {
    lua_pushstring(L, condition);
    lua_setfield(L, -2, "condition");
  }
  lua_pushboolean(L, exists);  /* 标记是否是更新 */
  lua_setfield(L, -2, "exists");
  
  /* 存入断点表 */
  lua_setfield(L, -2, key);
  
  return 1;
}

/*
** debug.removebreakpoint(source, line)
** 移除断点
** @param source 源文件路径
** @param line 行号
** @return 返回是否成功移除
*/
static int db_removebreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  
  ensure_breakpoint_table(L);
  
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", source, line);
  
  lua_getfield(L, -1, key);
  int exists = !lua_isnil(L, -1);
  lua_pop(L, 1);
  
  if (exists) {
    lua_pushnil(L);
    lua_setfield(L, -2, key);
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  
  return 1;
}

/*
** debug.getbreakpoints()
** 获取所有断点
** @return 返回包含所有断点的表
*/
static int db_getbreakpoints (lua_State *L) {
  ensure_breakpoint_table(L);
  
  lua_newtable(L);
  int idx = 1;
  
  /* 遍历断点表 */
  lua_pushnil(L);  /* 第一个键 */
  while (lua_next(L, -2) != 0) {
    /* key 是断点键，value 是断点表 */
    if (lua_istable(L, -1)) {
      lua_pushinteger(L, idx);
      lua_pushvalue(L, -2);  /* value */
      lua_settable(L, -4);
      idx++;
    }
    lua_pop(L, 1);  /* 移除 value，保留 key */
  }
  
  return 1;
}

/*
** debug.enablebreakpoint(source, line, enable)
** 启用/禁用断点
** @param source 源文件路径
** @param line 行号
** @param enable 是否启用
** @return 返回是否成功
*/
static int db_enablebreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  int enable = lua_toboolean(L, 3);
  
  ensure_breakpoint_table(L);
  
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", source, line);
  
  lua_getfield(L, -1, key);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 0);
    return 1;  /* 断点不存在 */
  }
  
  lua_pushboolean(L, enable);
  lua_setfield(L, -2, "enabled");
  lua_pop(L, 1);
  
  lua_pushboolean(L, 1);
  return 1;
}

/*
** debug.clearbreakpoints()
** 清除所有断点
** @return 返回清除的断点数量
*/
static int db_clearbreakpoints (lua_State *L) {
  ensure_breakpoint_table(L);
  
  int count = 0;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);  /* 移除 value */
    lua_pushnil(L);
    lua_settable(L, -3);  /* 删除键 */
    count++;
  }
  
  lua_pushinteger(L, count);
  return 1;
}

/*
** debug.continue()
** 继续执行直到下一个断点
*/
static int db_continue (lua_State *L) {
  ensure_debug_state(L);
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "mode");
  lua_pop(L, 1);
  lua_pushstring(L, "continue");
  return 1;
}

/*
** debug.step()
** 单步执行（进入函数）
*/
static int db_step (lua_State *L) {
  ensure_debug_state(L);
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "mode");
  lua_pop(L, 1);
  lua_pushstring(L, "step");
  return 1;
}

/*
** debug.next()
** 单步执行（不进入函数）
*/
static int db_next (lua_State *L) {
  ensure_debug_state(L);
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "mode");
  lua_pushinteger(L, lua_gettop(L));
  lua_setfield(L, -2, "target_level");
  lua_pop(L, 1);
  lua_pushstring(L, "next");
  return 1;
}

/*
** debug.finish()
** 执行到当前函数返回
*/
static int db_finish (lua_State *L) {
  ensure_debug_state(L);
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "mode");
  lua_pushinteger(L, lua_gettop(L));
  lua_setfield(L, -2, "target_level");
  lua_pop(L, 1);
  lua_pushstring(L, "finish");
  return 1;
}

/*
** debug.setoutputcallback(callback)
** 设置调试输出回调函数
** @param callback 回调函数，接收 (event, source, line) 三个参数
** @return 之前的回调函数（如果有）
*/
static int db_setoutputcallback (lua_State *L) {
  /* 保存旧的回调函数 */
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
  
  /* 设置新的回调函数 */
  if (lua_isfunction(L, 1)) {
    lua_pushvalue(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
  } else {
    /* 如果参数不是函数，则清除回调 */
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
  }
  
  /* 返回旧的回调函数 */
  return 1;
}

/*
** debug.getoutputcallback()
** 获取当前的调试输出回调函数
** @return 当前的回调函数
*/
static int db_getoutputcallback (lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
  return 1;
}


static int db_setcstacklimit (lua_State *L) {
  int limit = (int)luaL_checkinteger(L, 1);
  int res = lua_setcstacklimit(L, limit);
  lua_pushinteger(L, res);
  return 1;
}

/**
 * @brief 热修复函数，允许在运行时替换函数实现
 *
 * @param L lua状态机
 * @return 返回被替换的旧函数
 *
 * 该函数实现动态热修复功能：
 * 1. 支持两种调用方式: debug.hotfix("funcName", newFunc) 或 debug.hotfix(oldFunc, newFunc)
 * 2. 新函数的函数体会被替换到旧函数中
 * 3. 旧函数的上值引用会被保留
 * 4. 返回旧函数引用，方便回滚
 */
static int db_hotfix (lua_State *L) {
  int oldidx, newidx;
  int oldnup, newnup;
  const char *upname;
  
  /* 支持两种调用方式 */
  if (lua_type(L, 1) == LUA_TSTRING) {
    const char *name = lua_tostring(L, 1);
    lua_pushglobaltable(L);
    lua_pushstring(L, name);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
      return luaL_error(L, "global function '%s' not found", name);
    }
    if (!lua_isfunction(L, -1)) {
      return luaL_error(L, "'%s' is not a function", name);
    }
    oldidx = -1;
  }
  else if (lua_isfunction(L, 1)) {
    oldidx = 1;
  }
  else {
    return luaL_error(L, "argument #1 must be string or function");
  }
  
  luaL_checktype(L, 2, LUA_TFUNCTION);
  newidx = 2;
  
  /* 检查上值数量 */
  oldnup = 0;
  while ((upname = lua_getupvalue(L, oldidx, oldnup + 1)) != NULL) {
    oldnup++;
    lua_pop(L, 1);
  }
  
  newnup = 0;
  while ((upname = lua_getupvalue(L, newidx, newnup + 1)) != NULL) {
    newnup++;
    lua_pop(L, 1);
  }
  
  if (oldnup != newnup) {
    return luaL_error(L, "upvalue count mismatch: old=%d, new=%d", oldnup, newnup);
  }
  
  /* 保存旧函数引用用于返回 */
  lua_pushvalue(L, oldidx);
  
  /* 执行热修复 */
  luaB_hotfix(L, oldidx, newidx);
  
  /* 如果是通过名字调用，需要更新全局表 */
  if (lua_type(L, 1) == LUA_TSTRING) {
    lua_pop(L, 1);  /* 弹出旧函数 */
    lua_pushvalue(L, 2);
    lua_setglobal(L, lua_tostring(L, 1));
  }
  
  return 1;
}


static const luaL_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {"setcstacklimit", db_setcstacklimit},
  {"hotfix", db_hotfix},
  /* Breakpoint functions */
  {"setbreakpoint", db_setbreakpoint},
  {"removebreakpoint", db_removebreakpoint},
  {"getbreakpoints", db_getbreakpoints},
  {"enablebreakpoint", db_enablebreakpoint},
  {"clearbreakpoints", db_clearbreakpoints},
  /* Debug control */
  {"continue", db_continue},
  {"step", db_step},
  {"next", db_next},
  {"finish", db_finish},
  /* Debug output */
  {"setoutputcallback", db_setoutputcallback},
  {"getoutputcallback", db_getoutputcallback},
  {NULL, NULL}
};


LUAMOD_API int luaopen_debug (lua_State *L) {
  luaL_newlib(L, dblib);
  return 1;
}
