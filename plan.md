1. **Add `inline_opt` variable to `tcc_compile`**: Parse `inline` field from the configuration table passed to `tcc.compile`.
2. **Update `process_proto` signature**: Add `inline_opt` as an argument to `process_proto`.
3. **Generate `static inline int`**:
   - For forward declarations, generate `static inline int %s(lua_State *L);` if `inline_opt` is true, otherwise `static int %s(lua_State *L);`.
   - For implementations, generate `static inline int %s(lua_State *%s) {` if `inline_opt` is true, otherwise `static int %s(lua_State *%s) {`.
4. **Test the changes**: Create a test script using `tcc.compile(code, {inline=true})` and ensure the generated C code contains the `static inline int` keyword.
5. **Pre-commit checks**: Run standard testing and validation checks.
