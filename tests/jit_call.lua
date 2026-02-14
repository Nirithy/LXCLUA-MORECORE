function add(a, b)
  return a + b
end

function complex_call(x)
  local y = add(x, 10)
  return y * 2
end

print("Testing JIT CALL...")
local res = complex_call(5)
print("Result: " .. res)
if res == 30 then
  print("SUCCESS")
else
  print("FAILURE: Expected 30, got " .. res)
  os.exit(1)
end
