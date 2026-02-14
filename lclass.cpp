/**
 * @file lclass.c
 * @brief Lua object-oriented system implementation.
 */

#define lclass_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "lstruct.h"


/*
** =====================================================================
** 内部辅助函数
** =====================================================================
*/

static TValue *index2value_helper (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    StkId o = ci->func.p + idx;
    api_check(L, idx <= ci->top.p - (ci->func.p + 1), "unacceptable index");
    if (o >= L->top.p) return &G(L)->nilvalue;
    else return s2v(o);
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {
      /* assume upvalue or other pseudo indices not handled here for now in lclass */
      return &G(L)->nilvalue;
  }
}

/*
** 获取绝对栈索引
** 参数：
**   L - Lua状态机
**   idx - 栈索引（可以是负数）
** 返回值：
**   绝对栈索引
*/
static int absindex(lua_State *L, int idx) {
  if (idx > 0 || idx <= LUA_REGISTRYINDEX)
    return idx;
  return cast_int(L->top.p - L->ci->func.p) + idx;
}


/*
** 在表中设置字符串键的布尔值（使用rawset避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 键名
**   value - 布尔值
*/
static void setboolfield(lua_State *L, int t_idx, const char *key, int value) {
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_pushboolean(L, value);
  lua_rawset(L, t_idx);
}


/*
** 在表中设置字符串键的字符串值（使用rawset避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 键名
**   value - 字符串值
*/
static void setstrfield(lua_State *L, int t_idx, const char *key, const char *value) {
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_pushstring(L, value);
  lua_rawset(L, t_idx);
}


/*
** 检查表是否有指定的布尔标记
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 标记键名
** 返回值：
**   标记值（0或1）
*/
static int checkflag(lua_State *L, int t_idx, const char *key) {
  int result;
  t_idx = absindex(L, t_idx);
  lua_getfield(L, t_idx, key);
  result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}


/*
** 检查表是否有指定的布尔标记（使用rawget避免触发元方法）
** 参数：
**   L - Lua状态机
**   t_idx - 表在栈中的索引
**   key - 标记键名
** 返回值：
**   标记值（0或1）
*/
static int checkflag_raw(lua_State *L, int t_idx, const char *key) {
  int result;
  t_idx = absindex(L, t_idx);
  lua_pushstring(L, key);
  lua_rawget(L, t_idx);
  result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}


/*
** 复制表的所有键值对到另一个表
** 参数：
**   L - Lua状态机
**   src_idx - 源表索引
**   dst_idx - 目标表索引
*/
static void copytable(lua_State *L, int src_idx, int dst_idx) {
  src_idx = absindex(L, src_idx);
  dst_idx = absindex(L, dst_idx);
  
  lua_pushnil(L);  /* 第一个键 */
  while (lua_next(L, src_idx) != 0) {
    /* 栈顶: value, key */
    lua_pushvalue(L, -2);  /* 复制key */
    lua_pushvalue(L, -2);  /* 复制value */
    lua_settable(L, dst_idx);
    lua_pop(L, 1);  /* 移除value，保留key用于下一次迭代 */
  }
}


/*
** =====================================================================
** 类元方法实现
** =====================================================================
*/

/*
** 类的__call元方法 - 用于创建实例
** 语法: local obj = ClassName(args...)
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回新创建的对象
*/
static int class_call(lua_State *L) {
  int nargs = lua_gettop(L) - 1;  /* 排除类本身 */
  
  /* 检查第一个参数是否是类 */
  if (!luaC_isclass(L, 1)) {
    luaL_error(L, "尝试调用非类值");
    return 0;
  }
  
  /* 创建新对象实例 */
  luaC_newobject(L, 1, nargs);
  return 1;
}


/*
** 类的__index元方法 - 用于访问类成员
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回找到的值或nil
*/
static int class_index(lua_State *L) {
  /* 栈: [1]=类表, [2]=键 */
  
  /* 首先在类自身的方法表中查找（使用rawget避免递归） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);  /* 键 */
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;  /* 找到了 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 然后在静态成员表中查找 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);  /* 键 */
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;  /* 找到了 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 最后在父类中查找 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);  /* 键 */
    lua_gettable(L, -2);  /* 父类可以触发其__index */
    return 1;
  }
  
  lua_pushnil(L);
  return 1;
}


/*
** 类的__newindex元方法 - 用于设置类成员
** 参数：
**   L - Lua状态机
** 返回值：
**   0
*/
static int class_newindex(lua_State *L) {
  /* 栈: [1]=类表, [2]=键, [3]=值 */
  
  /* 如果值是函数，设置到方法表（使用rawget/rawset避免递归） */
  if (lua_isfunction(L, 3)) {
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, 1);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_insert(L, -2);
      lua_rawset(L, 1);
    }
    lua_pushvalue(L, 2);  /* 键 */
    lua_pushvalue(L, 3);  /* 值 */
    lua_rawset(L, -3);
  } else {
    /* 否则设置到静态成员表 */
    lua_pushstring(L, CLASS_KEY_STATICS);
    lua_rawget(L, 1);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_STATICS);
      lua_insert(L, -2);
      lua_rawset(L, 1);
    }
    lua_pushvalue(L, 2);  /* 键 */
    lua_pushvalue(L, 3);  /* 值 */
    lua_rawset(L, -3);
  }
  
  return 0;
}


/*
** 类的__tostring元方法
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回类名字符串
*/
static int class_tostring(lua_State *L) {
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, 1);
  if (lua_isstring(L, -1)) {
    lua_pushfstring(L, "class: %s", lua_tostring(L, -1));
  } else {
    lua_pushfstring(L, "class: %p", lua_topointer(L, 1));
  }
  return 1;
}


/*
** =====================================================================
** 对象元方法实现
** =====================================================================
*/


/*
** 获取调用栈中调用者所属的类
** 参数：
**   L - Lua状态机
**   obj_class_idx - 被访问对象的类在栈中的索引
** 返回值：
**   ACCESS_PUBLIC - 外部调用，只能访问公开成员
**   ACCESS_PROTECTED - 子类调用，可访问公开和受保护成员
**   ACCESS_PRIVATE - 同类调用，可访问所有成员
** 说明：
**   通过遍历调用栈，查找是否存在当前对象或其子类的方法调用
**   使用 self 参数来判断调用者上下文
*/
static int get_caller_access_level(lua_State *L, int obj_class_idx) {
  lua_Debug ar;
  int level = 1;  /* 从调用者开始（跳过当前函数） */
  
  obj_class_idx = absindex(L, obj_class_idx);
  
  /* 遍历调用栈 */
  while (lua_getstack(L, level, &ar)) {
    /* 获取栈帧的函数信息 */
    if (lua_getinfo(L, "nSlu", &ar) == 0) {
      level++;
      continue;
    }
    
    /* 检查第一个局部变量（通常是 self） */
    const char *name = lua_getlocal(L, &ar, 1);
    if (name != NULL) {
      /* 检查是否是 self 参数 */
      if (strcmp(name, "self") == 0 && lua_istable(L, -1)) {
        /* 获取 self 对象的类 */
        lua_pushstring(L, OBJ_KEY_CLASS);
        lua_rawget(L, -2);
        
        if (lua_istable(L, -1)) {
          int caller_class_idx = lua_gettop(L);
          
          /* 检查是否是同一个类 */
          if (lua_rawequal(L, caller_class_idx, obj_class_idx)) {
            lua_pop(L, 2);  /* 移除 caller_class 和 self */
            return ACCESS_PRIVATE;  /* 同类，可访问私有成员 */
          }
          
          /* 检查调用者类是否是目标类的子类 */
          lua_pushstring(L, CLASS_KEY_PARENT);
          lua_rawget(L, caller_class_idx);
          while (lua_istable(L, -1)) {
            if (lua_rawequal(L, -1, obj_class_idx)) {
              lua_pop(L, 3);  /* 移除 parent, caller_class, self */
              return ACCESS_PROTECTED;  /* 子类，可访问受保护成员 */
            }
            lua_pushstring(L, CLASS_KEY_PARENT);
            lua_rawget(L, -2);
            lua_remove(L, -2);
          }
          lua_pop(L, 1);  /* 移除 nil（非表值） */
          
          /* 检查目标类是否是调用者类的子类（即调用者是父类方法） */
          lua_pushstring(L, CLASS_KEY_PARENT);
          lua_rawget(L, obj_class_idx);
          while (lua_istable(L, -1)) {
            if (lua_rawequal(L, -1, caller_class_idx)) {
              lua_pop(L, 3);  /* 移除 parent, caller_class, self */
              return ACCESS_PROTECTED;  /* 父类方法访问子类对象，允许受保护访问 */
            }
            lua_pushstring(L, CLASS_KEY_PARENT);
            lua_rawget(L, -2);
            lua_remove(L, -2);
          }
          lua_pop(L, 1);  /* 移除 nil */
          
          lua_pop(L, 1);  /* 移除 caller_class */
        } else {
          lua_pop(L, 1);  /* 移除非表值 */
        }
      }
      lua_pop(L, 1);  /* 移除局部变量值 */
    }
    
    level++;
  }
  
  /* 没有找到合适的调用者上下文，视为外部调用 */
  return ACCESS_PUBLIC;
}


