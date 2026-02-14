/*
** Lua函数信息解析器
** 功能：
** 1. parse_function_info: 解析函数的基本信息（参数、局部变量、指令数量等）
** 2. get_function_instructions: 获取函数的所有指令详细信息
*/

#define ltranslator_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lobject.h"
#include "lstate.h"
#include "lfunc.h"
#include "lopcodes.h"
#include "lopnames.h"

/* 辅助函数：获取操作码名称 */
static const char *get_opcode_name(Instruction i) {
    OpCode o = GET_OPCODE(i);
    return opnames[o];
}

/* 辅助函数：获取操作码模式 */
static const char *get_opcode_mode(Instruction i) {
    OpCode o = GET_OPCODE(i);
    switch (getOpMode(o)) {
        case iABC:
            return "ABC";
        case iABx:
            return "ABx";
        case iAsBx:
            return "AsBx";
        case iAx:
            return "Ax";
        case isJ:
            return "sJ";
        default:
            return "unknown";
    }
}

/* Lua: parse_function_info(func) - 解析函数的基本信息 */
static int l_pfi(lua_State *L) {
    /* 检查参数是否为函数 */
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "expected function");
    }
    
    /* 将函数复制到栈顶 */
    lua_pushvalue(L, 1);
    
    /* 获取栈顶的LClosure对象 */
    TValue *func_val = s2v(L->top.p - 1);
    if (!isLfunction(func_val)) {
        lua_pop(L, 1);
        return luaL_error(L, "not a Lua closure");
    }
    
    /* 获取Proto结构体 */
    const Proto *f = getproto(func_val);
    if (f == NULL) {
        lua_pop(L, 1);
        return luaL_error(L, "failed to get proto from function");
    }
    
    /* 创建一个表来存储函数信息 */
    lua_newtable(L);
    
    /* 基本信息 */
    lua_pushstring(L, "source");
    lua_pushstring(L, f->source ? getstr(f->source) : "[unknown]");
    lua_settable(L, -3);
    
    lua_pushstring(L, "linedefined");
    lua_pushinteger(L, f->linedefined);
    lua_settable(L, -3);
    
    lua_pushstring(L, "lastlinedefined");
    lua_pushinteger(L, f->lastlinedefined);
    lua_settable(L, -3);
    
    lua_pushstring(L, "numparams");
    lua_pushinteger(L, f->numparams);
    lua_settable(L, -3);
    
    lua_pushstring(L, "is_vararg");
    lua_pushboolean(L, f->is_vararg);
    lua_settable(L, -3);
    
    lua_pushstring(L, "maxstacksize");
    lua_pushinteger(L, f->maxstacksize);
    lua_settable(L, -3);
    
    lua_pushstring(L, "sizecode");
    lua_pushinteger(L, f->sizecode);
    lua_settable(L, -3);
    
    lua_pushstring(L, "sizek");
    lua_pushinteger(L, f->sizek);
    lua_settable(L, -3);
    
    lua_pushstring(L, "sizelocvars");
    lua_pushinteger(L, f->sizelocvars);
    lua_settable(L, -3);
    
    lua_pushstring(L, "sizeupvalues");
    lua_pushinteger(L, f->sizeupvalues);
    lua_settable(L, -3);
    
    lua_pushstring(L, "sizep");
    lua_pushinteger(L, f->sizep);
    lua_settable(L, -3);
    
    /* 弹出复制的函数 */
    lua_remove(L, -2);
    
    return 1;
}

