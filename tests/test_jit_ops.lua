local function mul(a, b) return a * b end
local function sub(a, b) return a - b end
local function div(a, b) return a / b end
local function mod(a, b) return a % b end
local function pow(a, b) return a ^ b end
local function idiv(a, b) return a // b end
local function band(a, b) return a & b end
local function bor(a, b) return a | b end
local function bxor(a, b) return a ~ b end
local function shl(a, b) return a << b end
local function shr(a, b) return a >> b end
local function unm(a) return -a end
local function bnot(a) return ~a end

local function test(name, expected, actual)
  if expected ~= actual then
    print("FAILED " .. name .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
    return false
  end
  return true
end

print("Testing JIT ops (individual functions)...")

local ok = true
ok = test("MUL", 200, mul(10, 20)) and ok
ok = test("SUB", 10, sub(20, 10)) and ok
ok = test("DIV", 2.0, div(20, 10)) and ok
ok = test("MOD", 1, mod(21, 10)) and ok
ok = test("POW", 100.0, pow(10, 2)) and ok
ok = test("IDIV", 2, idiv(21, 10)) and ok
ok = test("BAND", 1, band(3, 1)) and ok
ok = test("BOR", 3, bor(2, 1)) and ok
ok = test("BXOR", 2, bxor(3, 1)) and ok
ok = test("SHL", 2, shl(1, 1)) and ok
ok = test("SHR", 1, shr(2, 1)) and ok
ok = test("UNM", -10, unm(10)) and ok
ok = test("BNOT", -1, bnot(0)) and ok

if ok then
  print("SUCCESS")
else
  print("FAILURE")
  os.exit(1)
end