/*
** 检查成员存在于哪个访问级别表中
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   key_idx - 键在栈中的索引
** 返回值：
**   ACCESS_PUBLIC - 公开成员
**   ACCESS_PROTECTED - 受保护成员
**   ACCESS_PRIVATE - 私有成员
**   -1 - 成员不存在
** 说明：
**   检查成员在类中的访问级别
*/
static int get_member_access_level(lua_State *L, int class_idx, int key_idx) {
  class_idx = absindex(L, class_idx);
  key_idx = absindex(L, key_idx);
  
  /* 检查公开方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PUBLIC;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护成员 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PROTECTED;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有成员 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, key_idx);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return ACCESS_PRIVATE;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  return -1;  /* 成员不存在 */
}


/*
** 获取类名（用于错误消息）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   类名字符串，如果获取失败返回 "unknown"
** 说明：
**   使用静态缓冲区存储类名，避免弹出栈后指针悬空
**   注意：返回的字符串在下次调用本函数时会被覆盖
*/
static const char* get_class_name_str(lua_State *L, int class_idx) {
  static char classname_buf[256];  /* 静态缓冲区，存储类名 */
  class_idx = absindex(L, class_idx);
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, class_idx);
  const char *name = lua_tostring(L, -1);
  if (name == NULL) {
    lua_pop(L, 1);
    return "unknown";
  }
  /* 将类名复制到静态缓冲区 */
  size_t len = strlen(name);
  if (len >= sizeof(classname_buf)) {
    len = sizeof(classname_buf) - 1;
  }
  memcpy(classname_buf, name, len);
  classname_buf[len] = '\0';
  lua_pop(L, 1);
  return classname_buf;
}


/*
** 对象的__index元方法 - 用于访问对象属性和方法
** 支持访问控制：public成员可自由访问，protected和private成员有限制
** 支持getter属性访问器
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回找到的值或nil
*/
static int object_index(lua_State *L) {
  /* 栈: [1]=对象, [2]=键 */
  
  /* 获取对象所属的类（使用rawget避免递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    /* 不是对象，直接在表中查找 */
    lua_pushvalue(L, 2);
    lua_rawget(L, 1);
    return 1;
  }
  int class_idx = lua_gettop(L);
  
  /* 确定调用者的访问级别（提前获取，用于getter权限检查） */
  int caller_access = get_caller_access_level(L, class_idx);
  
  /* 检查getter（在继承链中查找，支持公开/受保护/私有getter） */
  /* 使用更安全的栈管理方式：不在循环中删除元素 */
  lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶作为遍历起点 */
  int iter_idx = lua_gettop(L);
  int is_first_class_getter = 1;  /* 标记是否是对象直接所属的类 */
  
  while (lua_istable(L, iter_idx)) {
    /* 根据访问级别依次检查不同的getter表 */
    
    /* 1. 私有getter - 只有同类方法可以访问，且只在第一个类（对象直接所属类）中查找 */
    if (caller_access == ACCESS_PRIVATE && is_first_class_getter) {
      lua_pushstring(L, CLASS_KEY_PRIVATE_GETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到私有getter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_call(L, 1, 1);
          return 1;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除私有getters表 */
    }
    
    /* 2. 受保护getter - 同类或子类方法可以访问 */
    if (caller_access == ACCESS_PRIVATE || caller_access == ACCESS_PROTECTED) {
      lua_pushstring(L, CLASS_KEY_PROTECTED_GETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到受保护getter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_call(L, 1, 1);
          return 1;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除受保护getters表 */
    }
    
    /* 3. 公开getter - 任何人都可以访问 */
    lua_pushstring(L, CLASS_KEY_GETTERS);
    lua_rawget(L, iter_idx);
    if (lua_istable(L, -1)) {
      lua_pushvalue(L, 2);  /* 键 */
      lua_rawget(L, -2);
      if (lua_isfunction(L, -1)) {
        /* 找到公开getter，调用它 */
        lua_pushvalue(L, 1);  /* self */
        lua_call(L, 1, 1);
        return 1;
      }
      lua_pop(L, 1);  /* 移除查找结果 */
    }
    lua_pop(L, 1);  /* 移除公开getters表 */
    
    /* 继续查找父类 */
    is_first_class_getter = 0;  /* 后续都不是第一个类了 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, iter_idx);
    lua_replace(L, iter_idx);  /* 用父类替换当前迭代位置 */
  }
  lua_pop(L, 1);  /* 移除最后的非表值（nil） */
  
  /* 在对象自身查找（实例属性） */
  lua_pushvalue(L, 2);
  lua_rawget(L, 1);
  if (!lua_isnil(L, -1)) {
    /* 检查不是内部键 */
    if (lua_isstring(L, 2)) {
      const char *key = lua_tostring(L, 2);
      /* 内部键以双下划线开头，不允许外部直接访问 */
      if (key && key[0] == '_' && key[1] == '_') {
        /* 这是内部键，需要检查权限 */
        lua_pop(L, 1);
        /* 继续下面的处理流程 */
      } else {
        return 1;  /* 普通实例属性，允许访问 */
      }
    } else {
      return 1;
    }
  } else {
    lua_pop(L, 1);
  }
  
  /* 在类及其继承链中查找成员 */
  /* 使用更安全的栈管理方式：复制一份用于迭代 */
  lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶 */
  int current_class = lua_gettop(L);
  int is_first_class = 1;  /* 标记是否是对象直接所属的类 */
  
  while (lua_istable(L, current_class)) {
    /* 确定当前类中成员的访问级别 */
    int member_access = get_member_access_level(L, current_class, 2);
    
    if (member_access >= 0) {
      /* 找到了成员，检查访问权限 */
      
      /* 私有成员：只有同类方法可以访问 */
      if (member_access == ACCESS_PRIVATE) {
        if (!is_first_class) {
          /* 父类的私有成员不可被子类访问 */
          goto next_class;
        }
        if (caller_access != ACCESS_PRIVATE) {
          /* 外部或子类不能访问私有成员 */
          const char *classname = get_class_name_str(L, class_idx);
          const char *key = lua_tostring(L, 2);
          return luaL_error(L, "无法访问类 '%s' 的私有成员 '%s'", 
                           classname, key ? key : "?");
        }
      }
      
      /* 受保护成员：只有同类或子类方法可以访问 */
      if (member_access == ACCESS_PROTECTED) {
        if (caller_access == ACCESS_PUBLIC) {
          /* 外部不能访问受保护成员 */
          const char *classname = get_class_name_str(L, class_idx);
          const char *key = lua_tostring(L, 2);
          return luaL_error(L, "无法访问类 '%s' 的受保护成员 '%s'", 
                           classname, key ? key : "?");
        }
      }
      
      /* 访问权限检查通过，获取成员值 */
      const char *table_key;
      if (member_access == ACCESS_PRIVATE) {
        table_key = CLASS_KEY_PRIVATES;
      } else if (member_access == ACCESS_PROTECTED) {
        table_key = CLASS_KEY_PROTECTED;
      } else {
        table_key = CLASS_KEY_METHODS;
      }
      
      lua_pushstring(L, table_key);
      lua_rawget(L, current_class);
      lua_pushvalue(L, 2);
      lua_rawget(L, -2);
      return 1;
    }
    
next_class:
    /* 继续查找父类 */
    is_first_class = 0;
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_replace(L, current_class);  /* 用父类替换当前迭代位置 */
  }
  lua_pop(L, 1);  /* 移除迭代用的类引用 */
  
  /* 检查对象私有数据表（实例级别的私有数据） */
  lua_pushstring(L, OBJ_KEY_PRIVATES);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      /* 实例私有数据也需要权限检查 */
      if (caller_access != ACCESS_PRIVATE) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "无法访问对象 '%s' 的私有数据 '%s'", 
                         classname, key ? key : "?");
      }
      lua_remove(L, -2);
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查静态成员 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  lua_pushnil(L);
  return 1;
}


