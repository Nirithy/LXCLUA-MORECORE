-- Test Namespace Feature

namespace MySpace {
    int a = 10;

    int add(int x, int y) {
        return x + y;
    }

    class Test {
        function show()
            print("Test object show")
        end
    }
}

print("MySpace.a:", MySpace.a)
assert(MySpace.a == 10)

MySpace::a = 20
print("MySpace::a updated:", MySpace.a)
assert(MySpace.a == 20)

print("MySpace::add(2, 3):", MySpace::add(2, 3))
assert(MySpace::add(2, 3) == 5)

-- Test using namespace
namespace Other {
    int b = 100;
}

using namespace Other;
-- Should find b in _ENV (via link)
print("b from Other:", b)
if b == 100 then
    print("using namespace worked!")
else
    print("using namespace failed to resolve b")
    -- Fail test if using namespace is critical feature
    assert(false, "using namespace failed")
end

-- Test using alias
using MySpace::add;
print("add(5,5):", add(5,5))
assert(add(5,5) == 10)

-- Test main
int main() {
    print("Inside main")
    MySpace::a = 30;
    print("MySpace::a in main:", MySpace.a)

    -- Using namespace inside function
    using namespace MySpace;
    assert(a == 30)
}

main()
assert(MySpace.a == 30)

print("All tests passed!")
