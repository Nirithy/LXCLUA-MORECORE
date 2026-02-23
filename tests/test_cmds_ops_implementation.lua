
print("Testing New Implementation of _CMDS and _OPERATORS...")

-- Verify _G._CMDS does NOT exist
if _G._CMDS == nil then
    print("PASS: _G._CMDS does not exist (as expected)")
else
    print("FAIL: _G._CMDS still exists!")
    os.exit(1)
end

-- Verify _G._OPERATORS does NOT exist
if _G._OPERATORS == nil then
    print("PASS: _G._OPERATORS does not exist (as expected)")
else
    print("FAIL: _G._OPERATORS still exists!")
    os.exit(1)
end

-- Define a command
command test_cmd_new(x)
    return x * 3
end

-- Verify functionality
if test_cmd_new(10) == 30 then
    print("PASS: command execution works with new implementation")
else
    print("FAIL: command execution failed")
    os.exit(1)
end

-- Define operator
operator testop (a, b)
    return a * b
end

-- Verify operator
if $$testop(10, 2) == 20 then
    print("PASS: operator works with new implementation")
else
    print("FAIL: operator failed")
    os.exit(1)
end

-- Verify internal accessor exists (optional check)
if __lxc_get_cmds then
    print("PASS: internal accessor __lxc_get_cmds found")
else
    print("FAIL: internal accessor __lxc_get_cmds missing")
end

print("New implementation test complete.")
