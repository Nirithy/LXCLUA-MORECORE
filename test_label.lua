
function f()
  print("f called")
  return {}
end

goto lbl

::lbl::
print("Label reached")

-- Test with explicit delimiter
f();
::L2::
print("Label 2 reached")