/*
** 对象的__newindex元方法 - 用于设置对象属性
** 支持访问控制：防止外部修改私有/受保护成员
** 支持setter属性访问器
** 参数：
**   L - Lua状态机
** 返回值：
**   0
*/
static int object_newindex(lua_State *L) {
  /* 栈: [1]=对象, [2]=键, [3]=值 */
  
  /* 获取对象的类 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    int class_idx = lua_gettop(L);
    
    /* 确定调用者的访问级别（提前获取，用于setter权限检查） */
    int caller_access = get_caller_access_level(L, class_idx);
    
    /* 检查setter（在继承链中查找，支持公开/受保护/私有setter） */
    /* 使用更安全的栈管理方式：不在循环中删除元素 */
    lua_pushvalue(L, class_idx);  /* 复制类引用到栈顶作为遍历起点 */
    int iter_idx = lua_gettop(L);
    int is_first_class_setter = 1;  /* 标记是否是对象直接所属的类 */
    
    while (lua_istable(L, iter_idx)) {
      /* 根据访问级别依次检查不同的setter表 */
      
      /* 1. 私有setter - 只有同类方法可以访问，且只在第一个类中查找 */
      if (caller_access == ACCESS_PRIVATE && is_first_class_setter) {
        lua_pushstring(L, CLASS_KEY_PRIVATE_SETTERS);
        lua_rawget(L, iter_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, 2);  /* 键 */
          lua_rawget(L, -2);
          if (lua_isfunction(L, -1)) {
            /* 找到私有setter，调用它 */
            lua_pushvalue(L, 1);  /* self */
            lua_pushvalue(L, 3);  /* value */
            lua_call(L, 2, 0);
            return 0;
          }
          lua_pop(L, 1);  /* 移除查找结果 */
        }
        lua_pop(L, 1);  /* 移除私有setters表 */
      }
      
      /* 2. 受保护setter - 同类或子类方法可以访问 */
      if (caller_access == ACCESS_PRIVATE || caller_access == ACCESS_PROTECTED) {
        lua_pushstring(L, CLASS_KEY_PROTECTED_SETTERS);
        lua_rawget(L, iter_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, 2);  /* 键 */
          lua_rawget(L, -2);
          if (lua_isfunction(L, -1)) {
            /* 找到受保护setter，调用它 */
            lua_pushvalue(L, 1);  /* self */
            lua_pushvalue(L, 3);  /* value */
            lua_call(L, 2, 0);
            return 0;
          }
          lua_pop(L, 1);  /* 移除查找结果 */
        }
        lua_pop(L, 1);  /* 移除受保护setters表 */
      }
      
      /* 3. 公开setter - 任何人都可以访问 */
      lua_pushstring(L, CLASS_KEY_SETTERS);
      lua_rawget(L, iter_idx);
      if (lua_istable(L, -1)) {
        lua_pushvalue(L, 2);  /* 键 */
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
          /* 找到公开setter，调用它 */
          lua_pushvalue(L, 1);  /* self */
          lua_pushvalue(L, 3);  /* value */
          lua_call(L, 2, 0);
          return 0;
        }
        lua_pop(L, 1);  /* 移除查找结果 */
      }
      lua_pop(L, 1);  /* 移除公开setters表 */
      
      /* 继续查找父类 */
      is_first_class_setter = 0;  /* 后续都不是第一个类了 */
      lua_pushstring(L, CLASS_KEY_PARENT);
      lua_rawget(L, iter_idx);
      lua_replace(L, iter_idx);  /* 用父类替换当前迭代位置 */
    }
    lua_pop(L, 1);  /* 移除最后的非表值（nil） */
    
    /* 检查是否尝试设置内部键 */
    if (lua_isstring(L, 2)) {
      const char *key = lua_tostring(L, 2);
      if (key && key[0] == '_' && key[1] == '_') {
        /* 尝试设置内部键，需要检查权限 */
        if (caller_access != ACCESS_PRIVATE) {
          const char *classname = get_class_name_str(L, class_idx);
          return luaL_error(L, "无法从外部修改对象 '%s' 的内部属性 '%s'", 
                           classname, key);
        }
      }
    }
    
    /* 检查是否尝试覆盖类成员 */
    int member_access = get_member_access_level(L, class_idx, 2);
    
    if (member_access >= 0) {
      /* 尝试设置类成员，检查权限 */
      if (member_access == ACCESS_PRIVATE && caller_access != ACCESS_PRIVATE) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "无法从外部修改类 '%s' 的私有成员 '%s'", 
                         classname, key ? key : "?");
      }
      
      if (member_access == ACCESS_PROTECTED && caller_access == ACCESS_PUBLIC) {
        const char *classname = get_class_name_str(L, class_idx);
        const char *key = lua_tostring(L, 2);
        return luaL_error(L, "无法从外部修改类 '%s' 的受保护成员 '%s'", 
                         classname, key ? key : "?");
      }
    }
  }
  lua_pop(L, 1);
  
  /* 权限检查通过，设置属性 */
  lua_pushvalue(L, 2);
  lua_pushvalue(L, 3);
  lua_rawset(L, 1);
  return 0;
}


/*
** 对象的__tostring元方法
** 参数：
**   L - Lua状态机
** 返回值：
**   1 - 返回对象描述字符串
*/
static int object_tostring(lua_State *L) {
  /* 首先检查对象是否有自定义的__tostring方法 */
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, 1);
  if (lua_istable(L, -1)) {
    /* 使用rawget访问类表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
      lua_pushstring(L, "__tostring");
      lua_rawget(L, -2);
      if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);  /* self */
        lua_call(L, 1, 1);
        return 1;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 使用默认格式 */
    lua_pushstring(L, CLASS_KEY_NAME);
    lua_rawget(L, -2);
    if (lua_isstring(L, -1)) {
      lua_pushfstring(L, "<%s object: %p>", lua_tostring(L, -1), lua_topointer(L, 1));
      return 1;
    }
  }
  
  lua_pushfstring(L, "<object: %p>", lua_topointer(L, 1));
  return 1;
}


/*
** =====================================================================
** 类系统核心函数实现
** =====================================================================
*/

