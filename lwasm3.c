#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "wasm3.h"
#include "m3_env.h"

#define WASM3_ENV_METATABLE "wasm3.environment"
#define WASM3_RUNTIME_METATABLE "wasm3.runtime"
#define WASM3_MODULE_METATABLE "wasm3.module"
#define WASM3_FUNCTION_METATABLE "wasm3.function"

typedef struct {
    IM3Environment env;
} wasm3_Environment;

typedef struct {
    IM3Runtime runtime;
    // Keep a reference to the environment so it doesn't get GC'd
    int env_ref;
} wasm3_Runtime;

typedef struct {
    IM3Module module;
    // Keep a reference to the environment so it doesn't get GC'd
    int env_ref;
    int loaded; // whether it has been loaded into a runtime
} wasm3_Module;

typedef struct {
    IM3Function function;
    // Keep a reference to the runtime so it doesn't get GC'd
    int runtime_ref;
} wasm3_Function;


static int l_new_environment(lua_State *L) {
    IM3Environment env = m3_NewEnvironment();
    if (!env) {
        return luaL_error(L, "Failed to create wasm3 environment");
    }

    wasm3_Environment *we = (wasm3_Environment*)lua_newuserdata(L, sizeof(wasm3_Environment));
    we->env = env;

    luaL_getmetatable(L, WASM3_ENV_METATABLE);
    lua_setmetatable(L, -2);

    return 1;
}

static int env_gc(lua_State *L) {
    wasm3_Environment *we = (wasm3_Environment*)luaL_checkudata(L, 1, WASM3_ENV_METATABLE);
    if (we->env) {
        m3_FreeEnvironment(we->env);
        we->env = NULL;
    }
    return 0;
}

static int env_parse_module(lua_State *L) {
    wasm3_Environment *we = (wasm3_Environment*)luaL_checkudata(L, 1, WASM3_ENV_METATABLE);
    size_t wasm_size;
    const char *wasm_bytes = luaL_checklstring(L, 2, &wasm_size);

    IM3Module module;
    M3Result result = m3_ParseModule(we->env, &module, (const uint8_t*)wasm_bytes, wasm_size);
    if (result) {
        return luaL_error(L, "Failed to parse wasm module: %s", result);
    }

    wasm3_Module *wm = (wasm3_Module*)lua_newuserdata(L, sizeof(wasm3_Module));
    wm->module = module;
    wm->loaded = 0;

    // Store reference to environment
    lua_pushvalue(L, 1);
    wm->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WASM3_MODULE_METATABLE);
    lua_setmetatable(L, -2);

    return 1;
}

static int env_new_runtime(lua_State *L) {
    wasm3_Environment *we = (wasm3_Environment*)luaL_checkudata(L, 1, WASM3_ENV_METATABLE);
    lua_Integer stack_size = luaL_optinteger(L, 2, 64 * 1024);

    IM3Runtime runtime = m3_NewRuntime(we->env, stack_size, NULL);
    if (!runtime) {
        return luaL_error(L, "Failed to create wasm3 runtime");
    }

    wasm3_Runtime *wr = (wasm3_Runtime*)lua_newuserdata(L, sizeof(wasm3_Runtime));
    wr->runtime = runtime;

    // Store reference to environment
    lua_pushvalue(L, 1);
    wr->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WASM3_RUNTIME_METATABLE);
    lua_setmetatable(L, -2);

    return 1;
}

static int module_gc(lua_State *L) {
    wasm3_Module *wm = (wasm3_Module*)luaL_checkudata(L, 1, WASM3_MODULE_METATABLE);
    if (wm->module && !wm->loaded) {
        m3_FreeModule(wm->module);
        wm->module = NULL;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, wm->env_ref);
    return 0;
}

static int runtime_gc(lua_State *L) {
    wasm3_Runtime *wr = (wasm3_Runtime*)luaL_checkudata(L, 1, WASM3_RUNTIME_METATABLE);
    if (wr->runtime) {
        m3_FreeRuntime(wr->runtime);
        wr->runtime = NULL;
    }
    luaL_unref(L, LUA_REGISTRYINDEX, wr->env_ref);
    return 0;
}

static int runtime_load(lua_State *L) {
    wasm3_Runtime *wr = (wasm3_Runtime*)luaL_checkudata(L, 1, WASM3_RUNTIME_METATABLE);
    wasm3_Module *wm = (wasm3_Module*)luaL_checkudata(L, 2, WASM3_MODULE_METATABLE);

    if (wm->loaded) {
        return luaL_error(L, "Module already loaded");
    }

    M3Result result = m3_LoadModule(wr->runtime, wm->module);
    if (result) {
        return luaL_error(L, "Failed to load wasm module: %s", result);
    }

    wm->loaded = 1; // Ownership transferred to runtime
    return 0;
}

