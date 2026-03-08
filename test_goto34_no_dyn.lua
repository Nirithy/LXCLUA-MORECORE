goto my_end_label

local pcc = 1
::loop::
   if pcc == 0 then goto my_end_label end
   goto loop

::my_end_label::
print("finish")
