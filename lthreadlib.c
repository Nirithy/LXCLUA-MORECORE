#define lthreadlib_c
#define LUA_LIB

#include "lprefix.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lthread.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    l_thread_t thread;
    lua_State *L_thread;
    int ref;
} ThreadHandle;

typedef struct ChannelElem {
    int ref;
    struct ChannelElem *next;
} ChannelElem;

typedef struct Selector {
    l_mutex_t lock;
    l_cond_t cond;
    int signaled;
} Selector;

typedef struct Listener {
    Selector *sel;
    struct Listener *next;
} Listener;

typedef struct {
    l_mutex_t lock;
    l_cond_t cond;
    ChannelElem *head;
    ChannelElem *tail;
    int closed;
    Listener *listeners;
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
    ch->listeners = NULL;
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
    /* Listeners are managed by pick(), should be empty here but clean up just in case */
    Listener *l = ch->listeners;
    while (l) {
        Listener *next = l->next;
        free(l);
        l = next;
    }
    ch->listeners = NULL;
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

    /* Signal any listeners (pick) */
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

    /* Signal any listeners (pick) */
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

static int channel_recv_op(lua_State *L) {
    luaL_checkudata(L, 1, "lthread.channel");
    lua_newtable(L);
    lua_pushstring(L, "recv");
    lua_setfield(L, -2, "op");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "ch");
    return 1;
}

