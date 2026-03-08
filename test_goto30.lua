local function get_label()
    return ::my_label::
end

print(type(get_label()))

local target = get_label()
goto target

print("skipped")

::my_label::
print("success")