/*
** 创建新类
*/
void luaC_newclass(lua_State *L, TString *name) {
  /* 创建类表 */
  lua_newtable(L);
  int class_idx = lua_gettop(L);
  
  /* 设置类名（使用rawset避免触发元方法） */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_rawset(L, class_idx);
  
  /* 标记为类 */
  setboolfield(L, class_idx, CLASS_KEY_ISCLASS, 1);
  
  /* 初始化类标志为0 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, 0);
  lua_rawset(L, class_idx);
  
  /* 创建方法表（公开成员） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建静态成员表 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有成员表 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护成员表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建抽象方法表 */
  lua_pushstring(L, CLASS_KEY_ABSTRACTS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建getter方法表（公开） */
  lua_pushstring(L, CLASS_KEY_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建setter方法表（公开） */
  lua_pushstring(L, CLASS_KEY_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有getter方法表 */
  lua_pushstring(L, CLASS_KEY_PRIVATE_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建私有setter方法表 */
  lua_pushstring(L, CLASS_KEY_PRIVATE_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护getter方法表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED_GETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建受保护setter方法表 */
  lua_pushstring(L, CLASS_KEY_PROTECTED_SETTERS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建接口列表 */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建成员标志表 */
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_newtable(L);
  lua_rawset(L, class_idx);
  
  /* 创建并设置类的元表 */
  lua_newtable(L);
  int mt_idx = lua_gettop(L);
  
  /* 设置__call元方法（用于实例化） */
  lua_pushcfunction(L, class_call);
  lua_setfield(L, mt_idx, "__call");
  
  /* 设置__index元方法 */
  lua_pushcfunction(L, class_index);
  lua_setfield(L, mt_idx, "__index");
  
  /* 设置__newindex元方法 */
  lua_pushcfunction(L, class_newindex);
  lua_setfield(L, mt_idx, "__newindex");
  
  /* 设置__tostring元方法 */
  lua_pushcfunction(L, class_tostring);
  lua_setfield(L, mt_idx, "__tostring");
  
  /* 应用元表 */
  lua_setmetatable(L, class_idx);
  
  /* 类表现在在栈顶 */
}


/*
** 设置类的继承关系
** 支持final类检查、final方法检查、抽象方法继承、getter/setter继承
*/
void luaC_inherit(lua_State *L, int child_idx, int parent_idx) {
  child_idx = absindex(L, child_idx);
  parent_idx = absindex(L, parent_idx);
  
  /* 检查父类是否是有效的类 */
  if (!luaC_isclass(L, parent_idx)) {
    luaL_error(L, "父类不是有效的类");
    return;
  }
  
  /* 检查父类是否是final类或sealed类 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, parent_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_FINAL) {
      const char *parent_name = get_class_name_str(L, parent_idx);
      luaL_error(L, "不能继承final类 '%s'", parent_name);
      return;
    }
    if (flags & CLASS_FLAG_SEALED) {
      const char *parent_name = get_class_name_str(L, parent_idx);
      luaL_error(L, "不能继承sealed类 '%s'", parent_name);
      return;
    }
  }
  lua_pop(L, 1);
  
  /* 设置父类引用（使用rawset避免触发__newindex） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_pushvalue(L, parent_idx);
  lua_rawset(L, child_idx);
  
  /* 获取子类的方法表用于检查final方法重写 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, child_idx);
  int child_methods_for_check = lua_gettop(L);
  
  /* 检查子类是否尝试重写父类的final方法 */
  if (lua_istable(L, child_methods_for_check)) {
    lua_pushnil(L);
    while (lua_next(L, child_methods_for_check) != 0) {
      lua_pop(L, 1);  /* 移除value，只需要key */
      /* 检查这个方法是否是父类的final方法 */
      if (lua_isstring(L, -1)) {
        const char *method_name = lua_tostring(L, -1);
        /* 直接在父类的finals表中查找 */
        lua_pushstring(L, CLASS_KEY_FINALS);
        lua_rawget(L, parent_idx);
        if (lua_istable(L, -1)) {
          lua_pushvalue(L, -2);  /* 方法名 */
          lua_rawget(L, -2);
          if (lua_toboolean(L, -1)) {
            const char *parent_name = get_class_name_str(L, parent_idx);
            luaL_error(L, "不能重写类 '%s' 的final方法 '%s'", 
                       parent_name, method_name);
            return;
          }
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      }
    }
  }
  lua_pop(L, 1);
  
  /* 复制父类的公开方法到子类（实现继承，使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_methods = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_METHODS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_methods = lua_gettop(L);
    
    /* 复制方法（子类已有的方法不覆盖） */
    lua_pushnil(L);
    while (lua_next(L, parent_methods) != 0) {
      /* 检查子类是否已有该方法 */
      lua_pushvalue(L, -2);  /* 复制key */
      lua_rawget(L, child_methods);
      if (lua_isnil(L, -1)) {
        /* 子类没有这个方法，从父类复制 */
        lua_pop(L, 1);  /* 移除nil */
        lua_pushvalue(L, -2);  /* key */
        lua_pushvalue(L, -2);  /* value */
        lua_rawset(L, child_methods);
      } else {
        lua_pop(L, 1);  /* 移除已有的值 */
      }
      lua_pop(L, 1);  /* 移除value */
    }
    
    lua_pop(L, 1);  /* 移除child_methods */
  }
  lua_pop(L, 1);  /* 移除parent_methods */
  
  /* 复制父类的受保护成员到子类（子类可以访问，使用rawget） */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_protected = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_PROTECTED);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_protected = lua_gettop(L);
    
    /* 复制受保护成员（子类已有的不覆盖） */
    lua_pushnil(L);
    while (lua_next(L, parent_protected) != 0) {
      lua_pushvalue(L, -2);  /* 复制key */
      lua_rawget(L, child_protected);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);  /* 移除nil */
        lua_pushvalue(L, -2);  /* key */
        lua_pushvalue(L, -2);  /* value */
        lua_rawset(L, child_protected);
      } else {
        lua_pop(L, 1);  /* 移除已有的值 */
      }
      lua_pop(L, 1);  /* 移除value */
    }
    
    lua_pop(L, 1);  /* 移除child_protected */
  }
  lua_pop(L, 1);  /* 移除parent_protected */
  
  /* 复制父类的getter表（子类已有的不覆盖） */
  lua_pushstring(L, CLASS_KEY_GETTERS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_getters = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_GETTERS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_GETTERS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_getters = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_getters) != 0) {
      lua_pushvalue(L, -2);
      lua_rawget(L, child_getters);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, child_getters);
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 复制父类的setter表（子类已有的不覆盖） */
  lua_pushstring(L, CLASS_KEY_SETTERS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_setters = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_SETTERS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_SETTERS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_setters = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_setters) != 0) {
      lua_pushvalue(L, -2);
      lua_rawget(L, child_setters);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_rawset(L, child_setters);
      } else {
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 复制父类的final方法表（合并） */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, parent_idx);
  if (lua_istable(L, -1)) {
    int parent_finals = lua_gettop(L);
    
    lua_pushstring(L, CLASS_KEY_FINALS);
    lua_rawget(L, child_idx);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_pushstring(L, CLASS_KEY_FINALS);
      lua_insert(L, -2);
      lua_rawset(L, child_idx);
    }
    int child_finals = lua_gettop(L);
    
    lua_pushnil(L);
    while (lua_next(L, parent_finals) != 0) {
      lua_pushvalue(L, -2);
      lua_pushvalue(L, -2);
      lua_rawset(L, child_finals);
      lua_pop(L, 1);
    }
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 注意：私有成员不被继承 */
}


