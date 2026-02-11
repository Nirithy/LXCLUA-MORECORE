#define lthreadlib_c
#define LUA_LIB

#include "lprefix.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lthread.h"
#include "lstruct.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Thread handle structure for managing Lua threads.
 */
typedef struct {
    l_thread_t thread;      /**< Native thread handle */
    lua_State *L_thread;    /**< Lua state associated with the thread */
    int ref;                /**< Registry reference to the thread state */
    char name[64];          /**< Thread name */
} ThreadHandle;

/**
 * @brief Element in a channel's linked list.
 */
typedef struct ChannelElem {
    int ref;                    /**< Registry reference to the stored value */
    struct ChannelElem *next;   /**< Next element in the list */
} ChannelElem;

/**
 * @brief Selector structure for 'pick' operations.
 */
typedef struct Selector {
    l_mutex_t lock;     /**< Mutex for condition variable */
    l_cond_t cond;      /**< Condition variable for signaling */
    int signaled;       /**< Flag indicating if the selector has been signaled */
} Selector;

/**
 * @brief Listener structure for channels to notify selectors.
 */
typedef struct Listener {
    Selector *sel;          /**< Pointer to the selector */
    struct Listener *next;  /**< Next listener in the list */
} Listener;

/**
 * @brief Channel structure for thread communication.
 */
typedef struct {
    l_mutex_t lock;         /**< Mutex for thread safety */
    l_cond_t cond;          /**< Condition variable for waiting threads */
    ChannelElem *head;      /**< Head of the message queue */
    ChannelElem *tail;      /**< Tail of the message queue */
    int closed;             /**< Flag indicating if the channel is closed */
    Listener *listeners;    /**< List of listeners waiting on this channel */
    int type_ref;           /**< Registry reference to the type constraint */
} Channel;

/**
 * @brief Entry point for new threads.
 *
 * @param arg Pointer to the Lua state of the new thread.
 * @return NULL.
 */
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

/**
 * @brief Helper to register thread handle in the registry map.
 *
 * @param L The main Lua state.
 * @param L_thread The thread state to register.
 * @param th_idx Index of the thread handle userdata on the stack.
 */
