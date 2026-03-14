local dump_code = string.dump(function()
    print("Test segmented bytecode!")
    local function inner()
        print("Inner nested function")
    end
    inner()
end)

local f = load(dump_code)
f()
