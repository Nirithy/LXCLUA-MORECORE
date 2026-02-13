-- pointer_system_demo.lua
-- A detailed demonstration of the pointer system capabilities in this Lua implementation.

local function print_header(title)
  print(string.rep("-", 40))
  print(title)
  print(string.rep("-", 40))
end

local function assert_equal(actual, expected, msg)
  if actual ~= expected then
    error(string.format("%s: Expected %s, got %s", msg or "Assertion failed", tostring(expected), tostring(actual)))
  else
    print(string.format("[PASS] %s", msg or ""))
  end
end

-- Check if ptr library is loaded
if not ptr then
  print("Error: 'ptr' library not found. Make sure you are running this with the custom Lua build.")
  return
end

print_header("1. Pointer Creation & Memory Allocation")

-- ptr.malloc(size) - Allocate raw memory
local size = 64
local p = ptr.malloc(size)
print("Allocated 64 bytes at:", p)
assert(p, "malloc failed")

-- ptr.addr(p) - Get integer address
local addr = ptr.addr(p)
print("Address (integer):", string.format("0x%X", addr))

-- ptr(addr) - Create pointer from address
local p2 = ptr.new(addr) -- or just ptr(addr) if __call is set (it is in lptrlib.c)
print("Pointer from address:", p2)
assert_equal(p, p2, "Pointer reconstruction failed")

-- ptr.null() - Null pointer
local null_ptr = ptr.null()
print("Null pointer:", null_ptr)
assert(ptr.is_null(null_ptr), "ptr.is_null failed")


print_header("2. Pointer Arithmetic")

-- ptr.inc / ptr.dec
local p_next = ptr.inc(p)
print("p + 1:", p_next)
assert_equal(ptr.addr(p_next), addr + 1, "Increment failed")

local p_prev = ptr.dec(p_next)
print("p_next - 1:", p_prev)
assert_equal(p, p_prev, "Decrement failed")

-- Operators + / -
local p_plus_10 = p + 10
print("p + 10:", p_plus_10)
assert_equal(ptr.addr(p_plus_10), addr + 10, "Addition operator failed")

local p_sub_5 = p_plus_10 - 5
print("p + 10 - 5:", p_sub_5)
assert_equal(ptr.addr(p_sub_5), addr + 5, "Subtraction operator (ptr - int) failed")

-- Pointer difference (ptr - ptr) -> ptrdiff_t (integer)
local diff = p_plus_10 - p
print("(p + 10) - p:", diff)
assert_equal(diff, 10, "Pointer difference failed")


print_header("3. Raw Byte Access (Array Indexing)")

-- Initialize memory with specific byte pattern using loop and indexing
-- p[i] accesses byte at offset i
for i = 0, 9 do
  p[i] = i * 2
end

print("Written bytes 0..9:")
local hex_dump = ptr.tohex(p, 10)
print(hex_dump)
assert_equal(hex_dump, "00 02 04 06 08 0A 0C 0E 10 12", "Raw byte write verification failed")

-- Read back
assert_equal(p[5], 10, "Raw byte read failed at index 5")


print_header("4. Typed Access (String Keys)")

-- Supported types:
-- "i8"/"char", "u8"/"byte", "i16"/"short", "u16"/"ushort"
-- "i32"/"int", "u32"/"uint", "i64"/"long", "u64"/"ulong"
-- "f32"/"float", "f64"/"double"
-- "size_t", "ptr"/"pointer", "str"/"cstr"

local p_int = p + 16 -- Move to offset 16 to avoid overwriting previous test data

-- Write Int (4 bytes)
p_int["int"] = 123456789
print("Wrote int:", 123456789)
assert_equal(p_int["int"], 123456789, "Int read/write failed")

-- Write Float (4 bytes)
local p_float = p + 20
p_float["float"] = 3.14159
print("Wrote float:", 3.14159)
-- Float precision comparison
local val = p_float["float"]
assert(math.abs(val - 3.14159) < 0.00001, "Float read/write failed")