static void register_thread_handle(lua_State *L, lua_State *L_thread, int th_idx) {
    th_idx = lua_absindex(L, th_idx);
    if (lua_getfield(L, LUA_REGISTRYINDEX, "_THREAD_MAP") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_THREAD_MAP");
    }
    lua_pushlightuserdata(L, L_thread);
    lua_pushvalue(L, th_idx);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

/**
 * @brief Creates a new thread.
 *
 * Usage: thread.create(func, ...)
 *
 * @param L The Lua state.
 * @return 1 (the thread object).
 */
static int thread_create(lua_State *L) {
    int n = lua_gettop(L);
    luaL_checktype(L, 1, LUA_TFUNCTION);

    ThreadHandle *th = (ThreadHandle *)lua_newuserdata(L, sizeof(ThreadHandle));
    memset(th, 0, sizeof(ThreadHandle));
    strcpy(th->name, "thread");
    luaL_getmetatable(L, "lthread");
    lua_setmetatable(L, -2);

    lua_State *L1 = lua_newthread(L);
    th->L_thread = L1;

    // Register handle
    lua_pushvalue(L, -2); /* dup th */
    register_thread_handle(L, L1, -1);
    lua_pop(L, 1);

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

/**
 * @brief Joins a thread, waiting for it to finish and retrieving results.
 *
 * Usage: th:join()
 *
 * @param L The Lua state.
 * @return Number of results returned by the thread function.
 */
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

/**
 * @brief Creates a thread and immediately joins it (blocking execution).
 *
 * Usage: thread.createx(func, ...)
 *
 * @param L The Lua state.
 * @return Results of the function execution.
 */
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

/**
 * @brief Returns the thread object for the current thread.
 *
 * Usage: thread.self() or thread.current()
 *
 * @param L The Lua state.
 * @return The thread object.
 */
static int thread_self(lua_State *L) {
    if (lua_getfield(L, LUA_REGISTRYINDEX, "_THREAD_MAP") == LUA_TTABLE) {
        lua_pushlightuserdata(L, L);
        lua_gettable(L, -2);
        if (!lua_isnil(L, -1)) {
            lua_remove(L, -2); /* remove map */
            return 1;
        }
        lua_pop(L, 2); /* remove nil and map */
    } else {
        lua_pop(L, 1); /* remove nil (getfield result) */
    }

    /* Not found, create new */
    ThreadHandle *th = (ThreadHandle *)lua_newuserdata(L, sizeof(ThreadHandle));
    memset(th, 0, sizeof(ThreadHandle));
    th->L_thread = L;
    th->ref = LUA_NOREF;
#if defined(LUA_USE_WINDOWS)
    th->thread.thread = GetCurrentThread();
#else
    th->thread.thread = pthread_self();
#endif
    strcpy(th->name, "thread");

    luaL_getmetatable(L, "lthread");
    lua_setmetatable(L, -2);

    /* Register it */
    register_thread_handle(L, L, -1); /* th is at top */

    return 1;
}

/**
 * @brief Gets or sets the name of the thread.
 *
 * Usage: th:name([new_name])
 *
 * @param L The Lua state.
 * @return The thread name.
 */
static int thread_name(lua_State *L) {
    ThreadHandle *th = (ThreadHandle *)luaL_checkudata(L, 1, "lthread");
    if (lua_gettop(L) >= 2) {
        const char *name = luaL_checkstring(L, 2);
        strncpy(th->name, name, 63);
        th->name[63] = '\0';
    }
    lua_pushstring(L, th->name);
    return 1;
}

/**
 * @brief Returns the native thread ID.
 *
 * Usage: th:id()
 *
 * @param L The Lua state.
 * @return The thread ID (integer).
 */
static int thread_id(lua_State *L) {
    ThreadHandle *th = (ThreadHandle *)luaL_checkudata(L, 1, "lthread");
    lua_pushinteger(L, (lua_Integer)l_thread_getid(&th->thread));
    return 1;
}

/**
 * @brief Internal implementation of channel creation.
 *
 * @param L The Lua state.
 * @param type_idx Index of the type specifier.
 * @return 1 (the channel object).
 */
static int channel_create_impl(lua_State *L, int type_idx) {
    Channel *ch = (Channel *)lua_newuserdata(L, sizeof(Channel));
    l_mutex_init(&ch->lock);
    l_cond_init(&ch->cond);
    ch->head = NULL;
    ch->tail = NULL;
    ch->closed = 0;
    ch->listeners = NULL;
    ch->type_ref = LUA_NOREF;
    if (type_idx != 0) {
        lua_pushvalue(L, type_idx);
        ch->type_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    luaL_getmetatable(L, "lthread.channel");
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief Helper for factory calls.
 */
static int channel_factory_call(lua_State *L) {
    return channel_create_impl(L, lua_upvalueindex(1));
}

/**
 * @brief Creates a new channel.
 *
 * Usage: thread.channel([type])
 *
 * @param L The Lua state.
 * @return The channel object.
 */
static int thread_channel(lua_State *L) {
    if (lua_gettop(L) == 0) {
        return channel_create_impl(L, 0);
    } else {
        lua_pushvalue(L, 1);
        lua_pushcclosure(L, channel_factory_call, 1);
        return 1;
    }
}

/**
 * @brief Garbage collector for channels.
 *
 * @param L The Lua state.
 * @return 0.
 */
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
    Listener *l = ch->listeners;
    while (l) {
        Listener *next = l->next;
        free(l);
        l = next;
    }
    ch->listeners = NULL;
    if (ch->type_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, ch->type_ref);
    }
    l_mutex_unlock(&ch->lock);
    l_mutex_destroy(&ch->lock);
    l_cond_destroy(&ch->cond);
    return 0;
}

/**
 * @brief Checks if a value matches the channel's type constraint.
 *
 * @param L The Lua state.
 * @param type_idx Index of the type specifier in registry.
 * @param val_idx Index of the value to check.
 * @return 1 if match, 0 otherwise.
 */
static int check_type_match(lua_State *L, int type_idx, int val_idx) {
    if (lua_type(L, type_idx) == LUA_TSTRING) {
        const char *expected = lua_tostring(L, type_idx);
        if (strcmp(expected, "number") == 0) return lua_isnumber(L, val_idx);
        if (strcmp(expected, "string") == 0) return lua_isstring(L, val_idx);
        if (strcmp(expected, "boolean") == 0) return lua_isboolean(L, val_idx);
        if (strcmp(expected, "table") == 0) return lua_istable(L, val_idx);
        if (strcmp(expected, "function") == 0) return lua_isfunction(L, val_idx);
        if (strcmp(expected, "thread") == 0) return lua_isthread(L, val_idx);
        if (strcmp(expected, "userdata") == 0) return lua_isuserdata(L, val_idx);
        if (strcmp(expected, "nil_type") == 0) return lua_isnil(L, val_idx);
        return 0;
    } else if (lua_type(L, type_idx) == LUA_TTABLE) {
        if (lua_type(L, val_idx) != LUA_TSTRUCT) return 0;
        Struct *s = (Struct *)lua_topointer(L, val_idx);
        if (!s) return 0;
        return (s->def == (Table*)lua_topointer(L, type_idx));
    }
    return 1; /* Match anything else or invalid type spec */
}

/**
 * @brief Sends a value to the channel (blocking).
 *
 * Usage: ch:send(val) or ch:push(val)
 *
 * @param L The Lua state.
 * @return 0 on success.
 */
static int channel_send(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    luaL_checkany(L, 2);

    if (ch->type_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ch->type_ref);
        if (!check_type_match(L, -1, 2)) {
             return luaL_error(L, "channel type mismatch");
        }
        lua_pop(L, 1);
    }

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

    Listener *l = ch->listeners;
    while (l) {
        l_mutex_lock(&l->sel->lock);
        l->sel->signaled = 1;
        l_cond_signal(&l->sel->cond);
        l_mutex_unlock(&l->sel->lock);
        l = l->next;
    }

    l_mutex_unlock(&ch->lock);
    return 0;
}

/**
 * @brief Tries to send a value to the channel (non-blocking).
 *
 * Usage: ch:try_send(val)
 *
 * @param L The Lua state.
 * @return true on success, false otherwise.
 */
static int channel_try_send(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    luaL_checkany(L, 2);

    if (ch->type_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ch->type_ref);
        if (!check_type_match(L, -1, 2)) {
             lua_pop(L, 1);
             lua_pushboolean(L, 0);
             return 1;
        }
        lua_pop(L, 1);
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX); // pops value

    ChannelElem *elem = (ChannelElem *)malloc(sizeof(ChannelElem));
    if (!elem) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "out of memory");
    }
    elem->ref = ref;
    elem->next = NULL;

    if (l_mutex_trylock(&ch->lock) != 0) {
        free(elem);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (ch->closed) {
        l_mutex_unlock(&ch->lock);
        free(elem);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (ch->tail) {
        ch->tail->next = elem;
        ch->tail = elem;
    } else {
        ch->head = ch->tail = elem;
    }

    l_cond_signal(&ch->cond);

    Listener *l = ch->listeners;
    while (l) {
        l_mutex_lock(&l->sel->lock);
        l->sel->signaled = 1;
        l_cond_signal(&l->sel->cond);
        l_mutex_unlock(&l->sel->lock);
        l = l->next;
    }

    l_mutex_unlock(&ch->lock);
    lua_pushboolean(L, 1);
    return 1;
}

/**
 * @brief Receives a value from the channel (blocking).
 *
 * Usage: ch:receive() or ch:pop()
 *
 * @param L The Lua state.
 * @return The received value, or nil if closed.
 */
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

/**
 * @brief Tries to receive a value from the channel (non-blocking).
 *
 * Usage: ch:try_recv()
 *
 * @param L The Lua state.
 * @return The received value, or nil if empty.
 */
static int channel_try_receive(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");

    l_mutex_lock(&ch->lock);
    if (ch->head == NULL) {
        l_mutex_unlock(&ch->lock);
        lua_pushnil(L);
        return 1;
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

/**
 * @brief Closes the channel.
 *
 * Usage: ch:close()
 *
 * @param L The Lua state.
 * @return 0.
 */
static int channel_close(lua_State *L) {
    Channel *ch = (Channel *)luaL_checkudata(L, 1, "lthread.channel");
    l_mutex_lock(&ch->lock);
    ch->closed = 1;
    l_cond_broadcast(&ch->cond);

    Listener *l = ch->listeners;
    while (l) {
        l_mutex_lock(&l->sel->lock);
        l->sel->signaled = 1;
        l_cond_signal(&l->sel->cond);
        l_mutex_unlock(&l->sel->lock);
        l = l->next;
    }

    l_mutex_unlock(&ch->lock);
    return 0;
}

/**
 * @brief Peeks at the next value in the channel without removing it.
 *
 * Usage: ch:peek()
 *
 * @param L The Lua state.
 * @return The value, or nil if empty.
 */
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

/**
 * @brief Creates a receive operation descriptor for 'pick'.
 *
 * Usage: ch:recv_op()
 *
 * @param L The Lua state.
 * @return A table describing the operation.
 */
static int channel_recv_op(lua_State *L) {
    luaL_checkudata(L, 1, "lthread.channel");
    lua_newtable(L);
    lua_pushstring(L, "recv");
    lua_setfield(L, -2, "op");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "ch");
    return 1;
}

/**
 * @brief Creates a receive operation descriptor.
 *
 * Usage: thread.on(channel)
 *
 * @param L The Lua state.
 * @return A table describing the operation.
 */
static int thread_on(lua_State *L) {
    if (lua_type(L, 1) == LUA_TUSERDATA) {
        lua_newtable(L);
        lua_pushstring(L, "recv");
        lua_setfield(L, -2, "op");
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "ch");
        return 1;
    } else if (lua_type(L, 1) == LUA_TTABLE) {
        lua_pushvalue(L, 1);
        return 1;
    }
    return luaL_error(L, "invalid argument to on()");
}

/**
 * @brief Creates a timeout operation descriptor.
 *
 * Usage: thread.over(seconds)
 *
 * @param L The Lua state.
 * @return A table describing the operation.
 */
static int thread_over(lua_State *L) {
    lua_Number n = luaL_checknumber(L, 1);
    lua_newtable(L);
    lua_pushstring(L, "timeout");
    lua_setfield(L, -2, "op");
    lua_pushnumber(L, n);
    lua_setfield(L, -2, "duration");
    return 1;
}

/**
 * @brief Unregisters the selector from all channels in the case list.
 *
 * @param L The Lua state.
 * @param tab_idx Index of the cases table.
 * @param sel The selector.
 */
static void unregister_all(lua_State *L, int tab_idx, Selector *sel) {
    int n = luaL_len(L, tab_idx);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, tab_idx, i); /* case */
        lua_rawgeti(L, -1, 1);      /* desc */
        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (op && strcmp(op, "recv") == 0) {
            lua_getfield(L, -1, "ch");
            Channel *ch = (Channel *)lua_touserdata(L, -1);
            lua_pop(L, 1);
            if (ch) {
                l_mutex_lock(&ch->lock);
                Listener **pp = &ch->listeners;
                while (*pp) {
                    if ((*pp)->sel == sel) {
                        Listener *rem = *pp;
                        *pp = rem->next;
                        free(rem);
                        break;
                    }
                    pp = &(*pp)->next;
                }
                l_mutex_unlock(&ch->lock);
            }
        }
        lua_pop(L, 2);
    }
}