static int thread_on(lua_State *L) {
    if (lua_type(L, 1) == LUA_TUSERDATA) {
        /* Assume channel */
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

static int thread_over(lua_State *L) {
    lua_Number n = luaL_checknumber(L, 1);
    lua_newtable(L);
    lua_pushstring(L, "timeout");
    lua_setfield(L, -2, "op");
    lua_pushnumber(L, n);
    lua_setfield(L, -2, "duration");
    return 1;
}

static void unregister_all(lua_State *L, int tab_idx, Selector *sel) {
    int n = luaL_len(L, tab_idx);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, tab_idx, i); /* case */
        lua_rawgeti(L, -1, 1);      /* desc */
        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        lua_pop(L, 1); /* op */

        if (op && strcmp(op, "recv") == 0) {
            lua_getfield(L, -1, "ch");
            Channel *ch = (Channel *)lua_touserdata(L, -1);
            lua_pop(L, 1); /* ch */
            if (ch) {
                l_mutex_lock(&ch->lock);
                Listener **pp = &ch->listeners;
                while (*pp) {
                    if ((*pp)->sel == sel) {
                        Listener *rem = *pp;
                        *pp = rem->next;
                        free(rem);
                        break; /* Should only be once per channel per pick */
                    }
                    pp = &(*pp)->next;
                }
                l_mutex_unlock(&ch->lock);
            }
        }
        lua_pop(L, 2); /* desc, case */
    }
}

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

    /* Pass 1: Validate inputs */
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i); /* case table */
        if (!lua_istable(L, -1)) {
             l_mutex_destroy(&sel.lock);
             l_cond_destroy(&sel.cond);
             return luaL_error(L, "pick expects a table of cases (index %d)", i);
        }
        lua_rawgeti(L, -1, 1); /* desc */
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
        lua_pop(L, 3); /* op, desc, case */
    }

    /* Pass 2: Register listeners and check for immediate data */
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i); /* case table */
        lua_rawgeti(L, -1, 1); /* desc */

        lua_getfield(L, -1, "op");
        const char *op = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (strcmp(op, "recv") == 0) {
            lua_getfield(L, -1, "ch");
            Channel *ch = (Channel *)lua_touserdata(L, -1);
            lua_pop(L, 1); /* ch */

            l_mutex_lock(&ch->lock);
            if (ch->head || ch->closed) {
                /* Ready immediately */
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

                /* Cleanup other listeners */
                unregister_all(L, 1, &sel);
                l_mutex_destroy(&sel.lock);
                l_cond_destroy(&sel.cond);

                /* Call handler */
                lua_rawgeti(L, -2, 2); /* func */
                if (closed && ref == LUA_NOREF) {
                    lua_pushnil(L); /* closed */
                } else {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                }
                lua_call(L, 1, 1);
                return 1;
            }

            /* Register listener */
            Listener *l = malloc(sizeof(Listener));
            if (l) {
                l->sel = &sel;
                l->next = ch->listeners;
                ch->listeners = l;
            }
            /* If malloc fails, we skip this channel (or should we error? Error creates safety issues unless we use userdata cleanup) */
            /* For now, ignoring malloc failure is safer than crashing, but not ideal. */

            l_mutex_unlock(&ch->lock);
        }
        lua_pop(L, 2); /* desc, case */
    }

    /* Wait loop */
    /* Calculate deadline if timeout */
    /* Note: l_cond_wait_timeout needs implementation or use standard if available */
    /* We assume we might need a loop with timed wait */

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

                /* Call timeout handler */
                lua_rawgeti(L, 1, timeout_idx);
                lua_rawgeti(L, -1, 2); /* func */
                lua_call(L, 0, 1);
                return 1;
            }
        } else {
            l_cond_wait(&sel.cond, &sel.lock);
        }
    }
    l_mutex_unlock(&sel.lock);

    /* We woke up, check channels */
    /* We need to find which channel triggered or just consume from first available */

    /* Unregister everything first to be clean */
    unregister_all(L, 1, &sel);
    l_mutex_destroy(&sel.lock);
    l_cond_destroy(&sel.cond);

    /* Iterate to find ready channel */
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i); /* case */
        lua_rawgeti(L, -1, 1); /* desc */
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

                lua_rawgeti(L, -2, 2); /* func */
                if (closed && ref == LUA_NOREF) {
                    lua_pushnil(L);
                } else {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
                    luaL_unref(L, LUA_REGISTRYINDEX, ref);
                }
                lua_call(L, 1, 1);
                // lua_pop(L, 2); /* case, desc */ -> call consumes func/arg, result is on top. case/desc are below?
                // The loop iterates using lua_rawgeti(..., i). Stack grows.
                // We need to clean up stack?
                // lua_call puts result on top.
                // We can just return 1. The garbage below will be discarded by Lua call return handling?
                // No, this is C function. We should clean up if we can, but returning 1 with result on top is essential.
                // Actually, `lua_call` removed function and args.
                // We have `lua_rawgeti(L, 1, i)` (case) and `lua_rawgeti(L, -1, 1)` (desc) on stack before call loop logic.
                // In loop: push case, push desc, pop desc, pop case.
                // Wait, "lua_pop(L, 2); /* case, desc */" was at end of loop.
                // Inside "if (ch->head...)", we break out.
                // Stack state:
                // 1: table
                // ...
                // top-2: case
                // top-1: desc (popped before) -> wait, "lua_rawgeti(L, -1, 1); /* desc */" ... "lua_pop(L, 1);" (op) ...
                // "lua_pop(L, 1); /* ch */"
                // At this point we have 'case' and 'desc' on stack?
                // "lua_rawgeti(L, -2, 2); /* func */" (from case)
                // "lua_call(L, 1, 1)".
                // Result is on top. 'case' and 'desc' are below result.
                // If we return 1, Lua takes top.
                return 1;
            }
            l_mutex_unlock(&ch->lock);
        }
        lua_pop(L, 2); /* case, desc */
    }

    return 0; /* Should not happen if signaled */
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
    {"recv_op", channel_recv_op},
    {"close", channel_close},
    {"__gc", channel_gc},
    {NULL, NULL}
};

static const luaL_Reg thread_funcs[] = {
    {"create", thread_create},
    {"createx", thread_createx},
    {"channel", channel_new},
    {"pick", thread_pick},
    {"on", thread_on},
    {"over", thread_over},
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
