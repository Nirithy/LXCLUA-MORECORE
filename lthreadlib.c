#define lthreadlib_c
#define LUA_LIB

#include "lprefix.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lthread.h"

typedef struct {
    l_thread_t thread;
    lua_State *L_thread;
    int ref;
} ThreadHandle;

static void *thread_entry(void *arg) {
    lua_State *L = (lua_State *)arg;
    // Stack: func, args...
    int nargs = lua_gettop(L) - 1;
    if (lua_pcall(L, nargs, LUA_MULTRET, 0) != LUA_OK) {
        // Error string is on stack
        fprintf(stderr, "Thread error: %s\n", lua_tostring(L, -1));
    }
    return NULL;
}

static int thread_create(lua_State *L) {
    int n = lua_gettop(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);

    ThreadHandle *th = (ThreadHandle *)lua_newuserdata(L, sizeof(ThreadHandle));
    luaL_getmetatable(L, "lthread");
    lua_setmetatable(L, -2);

    lua_State *L1 = lua_newthread(L);
    th->L_thread = L1;

    // Anchor L1 to prevent collection
    th->ref = luaL_ref(L, LUA_REGISTRYINDEX); // Pops L1 from stack

    // Copy function and arguments to new thread
    lua_pushvalue(L, 1);
    lua_xmove(L, L1, 1);

    for (int i = 2; i <= n; ++i) {
        lua_pushvalue(L, i);
        lua_xmove(L, L1, 1);
    }

    if (l_thread_create(&th->thread, thread_entry, L1) != 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, th->ref);
        return luaL_error(L, "failed to create thread");
    }

    return 1;
}

static int thread_join(lua_State *L) {
    ThreadHandle *th = (ThreadHandle *)luaL_checkudata(L, 1, "lthread");
    if (th->L_thread == NULL) {
        return luaL_error(L, "thread already joined");
    }

    l_thread_join(th->thread, NULL);

    int nres = lua_gettop(th->L_thread);
    if (nres > 0) {
        lua_xmove(th->L_thread, L, nres);
    }

    luaL_unref(L, LUA_REGISTRYINDEX, th->ref);
    th->L_thread = NULL;

    return nres;
}

static const luaL_Reg thread_methods[] = {
    {"join", thread_join},
    {NULL, NULL}
};

static const luaL_Reg thread_funcs[] = {
    {"create", thread_create},
    {NULL, NULL}
};

int luaopen_thread(lua_State *L) {
    luaL_newmetatable(L, "lthread");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, thread_methods, 0);

    luaL_newlib(L, thread_funcs);
    return 1;
}