/*
** 创建类的实例对象
** 支持自动调用父类构造函数链
*/
void luaC_newobject(lua_State *L, int class_idx, int nargs) {
  class_idx = absindex(L, class_idx);
  
  /* 检查是否是有效的类 */
  if (!luaC_isclass(L, class_idx)) {
    luaL_error(L, "尝试实例化非类值");
    return;
  }
  
  /* 检查是否是抽象类（使用rawget避免触发类的__index） */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      luaL_error(L, "不能实例化抽象类");
      return;
    }
  }
  lua_pop(L, 1);
  
  /* 验证所有抽象方法都已实现（包括参数数量验证） */
  luaC_verify_abstracts(L, class_idx);
  
  /* 验证所有接口方法都已正确实现（包括参数数量验证） */
  luaC_verify_interfaces(L, class_idx);
  
  /* 创建对象表 */
  lua_newtable(L);
  int obj_idx = lua_gettop(L);
  
  /* 保存对类的引用（使用rawset因为对象还没有元表） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_pushvalue(L, class_idx);
  lua_rawset(L, obj_idx);
  
  /* 标记为对象 */
  lua_pushstring(L, OBJ_KEY_ISOBJ);
  lua_pushboolean(L, 1);
  lua_rawset(L, obj_idx);
  
  /* 创建对象私有数据表 */
  lua_pushstring(L, OBJ_KEY_PRIVATES);
  lua_newtable(L);
  lua_rawset(L, obj_idx);
  
  /* 创建并设置对象的元表 */
  lua_newtable(L);
  int mt_idx = lua_gettop(L);
  
  /* 设置__index元方法 */
  lua_pushcfunction(L, object_index);
  lua_setfield(L, mt_idx, "__index");
  
  /* 设置__newindex元方法 */
  lua_pushcfunction(L, object_newindex);
  lua_setfield(L, mt_idx, "__newindex");
  
  /* 设置__tostring元方法 */
  lua_pushcfunction(L, object_tostring);
  lua_setfield(L, mt_idx, "__tostring");
  
  /* 检查类是否有__gc方法（使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushstring(L, CLASS_KEY_DESTRUCTOR);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      lua_setfield(L, mt_idx, "__gc");
    } else {
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  
  /* 应用元表 */
  lua_setmetatable(L, obj_idx);
  
  /* 收集继承链中的所有类（从最顶层父类到当前类） */
  lua_newtable(L);  /* 构造函数调用链 */
  int chain_idx = lua_gettop(L);
  int chain_len = 0;
  
  /* 遍历继承链，将所有类压入链表（逆序，最后转为正序） */
  lua_pushvalue(L, class_idx);
  while (lua_istable(L, -1)) {
    chain_len++;
    lua_pushvalue(L, -1);
    lua_rawseti(L, chain_idx, chain_len);
    
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, -2);
    lua_remove(L, -2);  /* 移除旧的类引用 */
  }
  lua_pop(L, 1);  /* 移除非表值 */
  
  /* 从最顶层父类开始调用构造函数（chain_len到1） */
  for (int i = chain_len; i >= 1; i--) {
    lua_rawgeti(L, chain_idx, i);
    int current_class = lua_gettop(L);
    
    /* 获取当前类自己的构造函数（不是继承的） */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushstring(L, CLASS_KEY_INIT);
      lua_rawget(L, -2);
      if (lua_isfunction(L, -1)) {
        /* 检查这个构造函数是否是当前类定义的（不是从父类继承的） */
        int is_own_init = 1;
        if (i < chain_len) {
          /* 不是最顶层类，检查父类是否有相同的构造函数 */
          lua_rawgeti(L, chain_idx, i + 1);  /* 获取父类 */
          lua_pushstring(L, CLASS_KEY_METHODS);
          lua_rawget(L, -2);
          if (lua_istable(L, -1)) {
            lua_pushstring(L, CLASS_KEY_INIT);
            lua_rawget(L, -2);
            if (lua_rawequal(L, -1, -5)) {
              /* 与父类的构造函数相同，说明是继承的 */
              is_own_init = 0;
            }
            lua_pop(L, 1);
          }
          lua_pop(L, 2);
        }
        
        if (is_own_init) {
          /* 调用构造函数 */
          lua_pushvalue(L, obj_idx);  /* self */
          
          /* 只有当前类（i==1）才传递构造参数 */
          int args_count = (i == 1) ? nargs : 0;
          int first_arg = class_idx + 1;
          for (int j = 0; j < args_count; j++) {
            lua_pushvalue(L, first_arg + j);
          }
          
          lua_call(L, args_count + 1, 0);
        } else {
          lua_pop(L, 1);  /* 移除继承的构造函数 */
        }
      } else {
        lua_pop(L, 1);  /* 移除非函数值 */
      }
    }
    lua_pop(L, 1);  /* 移除methods表 */
    lua_pop(L, 1);  /* 移除当前类 */
  }
  
  lua_pop(L, 1);  /* 移除chain表 */
  
  /* 确保对象在栈顶 */
  lua_pushvalue(L, obj_idx);
  lua_remove(L, obj_idx);
}


/*
** 调用父类方法
*/
void luaC_super(lua_State *L, int obj_idx, TString *method) {
  obj_idx = absindex(L, obj_idx);
  
  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return;
  }
  
  /* 获取父类（使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, -2);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 2);
    lua_pushnil(L);
    return;
  }
  
  /* 在父类方法表中查找方法（使用rawget） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, -2);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, getstr(method), tsslen(method));
    lua_rawget(L, -2);
    /* 方法现在在栈顶 */
    lua_remove(L, -2);  /* 移除methods表 */
    lua_remove(L, -2);  /* 移除parent */
    lua_remove(L, -2);  /* 移除class */
    return;
  }
  
  lua_pop(L, 3);
  lua_pushnil(L);
}


/*
** 设置类方法
*/
void luaC_setmethod(lua_State *L, int class_idx, TString *name, int func_idx) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 使用rawget/rawset访问类表避免触发元方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置静态成员
*/
void luaC_setstatic(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 使用rawget/rawset访问类表避免触发元方法 */
  lua_pushstring(L, CLASS_KEY_STATICS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_STATICS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 获取属性（考虑继承链）
*/
void luaC_getprop(lua_State *L, int obj_idx, TString *key) {
  obj_idx = absindex(L, obj_idx);
  
  /* 首先在对象自身查找 */
  lua_pushlstring(L, getstr(key), tsslen(key));
  lua_rawget(L, obj_idx);
  if (!lua_isnil(L, -1)) {
    return;
  }
  lua_pop(L, 1);
  
  /* 在类方法中查找（使用rawget从对象获取类引用） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 使用rawget访问类表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, getstr(key), tsslen(key));
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);  /* 移除methods表 */
        lua_remove(L, -2);  /* 移除class */
        return;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 继续查找父类（使用rawget） */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  
  lua_pop(L, 1);  /* 移除非表值 */
  lua_pushnil(L);
}


/*
** 设置属性
*/
void luaC_setprop(lua_State *L, int obj_idx, TString *key, int value_idx) {
  obj_idx = absindex(L, obj_idx);
  value_idx = absindex(L, value_idx);
  
  lua_pushlstring(L, getstr(key), tsslen(key));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, obj_idx);
}


/*
** 检查对象是否是指定类的实例
*/
int luaC_instanceof(lua_State *L, int obj_idx, int class_idx) {
  obj_idx = absindex(L, obj_idx);
  class_idx = absindex(L, class_idx);
  
  if (lua_type(L, obj_idx) == LUA_TSTRUCT) {
      const TValue *o = index2value_helper(L, obj_idx);
      const TValue *c = index2value_helper(L, class_idx);
      if (structvalue(o)->def == hvalue(c)) return 1;
      return 0;
  }

  /* 检查是否是对象 */
  if (!luaC_isobject(L, obj_idx)) {
    return 0;
  }
  
  /* 检查class_idx是否是类 */
  if (!luaC_isclass(L, class_idx)) {
    return 0;
  }
  
  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  
  /* 沿继承链检查（使用rawget访问类表） */
  int loop_limit = 1000;
  while (lua_istable(L, -1)) {
    if (lua_rawequal(L, -1, class_idx)) {
      lua_pop(L, 1);
      return 1;
    }
    
    if (--loop_limit == 0) {
      /* 防止无限循环 */
      lua_pop(L, 1);
      return 0;
    }

    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, -2);
    lua_remove(L, -2);  /* 移除旧的类引用 */
  }
  
  lua_pop(L, 1);
  return 0;
}