-- Write Double (8 bytes)
local p_double = p + 24
p_double["double"] = 1.234567890123
print("Wrote double:", 1.234567890123)
assert(math.abs(p_double["double"] - 1.234567890123) < 1e-10, "Double read/write failed")


print_header("5. String Operations")

-- ptr.of(string) - Get pointer to string data
local s = "Hello World"
local p_str = ptr.of(s)
print("Pointer to string 'Hello World':", p_str)

-- Read char from string pointer
print("First char:", string.char(p_str[0]))
assert_equal(p_str[0], string.byte("H"), "String pointer read failed")

-- ptr.string(p, [len]) - Create Lua string from pointer
local s_copy = ptr.string(p_str, 5)
print("Copied 5 chars:", s_copy)
assert_equal(s_copy, "Hello", "ptr.string failed")

-- Writing string via pointer (Careful! Target must have space)
-- We use our allocated memory 'p' for this
local p_str_write = p + 32
p_str_write["str"] = "LuaPtr" -- Writes the string content? OR writes the pointer?
-- Checking lvm.c:
-- case 'c': if (strcmp(key_str, "cstr") == 0) ... writes *(const char**)p = getstr(...)
-- So p["str"] writes the ADDRESS of the string to the memory at p.
-- It does NOT copy the string content into p.

-- Let's verify this behavior
local test_str = "TestString"
p_str_write["cstr"] = test_str -- Write address of "TestString" to p_str_write

-- Read back
local read_str = p_str_write["cstr"]
print("Read back string via cstr:", read_str)
assert_equal(read_str, test_str, "String pointer read/write failed")

-- To actually copy string content to memory, use ptr.copy or loop
local src_p = ptr.of(test_str)
local dst_p = p + 40
ptr.copy(dst_p, src_p, #test_str)

-- Null terminate manually if needed, though ptr.string with length doesn't need it
local term_p = p + 40
term_p[#test_str] = 0

print("Copied string content to buffer offset 40")
print("Buffer at 40 (string):", ptr.string(p + 40))
assert_equal(ptr.string(p + 40), test_str, "String content copy failed")


print_header("6. Memory Operations")

-- ptr.fill(p, val, len)
ptr.fill(p, 0xFF, 4)
print("Filled first 4 bytes with 0xFF")
assert_equal(ptr.tohex(p, 4), "FF FF FF FF", "ptr.fill failed")

-- ptr.compare(p1, p2, len)
local p_cmp1 = p + 40 -- "TestString"
local p_cmp2 = ptr.of("TestString")
local cmp_res = ptr.compare(p_cmp1, p_cmp2, 10)
print("Comparing buffer string with literal string:", cmp_res)
assert_equal(cmp_res, 0, "ptr.compare failed")


print_header("7. Struct Emulation")

-- Simulating a struct:
-- struct Point {
--   int x;    // offset 0
--   int y;    // offset 4
--   double z; // offset 8
-- }

local struct_p = ptr.malloc(16)
local x_offset = 0
local y_offset = 4
local z_offset = 8

-- Set values
(struct_p + x_offset)["int"] = 10
(struct_p + y_offset)["int"] = 20
(struct_p + z_offset)["double"] = 1.5

print("Struct Point { x=10, y=20, z=1.5 }")
print("x:", (struct_p + x_offset)["int"])
print("y:", (struct_p + y_offset)["int"])
print("z:", (struct_p + z_offset)["double"])

assert_equal((struct_p + x_offset)["int"], 10, "Struct member x failed")
assert_equal((struct_p + y_offset)["int"], 20, "Struct member y failed")
assert_equal((struct_p + z_offset)["double"], 1.5, "Struct member z failed")

ptr.free(struct_p)


print_header("8. Cleanup")

ptr.free(p)
print("Memory freed.")
print("\nAll tests passed successfully.")
