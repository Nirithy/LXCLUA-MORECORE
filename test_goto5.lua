local function f()
    local t = {
       a = ::start::,
       b = ::loop::,
       c = ::exit::
    }

    print(t.b)

    goto t["b"]

    ::start::
    print("start")
    goto t.c

    ::loop::
    print("loop")
    goto t.c

    ::exit::
    print("exit")
end

f()
