local function f()
    local x = ::abc::
    return x
end
print(type(f()))
