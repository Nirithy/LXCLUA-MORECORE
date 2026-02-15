local function kill()
  local t1 = os.clock()
  local a = 123
  local x = 0
  for i = 1, 2000000 do
    local ok, res = pcall(function() return a['xxx'] end)
    if ok then x = x + 1 end
  end
  local t2 = os.clock()
  print(string.format("test2 非table取值     耗时: %.3f", t2 - t1))
end

kill()

