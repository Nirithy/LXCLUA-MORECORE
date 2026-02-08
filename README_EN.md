# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

English | [中文](README.md)

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

## Tested Platforms

| Platform | Status | Bytecode Interop |
|----------|--------|------------------|
| Windows (MinGW) | ✅ Passed | ✅ |
| Arch Linux | ✅ Passed | ✅ |
| Ubuntu | ✅ Passed | ✅ |
| Android (Termux) | ✅ Passed | ✅ |
| Android (LXCLUA JNI) | ✅ Passed | ✅ |

## Features

### Core Enhancements

- **Secure Compilation** - Bytecode uses dynamic OPcode mapping, timestamp encryption, and SHA-256 integrity verification
- **PNG Encapsulation** - Compiled bytecode embedded in PNG image format for enhanced obfuscation
- **Anti-Reverse Protection** - Multi-layer encryption mechanisms effectively prevent decompilation and tampering

### Extension Modules

| Module | Description |
|--------|-------------|
| `json` | Built-in JSON parsing and serialization |
| `sha256` | SHA-256 hash computation |
| `lclass` | Object-oriented programming support (classes, inheritance, polymorphism) |
| `lbitlib` | Bitwise operations library |
| `lboolib` | Boolean type enhancements |
| `ludatalib` | Binary data serialization |
| `lsmgrlib` | Memory management utilities |
| `ltranslator` | Code translator |
| `logtable` | Log table support |
| `lptrlib` | Pointer operations library |
| `lvmlib` | Virtual machine extension interface |

### Compilation Optimizations

- Compiled with C23 standard
- LTO (Link-Time Optimization)
- Loop unrolling and strict aliasing analysis
- Debug symbols stripped for minimal binary size

## System Requirements

- **Compiler**: GCC (with C11/C23 standard support)
- **Platform**: Windows / Linux / Android (Termux)

## Quick Start

### Build

```bash
# Windows (MinGW)
make mingw

# Linux
make linux

# Android (Termux)
make termux
```

### Verify Installation

```bash
make test
```

### Clean Build

```bash
make clean
```

## Build Outputs

| File | Description |
|------|-------------|
| `liblua.a` / `lua55.dll` | Lua static library / dynamic library |
| `lua` / `lua.exe` | Lua interpreter |
| `luac` / `luac.exe` | Lua bytecode compiler |

## Usage Examples

### Run Lua Script

```bash
./lua script.lua
```

### Compile to Bytecode

```bash
./luac -o output.luac script.lua
```

### Embed in C/C++ Project

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

## Project Structure

```
.
├── l*.c / l*.h      # Lua core source files
├── json_parser.*    # JSON parsing module
├── sha256.*         # SHA-256 hash module
├── lclass.*         # Object-oriented class system
├── stb_image*.h     # Image processing (stb library)
├── Makefile         # Build script
└── lua/             # Sub-version (secondary logic)
```

## Security Notes

The bytecode compilation in this project employs multiple security mechanisms:

1. **Dynamic OPcode Mapping** - Generates unique instruction mapping table for each compilation
2. **Timestamp Encryption** - Uses compilation time as encryption key
3. **SHA-256 Verification** - Ensures bytecode integrity
4. **PNG Image Encapsulation** - Embeds encrypted data in image format

> Note: These protective measures are designed to increase reverse engineering difficulty but cannot guarantee absolute security.

## License

This project is open-sourced under the [MIT License](LICENSE).

Original Lua code is copyrighted by PUC-Rio. See [Lua License](https://www.lua.org/license.html).

## Contributing

Issues and Pull Requests are welcome. Please refer to the [Contributing Guidelines](CONTRIBUTING.md).

## Contact

- **Email**: difierline@yeah.net
- **GitHub**: https://github.com/OxenFxc/LXCLUA-NCORE

## Acknowledgments

- [Lua](https://www.lua.org/) - Original Lua language
- [stb](https://github.com/nothings/stb) - Single-header image processing library
