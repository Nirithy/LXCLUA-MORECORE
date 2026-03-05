# LXCLUA-NCore Project Development Plan (计划书)

This document outlines the current state, strengths, weaknesses, and future development roadmaps for the LXCLUA-NCore project. It is intended to guide contributors and maintainers on where to focus efforts.

## 1. Project Overview & Strengths (项目优势与现状)

LXCLUA-NCore is a highly customized, high-performance embedded scripting engine based on Lua 5.5. It brings modern programming paradigms, deep C-level integration, and advanced security features to the Lua ecosystem.

*   **Custom 64-bit VM Instruction Set:** Optimized instruction mapping (`XCLUA`) allowing larger operand sizes (e.g., `sC` offsets up to 32767) and more efficient dispatching.
*   **Rich Syntax Extensions:** Native support for OOP (classes, interfaces, visibility modifiers), Switch statements, Try-Catch, Arrow Functions, Pipeline Operators (`|>`), Optional Chaining (`?.`), Nullish Coalescing (`??`), and Shell-style conditional testing (`[ -f "file" ]`).
*   **Security & Obfuscation:** A multi-layered obfuscation engine supporting control flow flattening (CFF), bogus blocks, string encryption via rolling XOR, integer arithmetic obfuscation (LCG-based), and binary dispatcher injection.
*   **JIT / Runtime C Compilation (TCC):** Integration with Tiny C Compiler enables seamless runtime compilation of C code into native execution via `require("tcc")`.
*   **WebAssembly (WASM) Integration:** Built-in `wasm3` runtime execution directly from Lua, supporting high-performance sandbox execution with environment manipulation.
*   **Exposed Lexer / AST (`lexer`):** Exposes internal C parsing primitives (like `build_tree`, `find_label`, `get_block_bounds`) to Lua, allowing powerful AST manipulation, refactoring, and customized CFF logic written entirely in pure Lua.
*   **Bare-Metal Foundation:** An included `os/` directory providing Multiboot support, VGA, Serial I/O, and basic interrupts for running the engine directly on bare metal (x86).

## 2. Weaknesses & Challenges (现存问题与挑战)

While feature-rich, the extensive modifications and integrations introduce specific challenges that need to be addressed:

*   **Complexity of Integration:** Maintaining synchronization between the custom 64-bit `lparser.c`/`lvm.c`, TCC inline C generation (`ltcc.c`), and WebAssembly bindings (`lwasm3.c`) requires immense care. Any structural changes to the Lua stack or bytecode format risk breaking multiple downstream modules.
*   **Memory Safety in Native Extensions:** Given the heavy use of raw C APIs in modules like `struct`, `ptr`, and the obfuscator (`lobfuscate.c`), the risk of segfaults or memory leaks (especially during AST generation or CFF restructuring) is higher than in vanilla Lua.
*   **Documentation & API Standardization:** While `README.md` covers the syntax, deeper modules (like the `ByteCode` API or `lexer` manipulation) lack comprehensive, standardized documentation detailing edge cases (e.g., handling interpolated strings `<interpstring>` vs raw strings `<rawstring>`).
*   **Test Coverage for Edge Cases:** Some modern syntax features (like async/await, or deeply nested concepts/superstructs) may lack exhaustive regression tests, especially when combined with heavy CFF obfuscation.

## 3. Short-Term Goals (近期开发目标)

Focus on stabilization, security enhancements, and expanding the Lua-side tooling for the C primitives.

1.  **Enhance Pure Lua Lexer Tooling:**
    *   Since C primitives (e.g., `lexer.get_block_bounds`) are available, implement more robust high-level control-flow flattening (CFF) and abstract syntax tree (AST) manipulation scripts in pure Lua. This avoids writing complex business logic in C (`llexerlib.c`).
    *   Create detailed examples/tutorials for users on how to use `lexer.reconstruct` and AST manipulation to refactor their own Lua code dynamically.
2.  **Strengthen `tcc` Obfuscation:**
    *   Expand `obf_int` to include more complex opaque predicates.
    *   Ensure `string_encryption` remains decoupled from the `flatten` obfuscation flag to avoid double-encryption bugs.
3.  **WASM Sandbox Hardening:**
    *   Improve error isolation in the `wasm3` module so that WASM traps do not crash the host Lua state.
    *   Expand WASI support and ensure macro guards (`d_m3HasWASI`) are robust across all compilation targets.
4.  **Testing & CI:**
    *   Add more automated tests specifically targeting the interaction between the custom 64-bit opcodes and the `debug` hooks (e.g., conditional breakpoints, `hookf`).

## 4. Long-Term Goals (远期开发目标)

Focus on architectural evolution, performance optimization, and broader platform support.

1.  **Advanced Bytecode Optimization Pass:**
    *   Implement an intermediate representation (IR) optimization pass before bytecode emission to aggressively fold constants, eliminate dead code, and inline small functions statically.
2.  **Full OS/Bare-Metal Maturation (Phase 2):**
    *   Expand the `os/` foundation from basic bootloader and VGA drivers to include a simple scheduler and file system support, allowing LXCLUA to run as a standalone embedded operating system.
3.  **JIT Compilation to WASM/Native:**
    *   Explore generating WASM bytecode directly from the Lua AST/ByteCode for execution in browsers, leveraging the existing `wasm3` knowledge.
4.  **Package Manager / Ecosystem:**
    *   Develop a lightweight package manager (akin to LuaRocks) tailored for LXCLUA-NCore, supporting pre-compiled TCC/WASM modules and pure Lua AST transformers.

---
*End of Document. Planned and maintained by the LXCLUA-NCore Development Team.*