/*
** 检查值是否是一个类
** 使用rawget避免触发__index元方法
*/
int luaC_isclass(lua_State *L, int idx) {
  if (!lua_istable(L, idx)) {
    return 0;
  }
  return checkflag_raw(L, idx, CLASS_KEY_ISCLASS);
}


/*
** 检查值是否是一个对象实例
** 使用rawget避免触发__index元方法
*/
int luaC_isobject(lua_State *L, int idx) {
  if (!lua_istable(L, idx)) {
    return 0;
  }
  return checkflag_raw(L, idx, OBJ_KEY_ISOBJ);
}


/*
** 获取对象所属的类
*/
void luaC_getclass(lua_State *L, int obj_idx) {
  obj_idx = absindex(L, obj_idx);
  
  if (!luaC_isobject(L, obj_idx)) {
    lua_pushnil(L);
    return;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
}


/*
** 获取类的父类
*/
void luaC_getparent(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  if (!luaC_isclass(L, class_idx)) {
    lua_pushnil(L);
    return;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
}


/*
** 获取类名
*/
const char *luaC_classname(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  if (!luaC_isclass(L, class_idx)) {
    return NULL;
  }
  
  /* 使用rawget避免触发__index递归 */
  lua_pushstring(L, CLASS_KEY_NAME);
  lua_rawget(L, class_idx);
  const char *name = lua_tostring(L, -1);
  lua_pop(L, 1);
  return name;
}


/*
** =====================================================================
** 接口相关函数实现
** =====================================================================
*/

/*
** 创建接口
*/
void luaC_newinterface(lua_State *L, TString *name) {
  /* 创建接口表 */
  lua_newtable(L);
  int iface_idx = lua_gettop(L);
  
  /* 设置接口名 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_setfield(L, iface_idx, CLASS_KEY_NAME);
  
  /* 标记为接口 */
  lua_pushinteger(L, CLASS_FLAG_INTERFACE);
  lua_setfield(L, iface_idx, CLASS_KEY_FLAGS);
  
  setboolfield(L, iface_idx, CLASS_KEY_ISCLASS, 1);
  
  /* 创建方法表（用于声明接口方法签名） */
  lua_newtable(L);
  lua_setfield(L, iface_idx, CLASS_KEY_METHODS);
}


/*
** 实现接口
*/
void luaC_implement(lua_State *L, int class_idx, int interface_idx) {
  class_idx = absindex(L, class_idx);
  interface_idx = absindex(L, interface_idx);
  
  /* 获取或创建接口列表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_INTERFACES);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 添加接口到列表 */
  int n = (int)lua_rawlen(L, -1);
  lua_pushvalue(L, interface_idx);
  lua_rawseti(L, -2, n + 1);
  lua_pop(L, 1);
}


/*
** 检查类是否实现了接口
*/
int luaC_implements(lua_State *L, int class_idx, int interface_idx) {
  class_idx = absindex(L, class_idx);
  interface_idx = absindex(L, interface_idx);
  
  /* 使用rawget访问类表 */
  lua_pushstring(L, CLASS_KEY_INTERFACES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  
  int n = (int)lua_rawlen(L, -1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, -1, i);
    if (lua_rawequal(L, -1, interface_idx)) {
      lua_pop(L, 2);
      return 1;
    }
    lua_pop(L, 1);
  }
  
  lua_pop(L, 1);
  
  /* 检查父类（使用rawget） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int result = luaC_implements(L, -1, interface_idx);
    lua_pop(L, 1);
    return result;
  }
  
  lua_pop(L, 1);
  return 0;
}


/*
** 初始化类系统
*/
void luaC_initclass(lua_State *L) {
  /* 类系统初始化 - 可以在这里注册全局函数等 */
  /* 目前不需要特殊初始化 */
}


/*
** =====================================================================
** 访问控制相关函数实现
** =====================================================================
*/

/*
** 设置私有成员
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   value_idx - 值在栈中的索引
** 说明：
**   将成员设置为私有，只有本类内部可以访问
*/
void luaC_setprivate(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 获取或创建私有成员表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_PRIVATES);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置私有成员 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置受保护成员
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   value_idx - 值在栈中的索引
** 说明：
**   将成员设置为受保护，本类和子类可以访问
*/
void luaC_setprotected(lua_State *L, int class_idx, TString *name, int value_idx) {
  class_idx = absindex(L, class_idx);
  value_idx = absindex(L, value_idx);
  
  /* 获取或创建受保护成员表（使用rawget/rawset） */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置受保护成员 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushvalue(L, value_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 检查类是否是另一个类的子类
** 参数：
**   L - Lua状态机
**   child_idx - 可能的子类在栈中的索引
**   parent_idx - 可能的父类在栈中的索引
** 返回值：
**   1 - 是子类（或同一个类）
**   0 - 不是子类
*/
int luaC_issubclass(lua_State *L, int child_idx, int parent_idx) {
  child_idx = absindex(L, child_idx);
  parent_idx = absindex(L, parent_idx);
  
  /* 检查是否是同一个类 */
  if (lua_rawequal(L, child_idx, parent_idx)) {
    return 1;
  }
  
  /* 检查是否是类 */
  if (!luaC_isclass(L, child_idx) || !luaC_isclass(L, parent_idx)) {
    return 0;
  }
  
  /* 沿继承链查找（使用rawget） */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, child_idx);
  while (lua_istable(L, -1)) {
    if (lua_rawequal(L, -1, parent_idx)) {
      lua_pop(L, 1);
      return 1;
    }
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, -2);
    lua_remove(L, -2);  /* 移除旧的父类引用 */
  }
  
  lua_pop(L, 1);
  return 0;
}


/*
** 检查访问权限
** 参数：
**   L - Lua状态机
**   obj_idx - 对象在栈中的索引
**   key - 要访问的成员名
**   caller_class_idx - 调用者所属类的索引（0表示外部调用）
** 返回值：
**   ACCESS_PUBLIC - 可以公开访问
**   ACCESS_PROTECTED - 需要子类关系才能访问
**   ACCESS_PRIVATE - 需要同类才能访问
**   -1 - 成员不存在
*/
int luaC_checkaccess(lua_State *L, int obj_idx, TString *key, int caller_class_idx) {
  obj_idx = absindex(L, obj_idx);
  if (caller_class_idx != 0) {
    caller_class_idx = absindex(L, caller_class_idx);
  }
  
  const char *keystr = getstr(key);
  size_t keylen = tsslen(key);
  
  /* 获取对象的类（使用rawget避免触发__index递归） */
  lua_pushstring(L, OBJ_KEY_CLASS);
  lua_rawget(L, obj_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  int obj_class_idx = lua_gettop(L);
  
  /* 检查公开成员（使用rawget访问类表） */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、方法表、类 */
      return ACCESS_PUBLIC;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护成员 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、受保护表、类 */
      return ACCESS_PROTECTED;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有成员 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, obj_class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, keystr, keylen);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
      lua_pop(L, 3);  /* 移除值、私有表、类 */
      return ACCESS_PRIVATE;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 沿继承链查找 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, obj_class_idx);
  lua_remove(L, obj_class_idx);  /* 移除对象类 */
  
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 检查公开成员 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, keystr, keylen);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_pop(L, 3);  /* 移除值、方法表、类 */
        return ACCESS_PUBLIC;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 检查受保护成员 */
    lua_pushstring(L, CLASS_KEY_PROTECTED);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      lua_pushlstring(L, keystr, keylen);
      lua_rawget(L, -2);
      if (!lua_isnil(L, -1)) {
        lua_pop(L, 3);
        return ACCESS_PROTECTED;
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    
    /* 继续查找父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  
  lua_pop(L, 1);
  return -1;  /* 成员不存在 */
}


/*
** =====================================================================
** 抽象方法和final方法相关函数实现
** =====================================================================
*/

/*
** 声明抽象方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 方法名
**   nparams - 方法期望的参数个数（用于验证实现类的方法签名，-1表示不验证）
** 说明：
**   声明一个抽象方法，子类必须实现该方法
**   同时标记类为抽象类
*/
void luaC_setabstract(lua_State *L, int class_idx, TString *name, int nparams) {
  class_idx = absindex(L, class_idx);
  
  /* 获取或创建抽象方法表 */
  lua_pushstring(L, CLASS_KEY_ABSTRACTS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_ABSTRACTS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 添加抽象方法名到表中（值为期望的参数个数，用于验证） */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushinteger(L, nparams);  /* 存储参数个数，而不是布尔值 */
  lua_rawset(L, -3);
  lua_pop(L, 1);
  
  /* 标记类为抽象类 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);
  
  flags |= CLASS_FLAG_ABSTRACT;
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_pushinteger(L, flags);
  lua_rawset(L, class_idx);
}


/*
** 设置final方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 方法名
**   func_idx - 函数在栈中的索引
** 说明：
**   设置一个final方法，子类不能重写该方法
*/
void luaC_setfinal(lua_State *L, int class_idx, TString *name, int func_idx) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 先设置方法 */
  luaC_setmethod(L, class_idx, name, func_idx);
  
  /* 获取或创建final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_FINALS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 添加final方法名到表中（值为true） */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushboolean(L, 1);
  lua_rawset(L, -3);
  lua_pop(L, 1);
  
  /* 设置成员标志 */
  int flags = luaC_getmemberflags(L, class_idx, name);
  flags |= MEMBER_FINAL;
  luaC_setmemberflags(L, class_idx, name, flags);
}


/*
** 获取函数的参数个数
** 参数：
**   L - Lua状态机
**   func_idx - 函数在栈中的索引
** 返回值：
**   参数个数，如果不是函数则返回-1
*/
static int get_func_numparams(lua_State *L, int func_idx) {
  func_idx = absindex(L, func_idx);
  
  if (!lua_isfunction(L, func_idx)) {
    return -1;
  }
  
  /* 获取函数信息 */
  lua_Debug ar;
  lua_pushvalue(L, func_idx);
  if (lua_getinfo(L, ">u", &ar) == 0) {
    return -1;
  }
  
  return ar.nparams;
}


/*
** 在类的方法表中查找函数并获取其参数个数
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   method_name_idx - 方法名在栈中的索引
** 返回值：
**   参数个数，如果未找到返回-1
** 说明：
**   依次在 __methods, __protected, __privates 中查找
*/
static int get_method_numparams(lua_State *L, int class_idx, int method_name_idx) {
  class_idx = absindex(L, class_idx);
  method_name_idx = absindex(L, method_name_idx);
  
  /* 检查公开方法 */
  lua_pushstring(L, CLASS_KEY_METHODS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查受保护方法 */
  lua_pushstring(L, CLASS_KEY_PROTECTED);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 检查私有方法 */
  lua_pushstring(L, CLASS_KEY_PRIVATES);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, method_name_idx);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1)) {
      int nparams = get_func_numparams(L, -1);
      lua_pop(L, 2);
      return nparams;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  return -1;
}


/*
** 验证抽象方法是否都被实现（包括参数数量验证）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   1 - 所有抽象方法都已正确实现
**   0 - 存在未实现或参数不匹配的抽象方法（会产生错误）
** 说明：
**   检查类是否实现了所有继承的抽象方法，并验证参数数量是否匹配
*/
int luaC_verify_abstracts(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  /* 如果类本身是抽象类，不需要验证 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      lua_pop(L, 1);
      return 1;  /* 抽象类不需要实现所有抽象方法 */
    }
  }
  lua_pop(L, 1);
  
  /* 收集所有需要实现的抽象方法（从继承链），value为参数个数 */
  lua_newtable(L);  /* 存储所有抽象方法 */
  int abstracts_idx = lua_gettop(L);
  
  /* 遍历继承链收集抽象方法 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  while (lua_istable(L, -1)) {
    int parent_idx = lua_gettop(L);
    
    /* 获取父类的抽象方法表 */
    lua_pushstring(L, CLASS_KEY_ABSTRACTS);
    lua_rawget(L, parent_idx);
    if (lua_istable(L, -1)) {
      /* 复制所有抽象方法到收集表（key=方法名，value=参数个数） */
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
        lua_pushvalue(L, -2);  /* 复制key */
        lua_pushvalue(L, -2);  /* 复制value（参数个数） */
        lua_rawset(L, abstracts_idx);
        lua_pop(L, 1);  /* 移除value */
      }
    }
    lua_pop(L, 1);  /* 移除抽象方法表 */
    
    /* 继续查找父类的父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, parent_idx);
    lua_remove(L, parent_idx);
  }
  lua_pop(L, 1);  /* 移除非表值 */
  
  /* 遍历所有需要实现的抽象方法，验证实现和参数数量 */
  lua_pushnil(L);
  while (lua_next(L, abstracts_idx) != 0) {
    /* 栈顶: value(期望参数个数), key(方法名) */
    int expected_params = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
    lua_pop(L, 1);  /* 移除value，保留key用于查找和迭代 */
    
    /* 获取实现方法的参数个数 */
    int actual_params = get_method_numparams(L, class_idx, lua_gettop(L));
    
    if (actual_params < 0) {
      /* 方法未实现 */
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "类 '%s' 必须实现抽象方法 '%s'", 
                 classname, methodname ? methodname : "?");
      return 0;
    }
    
    /* 验证参数数量是否匹配 */
    if (expected_params >= 0 && actual_params != expected_params) {
      const char *classname = get_class_name_str(L, class_idx);
      const char *methodname = lua_tostring(L, -1);
      luaL_error(L, "类 '%s' 的方法 '%s' 参数数量不匹配: 期望 %d 个参数，实际 %d 个参数", 
                 classname, methodname ? methodname : "?", 
                 expected_params, actual_params);
      return 0;
    }
  }
  
  lua_pop(L, 1);  /* 移除abstracts表 */
  return 1;
}


