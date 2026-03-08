local t = {
   a = ::start::,
   b = ::loop::,
   c = ::exit::
}

goto t.b  -- 跳 t.b 对应的标签

::start::
print("start")
goto t.c

::loop::
print("loop")
goto t.c

::exit::
print("exit")
