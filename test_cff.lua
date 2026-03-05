local func = function()
  print("test")
end
local obfuscated = string.dump(func, { obfuscate = 1 })
local f = load(obfuscated)
f()