/*
** 验证接口方法是否都被正确实现（包括参数数量验证）
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
** 返回值：
**   1 - 所有接口方法都已正确实现
**   0 - 存在未实现或参数不匹配的接口方法（会产生错误）
** 说明：
**   检查类是否实现了所有接口声明的方法，并验证参数数量
*/
int luaC_verify_interfaces(lua_State *L, int class_idx) {
  class_idx = absindex(L, class_idx);
  
  /* 如果类本身是抽象类，不需要验证接口实现 */
  lua_pushstring(L, CLASS_KEY_FLAGS);
  lua_rawget(L, class_idx);
  if (lua_isinteger(L, -1)) {
    int flags = (int)lua_tointeger(L, -1);
    if (flags & CLASS_FLAG_ABSTRACT) {
      lua_pop(L, 1);
      return 1;  /* 抽象类不需要实现所有接口方法 */
    }
  }
  lua_pop(L, 1);
  
  /* 收集所有实现的接口（包括继承链中的） */
  lua_newtable(L);  /* 存储所有接口 */
  int interfaces_collect_idx = lua_gettop(L);
  int iface_count = 0;
  
  /* 从当前类开始遍历继承链收集接口 */
  lua_pushvalue(L, class_idx);
  while (lua_istable(L, -1)) {
    int current_class = lua_gettop(L);
    
    /* 获取当前类的接口列表 */
    lua_pushstring(L, CLASS_KEY_INTERFACES);
    lua_rawget(L, current_class);
    if (lua_istable(L, -1)) {
      int ifaces_idx = lua_gettop(L);
      int n = (int)lua_rawlen(L, ifaces_idx);
      for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, ifaces_idx, i);
        /* 添加到收集表（避免重复） */
        int is_dup = 0;
        for (int j = 1; j <= iface_count; j++) {
          lua_rawgeti(L, interfaces_collect_idx, j);
          if (lua_rawequal(L, -1, -2)) {
            is_dup = 1;
            lua_pop(L, 1);
            break;
          }
          lua_pop(L, 1);
        }
        if (!is_dup) {
          iface_count++;
          lua_pushvalue(L, -1);
          lua_rawseti(L, interfaces_collect_idx, iface_count);
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);  /* 移除接口列表 */
    
    /* 继续查找父类 */
    lua_pushstring(L, CLASS_KEY_PARENT);
    lua_rawget(L, current_class);
    lua_remove(L, current_class);
  }
  lua_pop(L, 1);  /* 移除非表值 */
  
  /* 遍历所有接口，验证方法实现 */
  for (int i = 1; i <= iface_count; i++) {
    lua_rawgeti(L, interfaces_collect_idx, i);
    int iface_idx = lua_gettop(L);
    
    /* 获取接口名（用于错误消息） */
    const char *tmp_name = get_class_name_str(L, iface_idx);
    char iface_name[256];
    if (tmp_name) {
      strncpy(iface_name, tmp_name, sizeof(iface_name));
      iface_name[sizeof(iface_name) - 1] = '\0';
    } else {
      strcpy(iface_name, "?");
    }
    
    /* 获取接口的方法表 */
    lua_pushstring(L, CLASS_KEY_METHODS);
    lua_rawget(L, iface_idx);
    if (lua_istable(L, -1)) {
      int iface_methods_idx = lua_gettop(L);
      
      /* 遍历接口的所有方法 */
      lua_pushnil(L);
      while (lua_next(L, iface_methods_idx) != 0) {
        /* 栈顶: value(期望参数个数), key(方法名) */
        int expected_params = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
        lua_pop(L, 1);  /* 移除value，保留key用于查找 */
        
        /* 获取实现方法的参数个数 */
        int actual_params = get_method_numparams(L, class_idx, lua_gettop(L));
        
        if (actual_params < 0) {
          /* 方法未实现 */
          const char *classname = get_class_name_str(L, class_idx);
          const char *methodname = lua_tostring(L, -1);
          luaL_error(L, "类 '%s' 必须实现接口 '%s' 的方法 '%s'", 
                     classname, iface_name,
                     methodname ? methodname : "?");
          return 0;
        }
        
        /* 验证参数数量是否匹配 */
        if (expected_params >= 0 && actual_params != expected_params) {
          const char *classname = get_class_name_str(L, class_idx);
          const char *methodname = lua_tostring(L, -1);
          luaL_error(L, "类 '%s' 实现接口 '%s' 的方法 '%s' 参数数量不匹配: 期望 %d 个参数，实际 %d 个参数", 
                     classname, iface_name,
                     methodname ? methodname : "?", 
                     expected_params, actual_params);
          return 0;
        }
      }
    }
    lua_pop(L, 1);  /* 移除接口方法表 */
    lua_pop(L, 1);  /* 移除接口 */
  }
  
  lua_pop(L, 1);  /* 移除interfaces_collect表 */
  return 1;
}


