# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

[English](README_EN.md) | 中文

基于 **Lua 5.5 (Custom)** 深度定制的高性能嵌入式脚本引擎，提供增强的安全特性、扩展库支持和优化的字节码编译。

## 测试平台

| 平台 | 状态 | 字节码互通 |
|------|------|-----------|
| Windows (MinGW) | ✅ 通过 | ✅ |
| Arch Linux | ✅ 通过 | ✅ |
| Ubuntu | ✅ 通过 | ✅ |
| Android (Termux) | ✅ 通过 | ✅ |
| Android (LXCLUA JNI) | ✅ 通过 | ✅ |

## 特性

### 核心增强

- **安全编译** - 字节码采用动态 OPcode 映射、时间戳加密、SHA-256 完整性验证
- **PNG 封装** - 编译后的字节码嵌入 PNG 图像格式，增强混淆效果
- **防逆向保护** - 多层加密机制有效防止字节码被反编译或篡改

### 扩展模块

| 模块 | 功能描述 |
|------|----------|
| `json` | 内置 JSON 解析与序列化支持 |
| `sha256` | SHA-256 哈希计算 |
| `lclass` | 面向对象编程支持（类、继承、多态） |
| `lbitlib` | 位运算库 |
| `lboolib` | 布尔类型增强 |
| `ludatalib` | 二进制数据序列化 |
| `lsmgrlib` | 内存管理工具 |
| `ltranslator` | 代码翻译器 |
| `logtable` | 日志表支持 |
| `lptrlib` | 指针操作库 |
| `lvmlib` | 虚拟机扩展接口 |

### 编译优化

- 使用 C23 标准编译
- LTO（链接时优化）
- 循环展开与严格别名分析
- 去除调试符号，最小化二进制体积

## 系统要求

- **编译器**: GCC（支持 C11/C23 标准）
- **平台**: Windows (MinGW) / Linux / Android (Termux)

## 快速开始

### 编译

```bash
# Windows (MinGW)
make mingw

# Linux
make linux

# Android (Termux)
make termux
```

### 验证安装

```bash
make test
```

### 清理构建

```bash
make clean
```

## 构建产物

| 文件 | 描述 |
|------|------|
| `liblua.a` / `lua55.dll` | Lua 静态库 / 动态库 |
| `lua` / `lua.exe` | Lua 解释器 |
| `luac` / `luac.exe` | Lua 字节码编译器 |

## 使用示例

### 运行 Lua 脚本

```bash
./lua script.lua
```

### 编译为字节码

```bash
./luac -o output.luac script.lua
```

### 嵌入到 C/C++ 项目

```c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    
    luaL_dofile(L, "script.lua");
    
    lua_close(L);
    return 0;
}
```

## 项目结构

```
.
├── l*.c / l*.h      # Lua 核心源码
├── json_parser.*    # JSON 解析模块
├── sha256.*         # SHA-256 哈希模块
├── lclass.*         # 面向对象类系统
├── stb_image*.h     # 图像处理（stb 库）
├── Makefile         # 构建脚本
└── lua/             # 子版本（次逻辑）
```

## 安全说明

本项目的字节码编译采用多重安全机制：

1. **动态 OPcode 映射** - 每次编译生成唯一的指令映射表
2. **时间戳加密** - 使用编译时间作为加密密钥
3. **SHA-256 验证** - 确保字节码完整性
4. **PNG 图像封装** - 将加密数据嵌入图像格式

> 注意：这些保护措施旨在增加逆向工程难度，但不能保证绝对安全。

## 许可证

本项目基于 [MIT 许可证](LICENSE) 开源。

Lua 原始代码版权归 PUC-Rio 所有，详见 [Lua License](https://www.lua.org/license.html)。

## 贡献

欢迎提交 Issue 和 Pull Request。请参阅 [贡献指南](CONTRIBUTING.md)。

## 联系方式

- **邮箱**: difierline@yeah.net
- **GitHub**: https://github.com/OxenFxc/LXCLUA-NCORE

## 致谢

- [Lua](https://www.lua.org/) - 原始 Lua 语言
- [stb](https://github.com/nothings/stb) - 单头文件图像处理库
