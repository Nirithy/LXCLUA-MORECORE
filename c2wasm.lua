local function c_to_wasm_lua(c_code, func_name)
    -- Generate unique temporary file names
    local tmp_base = os.tmpname()
    local c_file_path = tmp_base .. ".c"
    local wasm_file_path = tmp_base .. ".wasm"

    -- Write C code to a temporary file
    local c_file = io.open(c_file_path, "w")
    if not c_file then
        return nil, "Failed to create temporary C file: " .. c_file_path
    end
    c_file:write(c_code)
    c_file:close()

    -- Compile to WebAssembly using clang
    -- Note: This requires clang with wasm32 target support installed on the system
    local cmd = string.format("clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-all -O3 -o %s %s", wasm_file_path, c_file_path)
    local ret = os.execute(cmd)

    if ret ~= true and ret ~= 0 then
        os.remove(c_file_path)
        return nil, "Compilation failed. Ensure clang is installed and supports --target=wasm32"
    end

    -- Read the compiled WebAssembly binary
    local wasm_file = io.open(wasm_file_path, "rb")
    if not wasm_file then
        os.remove(c_file_path)
        os.remove(wasm_file_path)
        return nil, "Failed to read compiled wasm file: " .. wasm_file_path
    end
    local wasm_bytes = wasm_file:read("*a")
    wasm_file:close()

    -- Clean up temporary files
    os.remove(c_file_path)
    os.remove(wasm_file_path)

    -- Convert binary bytes to hex string format for Lua efficiently
    local hex_t = {}
    for i = 1, #wasm_bytes do
        table.insert(hex_t, string.format("\\x%02x", string.byte(wasm_bytes, i)))
    end
    local hex_str = table.concat(hex_t)

    -- Generate the target Lua code
    local lua_code = string.format([[
local wasm3 = require("wasm3")


local wasm_bytes = "%s"

local env = wasm3.newEnvironment()
local runtime = env:newRuntime()
local module = env:parseModule(wasm_bytes)
runtime:loadModule(module)

local %s_func = runtime:findFunction("%s")
local result = %s_func:call(10, 20)
print("10 + 20 = " .. result)  -- 输出: 10 + 20 = 30
]], hex_str, func_name, func_name, func_name)

    return lua_code, wasm_bytes
end

return {
    compile = c_to_wasm_lua
}
