local pcc = 1
::loop::
   if pcc == 0 then goto my_end_label end
   pcc = 0
   goto loop

::my_end_label::
print("finish")
