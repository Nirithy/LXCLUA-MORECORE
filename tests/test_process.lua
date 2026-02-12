local process = require "process"
local ptr = require "ptr"

print("Process library loaded.")

-- Test getpid
local pid = process.getpid()
print("Current PID:", pid)
assert(type(pid) == "number")
assert(pid > 0)

-- Test open
local p = process.open(pid)
print("Process opened.")
assert(p)

-- Allocate memory for testing
local size = 64
local mem = ptr.malloc(size)
print("Allocated memory at:", tostring(mem))

-- Write to memory locally first
local test_str = "Hello, Process Memory!"
-- Pad with nulls
local test_data = test_str .. string.rep("\0", size - #test_str)
ptr.copy(mem, ptr.of(test_data), size)

-- Read using ptr to verify local write worked
local read_back_local = ptr.string(mem)
print("Local read back:", read_back_local)
assert(read_back_local == test_str)

-- Now use process library to read
-- We need the address as an integer or pointer. `ptr.malloc` returns a pointer (userdata/lightuserdata).
-- `process:read` accepts pointer or integer.

local remote_data = p:read(mem, size)
print("Remote read result length:", #remote_data)

-- Verify content
assert(remote_data == test_data, "Remote read data mismatch")
local remote_str = remote_data:match("^[^\0]*")
print("Remote read str:", remote_str)
assert(remote_str == test_str)

-- Now test writing via process library
local new_msg = "Modified via Process!"
-- Write with null terminator to ensure we overwrite cleanly and terminate string
local write_data = new_msg .. "\0"
local write_res = p:write(mem, write_data)
assert(write_res == #write_data, "Write return value mismatch")

local check_read = ptr.string(mem)
print("Read back after process write:", check_read)
assert(check_read == new_msg)

-- Cleanup
p:close()
ptr.free(mem)

print("Process library test passed!")
