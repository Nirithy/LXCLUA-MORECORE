local fs = require "fs"

print("Testing fs library...")

-- 1. Check fs table
assert(type(fs) == "table", "fs library not found")
assert(type(fs.ls) == "function", "fs.ls missing")

-- 2. Current Directory
local cwd = fs.currentdir()
print("Current Dir: " .. tostring(cwd))
assert(type(cwd) == "string", "fs.currentdir failed")

-- 3. Mkdir
local test_dir = "test_dir_" .. tostring(os.time())
print("Creating directory: " .. test_dir)
local ok, err = fs.mkdir(test_dir)
assert(ok, "fs.mkdir failed: " .. tostring(err))

-- 4. Exists (Dir)
assert(fs.exists(test_dir), "fs.exists failed for directory")
assert(fs.isdir(test_dir), "fs.isdir failed")

-- 5. Create a file inside
local test_file = test_dir .. "/test.txt"
print("Creating file: " .. test_file)
local f = io.open(test_file, "w")
f:write("Hello FS")
f:close()

-- 6. Exists (File)
assert(fs.exists(test_file), "fs.exists failed for file")
assert(fs.isfile(test_file), "fs.isfile failed")
assert(not fs.isdir(test_file), "fs.isdir true for file")

-- 7. Stat
local info = fs.stat(test_file)
assert(type(info) == "table", "fs.stat failed")
assert(info.size == 8, "fs.stat size incorrect")
assert(info.isfile, "fs.stat isfile incorrect")
print("File size: " .. info.size)

-- 8. ls
print("Listing directory...")
local files = fs.ls(test_dir)
assert(type(files) == "table", "fs.ls failed")
local found = false
for k, v in pairs(files) do
    print("Found: " .. v)
    if v == "test.txt" then found = true end
end
assert(found, "fs.ls did not find test.txt")

-- 9. Abs, Basename, Dirname
local abs = fs.abs(test_file)
print("Abs path: " .. tostring(abs))
assert(string.find(abs, test_file), "fs.abs failed")

assert(fs.basename(test_file) == "test.txt", "fs.basename failed")
-- fs.dirname might return relative path or absolute depending on implementation context,
-- but here we passed relative so it should probably preserve it or be consistent.
-- My implementation uses strrchr so:
assert(fs.dirname(test_file) == test_dir, "fs.dirname failed: " .. tostring(fs.dirname(test_file)))

-- 10. Rm
print("Removing file...")
assert(fs.rm(test_file), "fs.rm file failed")
assert(not fs.exists(test_file), "File still exists after rm")

print("Removing directory...")
assert(fs.rm(test_dir), "fs.rm dir failed")
assert(not fs.exists(test_dir), "Directory still exists after rm")

print("fs library tests passed!")
