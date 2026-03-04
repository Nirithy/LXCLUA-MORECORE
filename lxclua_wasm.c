/*
 * lxclua_wasm.c - Lua WASM 导出包装器
 * 将 Lua API 导出为 WASM 模块供 wasm3 加载
 */

#define LUA_CORE
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* ==================== Lua State 管理 ==================== */

/* 创建新的 Lua state */
EXPORT lua_State* lua_wasm_newstate(void) {
    return luaL_newstate();
}

/* 关闭 Lua state */
EXPORT void lua_wasm_close(lua_State* L) {
    lua_close(L);
}

/* 打开标准库 */
EXPORT void lua_wasm_openlibs(lua_State* L) {
    luaL_openlibs(L);
}

/* ==================== 执行 Lua 代码 ==================== */

/* 执行 Lua 字符串 */
EXPORT int lua_wasm_dostring(lua_State* L, const char* str) {
    return luaL_dostring(L, str);
}

/* 加载 Lua 字符串 */
EXPORT int lua_wasm_loadstring(lua_State* L, const char* str) {
    return luaL_loadstring(L, str);
}

/* 执行 Lua 文件（从内存路径） */
EXPORT int lua_wasm_dofile(lua_State* L, const char* filename) {
    return luaL_dofile(L, filename);
}

/* 加载 Lua 文件 */
EXPORT int lua_wasm_loadfile(lua_State* L, const char* filename) {
    return luaL_loadfile(L, filename);
}

/* ==================== 栈操作 ==================== */

/* 获取栈顶索引 */
EXPORT int lua_wasm_gettop(lua_State* L) {
    return lua_gettop(L);
}

/* 设置栈顶 */
EXPORT void lua_wasm_settop(lua_State* L, int idx) {
    lua_settop(L, idx);
}

/* 弹出 n 个元素 */
EXPORT void lua_wasm_pop(lua_State* L, int n) {
    lua_pop(L, n);
}

/* 压入值 */
EXPORT void lua_wasm_pushvalue(lua_State* L, int idx) {
    lua_pushvalue(L, idx);
}

/* 移除元素 */
EXPORT void lua_wasm_remove(lua_State* L, int idx) {
    lua_remove(L, idx);
}

/* 插入元素 */
EXPORT void lua_wasm_insert(lua_State* L, int idx) {
    lua_insert(L, idx);
}

/* 替换元素 */
EXPORT void lua_wasm_replace(lua_State* L, int idx) {
    lua_replace(L, idx);
}

/* 检查栈空间 */
EXPORT int lua_wasm_checkstack(lua_State* L, int n) {
    return lua_checkstack(L, n);
}

/* ==================== 类型检查 ==================== */

/* 获取类型 */
EXPORT int lua_wasm_type(lua_State* L, int idx) {
    return lua_type(L, idx);
}

/* 获取类型名 */
EXPORT const char* lua_wasm_typename(lua_State* L, int tp) {
    return lua_typename(L, tp);
}

/* 是否为 nil */
EXPORT int lua_wasm_isnil(lua_State* L, int idx) {
    return lua_isnil(L, idx);
}

/* 是否为 boolean */
EXPORT int lua_wasm_isboolean(lua_State* L, int idx) {
    return lua_isboolean(L, idx);
}

/* 是否为 number */
EXPORT int lua_wasm_isnumber(lua_State* L, int idx) {
    return lua_isnumber(L, idx);
}

/* 是否为 string */
EXPORT int lua_wasm_isstring(lua_State* L, int idx) {
    return lua_isstring(L, idx);
}

/* 是否为 table */
EXPORT int lua_wasm_istable(lua_State* L, int idx) {
    return lua_istable(L, idx);
}

/* 是否为 function */
EXPORT int lua_wasm_isfunction(lua_State* L, int idx) {
    return lua_isfunction(L, idx);
}

/* 是否为 userdata */
EXPORT int lua_wasm_isuserdata(lua_State* L, int idx) {
    return lua_isuserdata(L, idx);
}

/* 是否为 thread */
EXPORT int lua_wasm_isthread(lua_State* L, int idx) {
    return lua_isthread(L, idx);
}

/* 是否为 lightuserdata */
EXPORT int lua_wasm_islightuserdata(lua_State* L, int idx) {
    return lua_islightuserdata(L, idx);
}

/* ==================== 获取值 ==================== */

/* 转换为 number */
EXPORT lua_Number lua_wasm_tonumber(lua_State* L, int idx) {
    return lua_tonumber(L, idx);
}

/* 转换为 integer */
EXPORT lua_Integer lua_wasm_tointeger(lua_State* L, int idx) {
    return lua_tointeger(L, idx);
}

/* 转换为 boolean */
EXPORT int lua_wasm_toboolean(lua_State* L, int idx) {
    return lua_toboolean(L, idx);
}

/* 转换为 string */
EXPORT const char* lua_wasm_tostring(lua_State* L, int idx) {
    return lua_tostring(L, idx);
}

