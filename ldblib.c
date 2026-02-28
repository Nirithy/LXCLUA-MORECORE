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


static const char *get_filename (const char *source) {
  const char *filename = source;
  if (*filename == '@') filename++;
  const char *last_sep = strrchr(filename, '/');
  if (last_sep) {
    filename = last_sep + 1;
  }
  last_sep = strrchr(filename, '\\');
  if (last_sep && last_sep + 1 > filename) {
    filename = last_sep + 1;
  }
  return filename;
}


static int get_stack_level (lua_State *L) {
  lua_Debug ar;
  int level = 0;
  while (lua_getstack(L, level, &ar)) {
    level++;
  }
  return level;
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
  
  int top = lua_gettop(L);

  if (ar->event == LUA_HOOKLINE && ar->currentline >= 0) {
    int should_stop = 0;
    const char *stop_event = "breakpoint";

    /* 1. Check for breakpoints */
    if (lua_getfield(L, LUA_REGISTRYINDEX, BREAKPOINTKEY) == LUA_TTABLE) {
      int bptable_idx = lua_gettop(L);
      lua_getinfo(L, "S", ar);
      const char *filename = get_filename(ar->source ? ar->source : "");
      char key[512];
      snprintf(key, sizeof(key), "%s:%d", filename, ar->currentline);
      if (lua_getfield(L, bptable_idx, key) == LUA_TTABLE) {
        int bp_idx = lua_gettop(L);
        lua_getfield(L, bp_idx, "enabled");
        if (lua_toboolean(L, -1)) {
          lua_pop(L, 1); /* pop enabled */
          if (lua_getfield(L, bp_idx, "condition") == LUA_TSTRING) {
            const char *cond = lua_tostring(L, -1);
            char buff[512];
            if (strncmp(cond, "return ", 7) != 0) {
              snprintf(buff, sizeof(buff), "return %s", cond);
              cond = buff;
            }
            if (luaL_loadstring(L, cond) == LUA_OK) {
              if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                should_stop = lua_toboolean(L, -1);
              }
              lua_pop(L, 1); /* pop result/error */
            } else {
              lua_pop(L, 1); /* pop load error */
            }
          } else {
            should_stop = 1; /* unconditional */
            lua_pop(L, 1); /* pop non-string condition */
          }
        } else {
          lua_pop(L, 1); /* pop disabled */
        }
      }
    }
    lua_settop(L, top);

    /* 2. Check debug modes */
    if (!should_stop) {
      if (lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY) == LUA_TTABLE) {
        int state_idx = lua_gettop(L);
        lua_getfield(L, state_idx, "mode");
        int mode = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (mode != 0) {
          int stop_by_mode = 0;
          if (mode == 1) stop_by_mode = 1; /* step */
          else if (mode == 2 || mode == 3) {
            lua_getfield(L, state_idx, "target_level");
            int target_level = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (get_stack_level(L) <= target_level)
              stop_by_mode = 1;
          }
          if (stop_by_mode) {
            should_stop = 1;
            stop_event = (mode == 1) ? "step" : (mode == 2 ? "next" : "finish");
            lua_pushinteger(L, 0);
            lua_setfield(L, state_idx, "mode");
          }
        }
      }
    }
    lua_settop(L, top);

    if (should_stop) {
      if (lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY) == LUA_TTABLE) {
        lua_pushinteger(L, get_stack_level(L));
        lua_setfield(L, -2, "break_level");
      }
      lua_pop(L, 1); /* pop DEBUGSTATEKEY table or nil */

      lua_getinfo(L, "S", ar);
      lua_getfield(L, LUA_REGISTRYINDEX, DEBUGOUTPUTKEY);
      if (lua_isfunction(L, -1)) {
        lua_pushstring(L, stop_event);
        lua_pushstring(L, ar->short_src);
        lua_pushinteger(L, ar->currentline);
        lua_pcall(L, 3, 0, 0);
      } else {
        fprintf(stderr, "Breakpoint (%s) at %s:%d\n", stop_event, ar->short_src, ar->currentline);
      }
    }
    lua_settop(L, top);
  }

  /* 4. Call user registered hook function */
  if (lua_getfield(L, LUA_REGISTRYINDEX, HOOKKEY) == LUA_TTABLE) {
    int hooktable_idx = lua_gettop(L);
    lua_pushthread(L);
    if (lua_rawget(L, hooktable_idx) == LUA_TFUNCTION) {
      lua_pushstring(L, hooknames[(int)ar->event]);
      if (ar->currentline >= 0) lua_pushinteger(L, ar->currentline);
      else lua_pushnil(L);
      lua_getinfo(L, "lS", ar);
      lua_call(L, 2, 0);
    }
  }
  lua_settop(L, top);
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

