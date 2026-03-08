local a = ::label1::
goto a
print("should not see this")
::label1::
print("label1 works")
