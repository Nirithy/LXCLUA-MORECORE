
local fs = require "fs"

local args = {...}
local mode = args[1]

if mode == "readonly" then
    print("Testing Read-Only Mode")
    fs.set_permissions({ read_only = true })

    local ok, err = pcall(fs.mkdir, "test_mkdir_fail")
    if ok then error("mkdir should fail in read-only mode") end
    if not string.find(err, "read-only", 1, true) then error("Wrong error: " .. tostring(err)) end

    print("Read-Only Tests Passed")
    return
end

if mode == "root" then
    print("Testing Root Constraint")
    local cwd = fs.currentdir()
    local root = cwd .. "/tests/sandbox"

    fs.set_permissions({ root = root })

    -- Allowed
    local ok, err = pcall(fs.ls, root)
    if not ok then error("Should be allowed to ls root: " .. tostring(err)) end

    -- Allowed subfile
    local sub = root .. "/subdir"
    if fs.exists(sub) then fs.rm(sub) end
    ok, err = pcall(fs.mkdir, sub)
    if not ok then error("Should be allowed to mkdir inside root: " .. tostring(err)) end

    -- Test bare directory creation (requires chdir into root)
    -- First, enter the root (allowed)
    ok, err = pcall(fs.chdir, root)
    if not ok then error("Should be allowed to chdir to root: " .. tostring(err)) end

    -- Now create a bare directory (should work if POSIX fallback logic is correct)
    local bare = "bare_subdir"
    if fs.exists(bare) then fs.rm(bare) end
    ok, err = pcall(fs.mkdir, bare)
    if not ok then error("Should be allowed to mkdir bare_subdir inside root: " .. tostring(err)) end
    fs.rm(bare)

    -- Denied
    ok, err = pcall(fs.ls, cwd)
    if ok then error("Should be denied to ls parent") end
    if not string.find(err, "permission denied", 1, true) then error("Wrong error: " .. tostring(err)) end

    -- Denied via ..
    ok, err = pcall(fs.ls, root .. "/..")
    if ok then error("Should be denied to ls ..") end

    fs.rm(sub)

    print("Root Constraint Tests Passed")
    return
end

-- Main runner
print("Running FS Permission Tests")

local cwd = fs.currentdir()
local sandbox = cwd .. "/tests/sandbox"
if not fs.exists(sandbox) then
    local ok, err = fs.mkdir(sandbox)
    if not ok then error("Could not create sandbox: " .. tostring(err)) end
end

-- Run subprocess for readonly
local cmd = "./lxclua tests/test_fs_permissions.lua readonly"
local ret = os.execute(cmd)
if ret ~= 0 and ret ~= true then error("Read-only test failed") end

-- Run subprocess for root
cmd = "./lxclua tests/test_fs_permissions.lua root"
ret = os.execute(cmd)
if ret ~= 0 and ret ~= true then error("Root test failed") end

-- Test setting permissions twice
print("Testing set_permissions twice")
fs.set_permissions({})
local ok, err = pcall(fs.set_permissions, {})
if ok then error("Should fail setting permissions twice") end
if not string.find(err, "already set", 1, true) then error("Wrong error: " .. tostring(err)) end

print("All Tests Passed!")

fs.rm(sandbox)