/* 转换为 lstring */
EXPORT const char* lua_wasm_tolstring(lua_State* L, int idx, size_t* len) {
    return lua_tolstring(L, idx, len);
}

/* 获取字符串长度 */
EXPORT size_t lua_wasm_rawlen(lua_State* L, int idx) {
    return lua_rawlen(L, idx);
}

/* 转换为 userdata */
EXPORT void* lua_wasm_touserdata(lua_State* L, int idx) {
    return lua_touserdata(L, idx);
}

/* 转换为 thread */
EXPORT lua_State* lua_wasm_tothread(lua_State* L, int idx) {
    return lua_tothread(L, idx);
}

/* 转换为 pointer */
EXPORT const void* lua_wasm_topointer(lua_State* L, int idx) {
    return lua_topointer(L, idx);
}

/* ==================== 压入值 ==================== */

/* 压入 nil */
EXPORT void lua_wasm_pushnil(lua_State* L) {
    lua_pushnil(L);
}

/* 压入 number */
EXPORT void lua_wasm_pushnumber(lua_State* L, lua_Number n) {
    lua_pushnumber(L, n);
}

/* 压入 integer */
EXPORT void lua_wasm_pushinteger(lua_State* L, lua_Integer n) {
    lua_pushinteger(L, n);
}

/* 压入 boolean */
EXPORT void lua_wasm_pushboolean(lua_State* L, int b) {
    lua_pushboolean(L, b);
}

/* 压入 string */
EXPORT void lua_wasm_pushstring(lua_State* L, const char* s) {
    lua_pushstring(L, s);
}

/* 压入 lstring */
EXPORT void lua_wasm_pushlstring(lua_State* L, const char* s, size_t len) {
    lua_pushlstring(L, s, len);
}

/* 压入 lightuserdata */
EXPORT void lua_wasm_pushlightuserdata(lua_State* L, void* p) {
    lua_pushlightuserdata(L, p);
}

/* ==================== 表操作 ==================== */

/* 创建表 */
EXPORT void lua_wasm_createtable(lua_State* L, int narr, int nrec) {
    lua_createtable(L, narr, nrec);
}

/* 创建新表 */
EXPORT void lua_wasm_newtable(lua_State* L) {
    lua_newtable(L);
}

/* 获取全局变量 */
EXPORT void lua_wasm_getglobal(lua_State* L, const char* name) {
    lua_getglobal(L, name);
}

/* 设置全局变量 */
EXPORT void lua_wasm_setglobal(lua_State* L, const char* name) {
    lua_setglobal(L, name);
}

/* 获取字段 */
EXPORT void lua_wasm_getfield(lua_State* L, int idx, const char* k) {
    lua_getfield(L, idx, k);
}

/* 设置字段 */
EXPORT void lua_wasm_setfield(lua_State* L, int idx, const char* k) {
    lua_setfield(L, idx, k);
}

/* 获取表元素 */
EXPORT void lua_wasm_gettable(lua_State* L, int idx) {
    lua_gettable(L, idx);
}

/* 设置表元素 */
EXPORT void lua_wasm_settable(lua_State* L, int idx) {
    lua_settable(L, idx);
}

/* 原始获取 */
EXPORT void lua_wasm_rawget(lua_State* L, int idx) {
    lua_rawget(L, idx);
}

/* 原始获取 i */
EXPORT void lua_wasm_rawgeti(lua_State* L, int idx, lua_Integer n) {
    lua_rawgeti(L, idx, n);
}

/* 原始设置 */
EXPORT void lua_wasm_rawset(lua_State* L, int idx) {
    lua_rawset(L, idx);
}

/* 原始设置 i */
EXPORT void lua_wasm_rawseti(lua_State* L, int idx, lua_Integer n) {
    lua_rawseti(L, idx, n);
}

/* 设置元表 */
EXPORT int lua_wasm_setmetatable(lua_State* L, int idx) {
    return lua_setmetatable(L, idx);
}

/* 获取元表 */
EXPORT int lua_wasm_getmetatable(lua_State* L, int idx) {
    return lua_getmetatable(L, idx);
}

/* 表遍历 */
EXPORT int lua_wasm_next(lua_State* L, int idx) {
    return lua_next(L, idx);
}

/* 获取表长度 */
EXPORT int lua_wasm_len(lua_State* L, int idx) {
    lua_len(L, idx);
    return (int)lua_tointeger(L, -1);
}

/* ==================== 调用函数 ==================== */

/* 安全调用 */
EXPORT int lua_wasm_pcall(lua_State* L, int nargs, int nresults, int errfunc) {
    return lua_pcall(L, nargs, nresults, errfunc);
}

/* 调用 */
EXPORT void lua_wasm_call(lua_State* L, int nargs, int nresults) {
    lua_call(L, nargs, nresults);
}

/* 保护调用（带消息处理器） */
EXPORT int lua_wasm_pcallk(lua_State* L, int nargs, int nresults, int errfunc, 
                           lua_KContext ctx, lua_KFunction k) {
    return lua_pcallk(L, nargs, nresults, errfunc, ctx, k);
}

