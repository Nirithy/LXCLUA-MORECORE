local dispatch = {::L1::, ::L2::, ::L3::}
local pcc = 1
::loop::
   if pcc == 0 then goto (::my_end_label::) end
   goto dispatch[pcc]
::L1::
print("L1")
pcc = 2
goto loop
::L2::
print("L2")
pcc = 3
goto loop
::L3::
print("L3")
pcc = 0
goto loop

::my_end_label::
print("finish")