static int runtime_find_function(lua_State *L) {
    wasm3_Runtime *wr = (wasm3_Runtime*)luaL_checkudata(L, 1, WASM3_RUNTIME_METATABLE);
    const char *func_name = luaL_checkstring(L, 2);

    IM3Function function;
    M3Result result = m3_FindFunction(&function, wr->runtime, func_name);
    if (result) {
        return luaL_error(L, "Failed to find function '%s': %s", func_name, result);
    }

    wasm3_Function *wf = (wasm3_Function*)lua_newuserdata(L, sizeof(wasm3_Function));
    wf->function = function;

    // Store reference to runtime
    lua_pushvalue(L, 1);
    wf->runtime_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, WASM3_FUNCTION_METATABLE);
    lua_setmetatable(L, -2);

    return 1;
}

static int function_gc(lua_State *L) {
    wasm3_Function *wf = (wasm3_Function*)luaL_checkudata(L, 1, WASM3_FUNCTION_METATABLE);
    luaL_unref(L, LUA_REGISTRYINDEX, wf->runtime_ref);
    return 0;
}

static int function_call(lua_State *L) {
    wasm3_Function *wf = (wasm3_Function*)luaL_checkudata(L, 1, WASM3_FUNCTION_METATABLE);
    int top = lua_gettop(L);
    int argc = top - 1; // first arg is the function object itself

    int expected_argc = m3_GetArgCount(wf->function);
    if (argc != expected_argc) {
        return luaL_error(L, "Function expects %d arguments, but %d provided", expected_argc, argc);
    }

    const char* argv[128]; // Max 128 arguments for simplicity in this binding
    if (argc > 128) {
        return luaL_error(L, "Too many arguments");
    }

    // We keep strings to pass to m3_CallArgv. We need to format numbers properly as strings.
    char arg_bufs[128][64]; // Buffers for string conversion if needed
    for (int i = 0; i < argc; i++) {
        int idx = i + 2;
        if (lua_type(L, idx) == LUA_TNUMBER) {
            if (lua_isinteger(L, idx)) {
                snprintf(arg_bufs[i], sizeof(arg_bufs[i]), "%lld", (long long)lua_tointeger(L, idx));
            } else {
                snprintf(arg_bufs[i], sizeof(arg_bufs[i]), "%f", lua_tonumber(L, idx));
            }
            argv[i] = arg_bufs[i];
        } else if (lua_type(L, idx) == LUA_TSTRING) {
            argv[i] = lua_tostring(L, idx);
        } else if (lua_type(L, idx) == LUA_TBOOLEAN) {
            snprintf(arg_bufs[i], sizeof(arg_bufs[i]), "%d", lua_toboolean(L, idx));
            argv[i] = arg_bufs[i];
        } else {
            return luaL_error(L, "Argument %d must be number, string, or boolean", i+1);
        }
    }

    M3Result result = m3_CallArgv(wf->function, argc, argv);
    if (result) {
        return luaL_error(L, "Function call failed: %s", result);
    }

    int retc = m3_GetRetCount(wf->function);
    if (retc == 0) {
        return 0;
    } else if (retc > 0) {
        uint64_t val[128]; // Max 128 returns
        const void* valptrs[128];
        for(int i=0; i<retc; i++) valptrs[i] = &val[i];

        M3Result resResult = m3_GetResults(wf->function, retc, valptrs);
        if (resResult) {
            return luaL_error(L, "Failed to get results: %s", resResult);
        }

        for (int i = 0; i < retc; i++) {
            M3ValueType type = m3_GetRetType(wf->function, i);
            switch (type) {
                case c_m3Type_i32:
                    lua_pushinteger(L, *(int32_t*)&val[i]);
                    break;
                case c_m3Type_i64:
                    lua_pushinteger(L, *(int64_t*)&val[i]);
                    break;
                case c_m3Type_f32:
                    lua_pushnumber(L, *(float*)&val[i]);
                    break;
                case c_m3Type_f64:
                    lua_pushnumber(L, *(double*)&val[i]);
                    break;
                default:
                    lua_pushnil(L);
                    break;
            }
        }
        return retc;
    }

    return 0;
}


static const struct luaL_Reg env_methods[] = {
    {"parse_module", env_parse_module},
    {"new_runtime", env_new_runtime},
    {"__gc", env_gc},
    {NULL, NULL}
};

static const struct luaL_Reg module_methods[] = {
    {"__gc", module_gc},
    {NULL, NULL}
};

static const struct luaL_Reg runtime_methods[] = {
    {"load", runtime_load},
    {"find_function", runtime_find_function},
    {"__gc", runtime_gc},
    {NULL, NULL}
};

static const struct luaL_Reg function_methods[] = {
    {"call", function_call},
    {"__gc", function_gc},
    {NULL, NULL}
};

static const struct luaL_Reg wasm3_lib[] = {
    {"new_environment", l_new_environment},
    {NULL, NULL}
};

static void create_meta(lua_State *L, const char *name, const struct luaL_Reg *methods) {
    if (luaL_newmetatable(L, name)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, methods, 0);
    }
    lua_pop(L, 1);
}

int luaopen_wasm3(lua_State *L) {
    create_meta(L, WASM3_ENV_METATABLE, env_methods);
    create_meta(L, WASM3_RUNTIME_METATABLE, runtime_methods);
    create_meta(L, WASM3_MODULE_METATABLE, module_methods);
    create_meta(L, WASM3_FUNCTION_METATABLE, function_methods);

    luaL_newlib(L, wasm3_lib);
    return 1;
}
