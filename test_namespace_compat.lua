namespace MySpace {
  int x = 42;
  float y = 3.14;
}

print("MySpace:", MySpace.x, MySpace.y)
assert(MySpace.x == 42)
assert(MySpace.y == 3.14)

struct InNamespace {
  int val;
}

local ins = InNamespace{val=123}
print("InNamespace:", ins.val)
assert(ins.val == 123)

print("Namespace tests passed!")
