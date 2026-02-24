local f = function(T)(x) requires type(T) == "number" return x end; local inner = f(10); print(inner("hello"))
