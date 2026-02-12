-- tests/test_dump_envelop.lua

local function check_envelop(bytecode, should_have_envelop)
  local signature = "Nirithy=="
  if should_have_envelop then
    if string.sub(bytecode, 1, #signature) ~= signature then
      error("Expected bytecode to have Nirithy== shell, but it didn't.")
    end
    print("Envelop check passed: Present")
  else
    if string.sub(bytecode, 1, #signature) == signature then
      error("Expected bytecode NOT to have Nirithy== shell, but it did.")
    end
    print("Envelop check passed: Absent")
  end
end

local function my_func()
  print("Hello")
end

print("Testing string.dump(f)...")
local dump1 = string.dump(my_func)
check_envelop(dump1, true)

print("Testing string.dump(f, true)...")
local dump2 = string.dump(my_func, true)
check_envelop(dump2, true)

print("Testing string.dump(f, { strip = true })...")
local dump3 = string.dump(my_func, { strip = true })
check_envelop(dump3, true)

print("Testing string.dump(f, { envelop = false })...")
local dump4 = string.dump(my_func, { envelop = false })
check_envelop(dump4, false)

print("Testing string.dump(f, { envelop = true })...")
local dump5 = string.dump(my_func, { envelop = true })
check_envelop(dump5, true)

print("Testing string.dump(f, { strip = true, envelop = false })...")
local dump6 = string.dump(my_func, { strip = true, envelop = false })
check_envelop(dump6, false)

print("Testing string.envelop('test')...")
local enveloped = string.envelop("test")
check_envelop(enveloped, true)

print("All tests passed!")