/* Lua: get_function_instructions(func) - 获取函数的所有指令详细信息 */
static int l_gfi(lua_State *L) {
    /* 检查参数是否为函数 */
    if (!lua_isfunction(L, 1)) {
        return luaL_error(L, "expected function");
    }
    
    /* 将函数复制到栈顶 */
    lua_pushvalue(L, 1);
    
    /* 获取栈顶的LClosure对象 */
    TValue *func_val = s2v(L->top.p - 1);
    if (!isLfunction(func_val)) {
        lua_pop(L, 1);
        return luaL_error(L, "not a Lua closure");
    }
    
    /* 获取Proto结构体 */
    const Proto *f = getproto(func_val);
    if (f == NULL) {
        lua_pop(L, 1);
        return luaL_error(L, "failed to get proto from function");
    }
    
    /* 创建一个表来存储所有指令 */
    lua_newtable(L);
    
    /* 遍历所有指令 */
    int pc;
    for (pc = 0; pc < f->sizecode; pc++) {
        Instruction i = f->code[pc];
        OpCode o = GET_OPCODE(i);
        
        /* 创建一个表来存储当前指令的信息 */
        lua_newtable(L);
        
        /* 基本信息 */
        lua_pushstring(L, "pc");
        lua_pushinteger(L, pc);
        lua_settable(L, -3);
        
        lua_pushstring(L, "opcode");
        lua_pushstring(L, get_opcode_name(i));
        lua_settable(L, -3);
        
        lua_pushstring(L, "mode");
        lua_pushstring(L, get_opcode_mode(i));
        lua_settable(L, -3);
        
        lua_pushstring(L, "raw");
        lua_pushinteger(L, (lua_Integer)i);
        lua_settable(L, -3);
        
        /* 根据操作码模式添加不同的参数 */
        switch (getOpMode(o)) {
            case iABC: {
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                int k = GETARG_k(i);
                
                lua_pushstring(L, "a");
                lua_pushinteger(L, a);
                lua_settable(L, -3);
                
                lua_pushstring(L, "b");
                lua_pushinteger(L, b);
                lua_settable(L, -3);
                
                lua_pushstring(L, "c");
                lua_pushinteger(L, c);
                lua_settable(L, -3);
                
                lua_pushstring(L, "k");
                lua_pushboolean(L, k);
                lua_settable(L, -3);
                break;
            }
            case iABx: {
                int a = GETARG_A(i);
                int bx = GETARG_Bx(i);
                
                lua_pushstring(L, "a");
                lua_pushinteger(L, a);
                lua_settable(L, -3);
                
                lua_pushstring(L, "bx");
                lua_pushinteger(L, bx);
                lua_settable(L, -3);
                break;
            }
            case iAsBx: {
                int a = GETARG_A(i);
                int sbx = GETARG_sBx(i);
                
                lua_pushstring(L, "a");
                lua_pushinteger(L, a);
                lua_settable(L, -3);
                
                lua_pushstring(L, "sbx");
                lua_pushinteger(L, sbx);
                lua_settable(L, -3);
                break;
            }
            case iAx: {
                int ax = GETARG_Ax(i);
                
                lua_pushstring(L, "ax");
                lua_pushinteger(L, ax);
                lua_settable(L, -3);
                break;
            }
            case isJ: {
                int sj = GETARG_sJ(i);
                
                lua_pushstring(L, "sj");
                lua_pushinteger(L, sj);
                lua_settable(L, -3);
                break;
            }
            case ivABC: {
                int a = GETARG_A(i);
                int vb = GETARG_vB(i);
                int vc = GETARG_vC(i);
                int k = GETARG_k(i);
                
                lua_pushstring(L, "a");
                lua_pushinteger(L, a);
                lua_settable(L, -3);
                
                lua_pushstring(L, "vb");
                lua_pushinteger(L, vb);
                lua_settable(L, -3);
                
                lua_pushstring(L, "vc");
                lua_pushinteger(L, vc);
                lua_settable(L, -3);
                
                lua_pushstring(L, "k");
                lua_pushboolean(L, k);
                lua_settable(L, -3);
                break;
            }
        }
        
        /* 将当前指令的表添加到指令列表中 */
        lua_rawseti(L, -2, pc + 1); /* Lua表索引从1开始 */
    }
    
    /* 弹出复制的函数 */
    lua_remove(L, -2);
    
    return 1;
}

static const luaL_Reg translator_lib[] = {
    {"paser", l_pfi},
    {"get", l_gfi},
    {NULL, NULL}
};

/* 打开函数信息解析器库 */
int luaopen_translator(lua_State *L) {
    luaL_newlib(L, translator_lib);
    return 1;
}
