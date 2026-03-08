local function f()
    local x = ::abc::
    ::abc::
    return x
end
print(type(f()))