/**
 * @brief Selects one of multiple channel operations to perform.
 *
 * Usage: thread.pick { {case1, func1}, {case2, func2}, ... }
 *
 * @param L The Lua state.
 * @return The result of the selected function.
 */
static int thread_pick(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    Selector sel;
    l_mutex_init(&sel.lock);
    l_cond_init(&sel.cond);
    sel.signaled = 0;

    int n = luaL_len(L, 1);
    lua_Number timeout = -1.0;
    int has_timeout = 0;
    int timeout_idx = 0;

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        if (!lua_istable(L, -1)) {
             l_mutex_destroy(&sel.lock);
             l_cond_destroy(&sel.cond);
             return luaL_error(L, "pick expects a table of cases (index %d)", i);
        }
        lua_rawgeti(L, -1, 1);
        if (!lua_istable(L, -1)) {
             l_mutex_destroy(&sel.lock);
             l_cond_destroy(&sel.cond);
             return luaL_error(L, "invalid case description (index %d)", i);
        }
        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        if (!op) {
             l_mutex_destroy(&sel.lock);
             l_cond_destroy(&sel.cond);
             return luaL_error(L, "missing op in case description (index %d)", i);
        }
        if (strcmp(op, "recv") == 0) {
            lua_getfield(L, -2, "ch");
            if (lua_isnil(L, -1) || !lua_touserdata(L, -1)) {
                 l_mutex_destroy(&sel.lock);
                 l_cond_destroy(&sel.cond);
                 return luaL_error(L, "missing or invalid channel in recv op (index %d)", i);
            }
            lua_pop(L, 1);
        } else if (strcmp(op, "timeout") == 0) {
            lua_getfield(L, -2, "duration");
            if (!lua_isnumber(L, -1)) {
                 l_mutex_destroy(&sel.lock);
                 l_cond_destroy(&sel.cond);
                 return luaL_error(L, "missing duration in timeout op (index %d)", i);
            }
            timeout = lua_tonumber(L, -1);
            has_timeout = 1;
            timeout_idx = i;
            lua_pop(L, 1);
        } else {
             l_mutex_destroy(&sel.lock);
             l_cond_destroy(&sel.cond);
             return luaL_error(L, "unknown op '%s' (index %d)", op, i);
        }
        lua_pop(L, 3);
    }

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        lua_rawgeti(L, -1, 1);

        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (strcmp(op, "recv") == 0) {
            lua_getfield(L, -1, "ch");
            Channel *ch = (Channel *)lua_touserdata(L, -1);
            lua_pop(L, 1);

            l_mutex_lock(&ch->lock);
            if (ch->head || ch->closed) {
                int ref = LUA_NOREF;
                int closed = ch->closed;
                if (ch->head) {
                    ChannelElem *elem = ch->head;
                    ch->head = elem->next;
                    if (ch->head == NULL) ch->tail = NULL;
                    ref = elem->ref;
                    free(elem);
                }
                l_mutex_unlock(&ch->lock);

                unregister_all(L, 1, &sel);
                l_mutex_destroy(&sel.lock);
                l_cond_destroy(&sel.cond);

                lua_rawgeti(L, -2, 2);
                if (closed && ref == LUA_NOREF) {
                    lua_pushnil(L);
                } else {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                }
                lua_call(L, 1, 1);
                return 1;
            }

            Listener *l = malloc(sizeof(Listener));
            if (l) {
                l->sel = &sel;
                l->next = ch->listeners;
                ch->listeners = l;
            }
            l_mutex_unlock(&ch->lock);
        }
        lua_pop(L, 2);
    }

    l_mutex_lock(&sel.lock);
    while (1) {
        if (sel.signaled) break;

        if (has_timeout) {
            int res = l_cond_wait_timeout(&sel.cond, &sel.lock, (long)(timeout * 1000));
            if (res == LTHREAD_TIMEDOUT) {
                l_mutex_unlock(&sel.lock);
                unregister_all(L, 1, &sel);
                l_mutex_destroy(&sel.lock);
                l_cond_destroy(&sel.cond);

                lua_rawgeti(L, 1, timeout_idx);
                lua_rawgeti(L, -1, 2);
                lua_call(L, 0, 1);
                return 1;
            }
        } else {
            l_cond_wait(&sel.cond, &sel.lock);
        }
    }
    l_mutex_unlock(&sel.lock);

    unregister_all(L, 1, &sel);
    l_mutex_destroy(&sel.lock);
    l_cond_destroy(&sel.cond);

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);
        lua_rawgeti(L, -1, 1);
        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (op && strcmp(op, "recv") == 0) {
            lua_getfield(L, -1, "ch");
            Channel *ch = (Channel *)lua_touserdata(L, -1);
            lua_pop(L, 1);

            l_mutex_lock(&ch->lock);
            if (ch->head || ch->closed) {
                int ref = LUA_NOREF;
                int closed = ch->closed;
                if (ch->head) {
                    ChannelElem *elem = ch->head;
                    ch->head = elem->next;
                    if (ch->head == NULL) ch->tail = NULL;
                    ref = elem->ref;
                    free(elem);
                }
                l_mutex_unlock(&ch->lock);

                lua_rawgeti(L, -2, 2);
                if (closed && ref == LUA_NOREF) {
                    lua_pushnil(L);
                } else {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                }
                lua_call(L, 1, 1);
                return 1;
            }
            l_mutex_unlock(&ch->lock);
        }
        lua_pop(L, 2);
    }

    return 0;
}

static const luaL_Reg thread_methods[] = {
    {"join", thread_join},
    {"name", thread_name},
    {"id", thread_id},
    {NULL, NULL}
};

static const luaL_Reg channel_methods[] = {
    {"send", channel_send},
    {"receive", channel_receive},
    {"try_send", channel_try_send},
    {"try_recv", channel_try_receive},
    {"pop", channel_receive},
    {"push", channel_send},
    {"peek", channel_peek},
    {"recv_op", channel_recv_op},
    {"close", channel_close},
    {"__gc", channel_gc},
    {NULL, NULL}
};

static const luaL_Reg thread_funcs[] = {
    {"create", thread_create},
    {"createx", thread_createx},
    {"channel", thread_channel},
    {"pick", thread_pick},
    {"on", thread_on},
    {"over", thread_over},
    {"self", thread_self},
    {"current", thread_self}, /* Alias for self */
    {NULL, NULL}
};

/**
 * @brief Registers the thread library.
 *
 * @param L The Lua state.
 * @return 1 (the table).
 */
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
