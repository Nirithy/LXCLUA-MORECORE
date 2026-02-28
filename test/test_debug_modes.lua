-- Improved test for step, next, finish

local function helper(n)
    print("  Helper starting with", n) -- line 4
    local sum = 0
    for i = 1, n do
        sum = sum + i
    end
    print("  Helper ending")
    return sum
end

local function main()
    print("Main starting")          -- line 14
    local a = helper(2)             -- line 15
    print("Main back from helper")  -- line 16
    local b = helper(3)             -- line 17
    print("Main ending")            -- line 18
end

local commands = {}
local current_command = 1

debug.setoutputcallback(function(event, src, line)
    print(string.format("[%s] hit at %s:%d", event, src, line))
    local cmd = commands[current_command]
    if cmd then
        print("Executing command:", cmd)
        current_command = current_command + 1
        if cmd == "step" then debug.step()
        elseif cmd == "next" then debug.next()
        elseif cmd == "finish" then debug.finish()
        elseif cmd == "continue" then debug.continue()
        end
    end
end)

print("\n--- Scenario 1: Stepping through main ---")
commands = {"step", "step", "step", "continue"}
current_command = 1
debug.setbreakpoint("test_debug_modes.lua", 14)
main()

print("\n--- Scenario 2: Next (step over) helper call ---")
-- Break at 15 (helper call), then 'next' should land on 16.
commands = {"next", "continue"}
current_command = 1
debug.clearbreakpoints()
debug.setbreakpoint("test_debug_modes.lua", 15)
main()

print("\n--- Scenario 3: Finish (step out) from helper ---")
-- Break at 4 (inside helper), then 'finish' should land on 16 (in main)
commands = {"finish", "continue"}
current_command = 1
debug.clearbreakpoints()
debug.setbreakpoint("test_debug_modes.lua", 4)
main()