/* ==================== 错误处理 ==================== */

/* 抛出错误 */
EXPORT int lua_wasm_error(lua_State* L) {
    return lua_error(L);
}

/* 抛出字符串错误 */
EXPORT int lua_wasm_errorstring(lua_State* L, const char* msg) {
    return luaL_error(L, "%s", msg);
}

/* ==================== GC 控制 ==================== */

/* 垃圾回收 */
EXPORT int lua_wasm_gc(lua_State* L, int what, int data) {
    return lua_gc(L, what, data);
}

/* 收集垃圾 */
EXPORT void lua_wasm_collectgarbage(lua_State* L) {
    lua_gc(L, LUA_GCCOLLECT, 0);
}

/* 获取内存使用量 */
EXPORT int lua_wasm_memusage(lua_State* L) {
    return lua_gc(L, LUA_GCCOUNT, 0) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0);
}

/* ==================== 引用系统 ==================== */

/* 创建引用 */
EXPORT int lua_wasm_ref(lua_State* L, int t) {
    return luaL_ref(L, t);
}

/* 释放引用 */
EXPORT void lua_wasm_unref(lua_State* L, int t, int ref) {
    luaL_unref(L, t, ref);
}

/* ==================== 注册表操作 ==================== */

/* 获取注册表 */
EXPORT void lua_wasm_getregistry(lua_State* L) {
    lua_pushvalue(L, LUA_REGISTRYINDEX);
}

/* ==================== 辅助函数 ==================== */

/* 获取版本号 */
EXPORT lua_Number lua_wasm_version(void) {
    return lua_version(NULL);
}

/* 比较两个值 */
EXPORT int lua_wasm_compare(lua_State* L, int idx1, int idx2, int op) {
    return lua_compare(L, idx1, idx2, op);
}

/* 相等比较 */
EXPORT int lua_wasm_equal(lua_State* L, int idx1, int idx2) {
    return lua_compare(L, idx1, idx2, LUA_OPEQ);
}

/* 小于比较 */
EXPORT int lua_wasm_lessthan(lua_State* L, int idx1, int idx2) {
    return lua_compare(L, idx1, idx2, LUA_OPLT);
}

/* 原始相等 */
EXPORT int lua_wasm_rawequal(lua_State* L, int idx1, int idx2) {
    return lua_rawequal(L, idx1, idx2);
}

/* ==================== 高级 API ==================== */

/* 执行代码并返回字符串结果 */
EXPORT const char* lua_wasm_eval(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != LUA_OK) {
        return lua_tostring(L, -1);  /* 返回错误消息 */
    }
    if (lua_isstring(L, -1)) {
        return lua_tostring(L, -1);  /* 返回结果 */
    }
    return NULL;
}

/* 执行代码并返回数字结果 */
EXPORT lua_Number lua_wasm_eval_number(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != LUA_OK) {
        return 0;
    }
    return lua_tonumber(L, -1);
}

/* 执行代码并返回整数结果 */
EXPORT lua_Integer lua_wasm_eval_integer(lua_State* L, const char* code) {
    if (luaL_dostring(L, code) != LUA_OK) {
        return 0;
    }
    return lua_tointeger(L, -1);
}

/* 调用全局函数（无参数，返回数字） */
EXPORT lua_Number lua_wasm_call_global_number(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return 0;
    }
    lua_Number result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

/* 调用全局函数（无参数，返回字符串） */
EXPORT const char* lua_wasm_call_global_string(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return NULL;
    }
    const char* result = lua_tostring(L, -1);
    /* 不要 pop，让调用者使用结果 */
    return result;
}

/* 设置全局数字变量 */
EXPORT void lua_wasm_setglobal_number(lua_State* L, const char* name, lua_Number value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

/* 设置全局整数变量 */
EXPORT void lua_wasm_setglobal_integer(lua_State* L, const char* name, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setglobal(L, name);
}

/* 设置全局字符串变量 */
EXPORT void lua_wasm_setglobal_string(lua_State* L, const char* name, const char* value) {
    lua_pushstring(L, value);
    lua_setglobal(L, name);
}

/* 获取全局数字变量 */
EXPORT lua_Number lua_wasm_getglobal_number(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    lua_Number result = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return result;
}

/* 获取全局整数变量 */
EXPORT lua_Integer lua_wasm_getglobal_integer(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    lua_Integer result = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return result;
}

/* 获取全局字符串变量 */
EXPORT const char* lua_wasm_getglobal_string(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    const char* result = lua_tostring(L, -1);
    lua_pop(L, 1);
    return result;
}

/* ==================== 内存操作 ==================== */

/* 分配内存 */
EXPORT void* lua_wasm_malloc(size_t size) {
    return malloc(size);
}

/* 释放内存 */
EXPORT void lua_wasm_free(void* ptr) {
    free(ptr);
}

/* 重新分配内存 */
EXPORT void* lua_wasm_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}
