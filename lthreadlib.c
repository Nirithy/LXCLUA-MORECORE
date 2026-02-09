#define lthreadlib_c
#define LUA_LIB

#include "lprefix.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lthread.h"
#include <stdlib.h>

typedef struct {
    l_thread_t thread;
    lua_State *L_thread;
    int ref;
} ThreadHandle;

typedef struct ChannelElem {
    int ref;
    struct ChannelElem *next;
} ChannelElem;

typedef struct {
    l_mutex_t lock;
    l_cond_t cond;
    ChannelElem *head;
    ChannelElem *tail;
    int closed;
} Channel;

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

static int thread_createx(lua_State *L) {
    int n = lua_gettop(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_State *L1 = lua_newthread(L);

    // Move function and arguments to L1
    lua_pushvalue(L, 1);
    lua_xmove(L, L1, 1);
    for (int i = 2; i <= n; ++i) {
        lua_pushvalue(L, i);
        lua_xmove(L, L1, 1);
    }

    l_thread_t thread;
    if (l_thread_create(&thread, thread_entry, L1) != 0) {
        return luaL_error(L, "failed to create thread");
    }

    l_thread_join(thread, NULL);

    int nres = lua_gettop(L1);
    if (nres > 0) {
        if (!lua_checkstack(L, nres)) {
             return luaL_error(L, "too many results to move");
        }
        lua_xmove(L1, L, nres);
    }

    // Remove L1 from stack (it is at index n + 1)
    lua_remove(L, n + 1);

    return nres;
}

static int channel_new(lua_State *L) {
    Channel *ch = (Channel *)lua_newuserdata(L, sizeof(Channel));
    l_mutex_init(&ch->lock);
    l_cond_init(&ch->cond);
    ch->head = NULL;
    ch->tail = NULL;
    ch->closed = 0;
    luaL_getmetatable(L, "lthread.channel");
    lua_setmetatable(L, -2);
    return 1;
}

static int channel_gc(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    l_mutex_lock(&ch->lock);
    ChannelElem *curr = ch->head;
    while (curr) {
        ChannelElem *next = curr->next;
        luaL_unref(L, LUA_REGISTRYINDEX, curr->ref);
        free(curr);
        curr = next;
    }
    ch->head = NULL;
    ch->tail = NULL;
    l_mutex_unlock(&ch->lock);
    l_mutex_destroy(&ch->lock);
    l_cond_destroy(&ch->cond);
    return 0;
}

static int channel_send(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    luaL_checkany(L, 2);

    /* Create reference and allocate memory before locking to prevent deadlocks on error */
    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops value

    ChannelElem *elem = (ChannelElem *)malloc(sizeof(ChannelElem));
    if (!elem) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "out of memory");
    }
    elem->ref = ref;
    elem->next = NULL;

    l_mutex_lock(&ch->lock);
    if (ch->closed) {
        l_mutex_unlock(&ch->lock);
        free(elem);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "channel is closed");
    }

    if (ch->tail) {
        ch->tail->next = elem;
        ch->tail = elem;
    } else {
        ch->head = ch->tail = elem;
    }

    l_cond_signal(&ch->cond);
    l_mutex_unlock(&ch->lock);
    return 0;
}

static int channel_receive(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");

    l_mutex_lock(&ch->lock);
    while (ch->head == NULL) {
        if (ch->closed) {
            l_mutex_unlock(&ch->lock);
            lua_pushnil(L);
            return 1;
        }
        l_cond_wait(&ch->cond, &ch->lock);
    }

    ChannelElem *elem = ch->head;
    ch->head = elem->next;
    if (ch->head == NULL) {
        ch->tail = NULL;
    }

    int ref = elem->ref;
    free(elem);
    l_mutex_unlock(&ch->lock);

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

static int channel_close(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    l_mutex_lock(&ch->lock);
    ch->closed = 1;
    l_cond_broadcast(&ch->cond);
    l_mutex_unlock(&ch->lock);
    return 0;
}

static int channel_peek(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    l_mutex_lock(&ch->lock);
    if (ch->head) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ch->head->ref);
    } else {
        lua_pushnil(L);
    }
    l_mutex_unlock(&ch->lock);
    return 1;
}

static const luaL_Reg thread_methods[] = {
    {"join", thread_join},
    {NULL, NULL}
};

static const luaL_Reg channel_methods[] = {
    {"send", channel_send},
    {"receive", channel_receive},
    {"pop", channel_receive}, /* Alias */
    {"push", channel_send},   /* Alias */
    {"peek", channel_peek},
    {"close", channel_close},
    {"__gc", channel_gc},
    {NULL, NULL}
};

static const luaL_Reg thread_funcs[] = {
    {"create", thread_create},
    {"createx", thread_createx},
    {"channel", channel_new},
    {NULL, NULL}
};

int luaopen_thread(lua_State *L) {
    luaL_newmetatable(L, "lthread");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, thread_methods, 0);

    luaL_newmetatable(L, "lthread.channel");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, channel_methods, 0);

    luaL_newlib(L, thread_funcs);
    return 1;
}