/*
** 检查方法是否可以被重写
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引（父类）
**   name - 方法名
** 返回值：
**   1 - 可以重写
**   0 - 不能重写（是final方法）
*/
int luaC_can_override(lua_State *L, int class_idx, TString *name) {
  class_idx = absindex(L, class_idx);
  
  /* 检查final方法表 */
  lua_pushstring(L, CLASS_KEY_FINALS);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    lua_pushlstring(L, getstr(name), tsslen(name));
    lua_rawget(L, -2);
    if (lua_toboolean(L, -1)) {
      lua_pop(L, 2);
      return 0;  /* 是final方法，不能重写 */
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  /* 递归检查父类 */
  lua_pushstring(L, CLASS_KEY_PARENT);
  lua_rawget(L, class_idx);
  if (lua_istable(L, -1)) {
    int result = luaC_can_override(L, -1, name);
    lua_pop(L, 1);
    return result;
  }
  lua_pop(L, 1);
  
  return 1;  /* 可以重写 */
}


/*
** =====================================================================
** getter/setter属性访问器相关函数实现
** =====================================================================
*/

/*
** 设置getter方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   prop_name - 属性名
**   func_idx - getter函数在栈中的索引
** 说明：
**   当访问指定属性时，会调用getter函数
**   根据访问级别存储到不同的getter表中
*/
void luaC_setgetter(lua_State *L, int class_idx, TString *prop_name, int func_idx, int access_level) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 根据访问级别选择getter表 */
  const char *table_key;
  if (access_level == ACCESS_PRIVATE) {
    table_key = CLASS_KEY_PRIVATE_GETTERS;
  } else if (access_level == ACCESS_PROTECTED) {
    table_key = CLASS_KEY_PROTECTED_GETTERS;
  } else {
    table_key = CLASS_KEY_GETTERS;  /* 公开 */
  }
  
  /* 获取或创建getter表 */
  lua_pushstring(L, table_key);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, table_key);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置getter函数 */
  lua_pushlstring(L, getstr(prop_name), tsslen(prop_name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置setter方法
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   prop_name - 属性名
**   func_idx - setter函数在栈中的索引
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   当设置指定属性时，会调用setter函数
**   根据访问级别存储到不同的setter表中
*/
void luaC_setsetter(lua_State *L, int class_idx, TString *prop_name, int func_idx, int access_level) {
  class_idx = absindex(L, class_idx);
  func_idx = absindex(L, func_idx);
  
  /* 根据访问级别选择setter表 */
  const char *table_key;
  if (access_level == ACCESS_PRIVATE) {
    table_key = CLASS_KEY_PRIVATE_SETTERS;
  } else if (access_level == ACCESS_PROTECTED) {
    table_key = CLASS_KEY_PROTECTED_SETTERS;
  } else {
    table_key = CLASS_KEY_SETTERS;  /* 公开 */
  }
  
  /* 获取或创建setter表 */
  lua_pushstring(L, table_key);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, table_key);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置setter函数 */
  lua_pushlstring(L, getstr(prop_name), tsslen(prop_name));
  lua_pushvalue(L, func_idx);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 设置成员标志
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
**   flags - 标志位（MEMBER_*）
*/
void luaC_setmemberflags(lua_State *L, int class_idx, TString *name, int flags) {
  class_idx = absindex(L, class_idx);
  
  /* 获取或创建成员标志表 */
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
    lua_insert(L, -2);
    lua_rawset(L, class_idx);
  }
  
  /* 设置标志 */
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_pushinteger(L, flags);
  lua_rawset(L, -3);
  lua_pop(L, 1);
}


/*
** 获取成员标志
** 参数：
**   L - Lua状态机
**   class_idx - 类在栈中的索引
**   name - 成员名
** 返回值：
**   成员标志位，不存在返回0
*/
int luaC_getmemberflags(lua_State *L, int class_idx, TString *name) {
  class_idx = absindex(L, class_idx);
  
  lua_pushstring(L, CLASS_KEY_MEMBER_FLAGS);
  lua_rawget(L, class_idx);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  
  lua_pushlstring(L, getstr(name), tsslen(name));
  lua_rawget(L, -2);
  int flags = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : 0;
  lua_pop(L, 2);
  return flags;
}
