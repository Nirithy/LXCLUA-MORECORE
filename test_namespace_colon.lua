
namespace MySpace {
    int val = 10;

    int add(int x, int y) {
        return x + y;
    }

    namespace Inner {
        int innerVal = 20;
    }
}

-- Access global namespace member via ::
print("MySpace::val =", MySpace::val)
assert(MySpace::val == 10)

-- Call function via ::
local res = MySpace::add(5, 7)
print("MySpace::add(5, 7) =", res)
assert(res == 12)

-- Assign via ::
MySpace::val = 30
assert(MySpace::val == 30)

-- Chained access
print("MySpace::Inner::innerVal =", MySpace::Inner::innerVal)
assert(MySpace::Inner::innerVal == 20)

-- Using with ::
using MySpace::Inner::innerVal;
print("innerVal =", innerVal)
assert(innerVal == 20)

print("Namespace colon test passed!")