static void ensure_breakpoint_table (lua_State *L) {
  if (lua_getfield(L, LUA_REGISTRYINDEX, BREAKPOINTKEY) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, BREAKPOINTKEY);
  }
}

static void ensure_debug_state (lua_State *L) {
  if (lua_getfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "mode");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "target_level");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "break_level");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, DEBUGSTATEKEY);
  }
}

static int db_setbreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  const char *condition = luaL_optstring(L, 3, NULL);
  if (lua_gethook(L) == NULL) {
    lua_sethook(L, hookf, LUA_MASKLINE, 0);
  }
  lua_settop(L, 3);
  ensure_breakpoint_table(L); /* index 4 */
  const char *filename = get_filename(source);
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", filename, line);
  lua_getfield(L, 4, key);
  int exists = !lua_isnil(L, -1);
  lua_pop(L, 1);
  lua_newtable(L); /* index 5 */
  lua_pushstring(L, filename);
  lua_setfield(L, 5, "source");
  lua_pushinteger(L, line);
  lua_setfield(L, 5, "line");
  lua_pushboolean(L, 1);
  lua_setfield(L, 5, "enabled");
  if (condition) {
    lua_pushstring(L, condition);
    lua_setfield(L, 5, "condition");
  }
  lua_pushboolean(L, exists);
  lua_setfield(L, 5, "exists");
  lua_pushvalue(L, 5);
  lua_setfield(L, 4, key);
  lua_remove(L, 4);
  return 1;
}

static int db_removebreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  lua_settop(L, 2);
  ensure_breakpoint_table(L); /* index 3 */
  const char *filename = get_filename(source);
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", filename, line);
  lua_getfield(L, 3, key);
  int exists = !lua_isnil(L, -1);
  lua_pop(L, 1);
  if (exists) {
    lua_pushnil(L);
    lua_setfield(L, 3, key);
  }
  lua_pushboolean(L, exists);
  lua_remove(L, 3);
  return 1;
}

static int db_getbreakpoints (lua_State *L) {
  lua_settop(L, 0);
  ensure_breakpoint_table(L); /* index 1 */
  lua_newtable(L); /* index 2 */
  int idx = 1;
  lua_pushnil(L); /* index 3 */
  while (lua_next(L, 1)) {
    lua_pushvalue(L, -1);
    lua_rawseti(L, 2, idx++);
    lua_pop(L, 1);
  }
  lua_remove(L, 1);
  return 1;
}

static int db_enablebreakpoint (lua_State *L) {
  const char *source = luaL_checkstring(L, 1);
  int line = (int)luaL_checkinteger(L, 2);
  int enable = lua_toboolean(L, 3);
  lua_settop(L, 3);
  ensure_breakpoint_table(L); /* index 4 */
  const char *filename = get_filename(source);
  char key[512];
  snprintf(key, sizeof(key), "%s:%d", filename, line);
  if (lua_getfield(L, 4, key) == LUA_TTABLE) {
    lua_pushboolean(L, enable);
    lua_setfield(L, -2, "enabled");
    lua_pushboolean(L, 1);
  } else {
    lua_pushboolean(L, 0);
  }
  lua_remove(L, 4);
  return 1;
}

static int db_clearbreakpoints (lua_State *L) {
  ensure_breakpoint_table(L);
  int count = 0;
  lua_pushnil(L);
  while (lua_next(L, -2)) {
    count++;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, BREAKPOINTKEY);
  lua_pushinteger(L, count);
  return 1;
}

static int db_continue (lua_State *L) {
  ensure_debug_state(L);
  lua_pushinteger(L, 0);
  lua_setfield(L, -2, "mode");
  lua_pop(L, 1);
  lua_pushstring(L, "continue");
  return 1;
}

static int db_step (lua_State *L) {
  ensure_debug_state(L);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "mode");
  lua_pop(L, 1);
  lua_pushstring(L, "step");
  return 1;
}

static int db_next (lua_State *L) {
  ensure_debug_state(L);
  lua_pushinteger(L, 2);
  lua_setfield(L, -2, "mode");
  lua_getfield(L, -1, "break_level");
  int break_level = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (break_level == 0) break_level = get_stack_level(L) - 1;
  lua_pushinteger(L, break_level);
  lua_setfield(L, -2, "target_level");
  lua_pop(L, 1);
  lua_pushstring(L, "next");
  return 1;
}

static int db_finish (lua_State *L) {
  ensure_debug_state(L);
  lua_pushinteger(L, 3);
  lua_setfield(L, -2, "mode");
  lua_getfield(L, -1, "break_level");
  int break_level = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  if (break_level == 0) break_level = get_stack_level(L) - 1;
  lua_pushinteger(L, break_level - 1);
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
