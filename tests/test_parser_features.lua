-- Tests for undocumented or advanced parser features in LXCLUA-NCore

print("Running Parser Features Tests...")

local function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("Assertion failed: %s (expected %s, got %s)", msg or "", tostring(b), tostring(a)))
    end
end

local function assert_true(cond, msg)
    if not cond then
        error(string.format("Assertion failed: %s", msg or "expected true"))
    end
end

-- 1. Superstruct
do
    print("1. Testing Superstruct...")
    superstruct MySuper [
        key1: 100,
        key2: "val",
        ["dynamic"]: true
    ]
    assert_eq(MySuper.key1, 100, "superstruct key1")
    assert_eq(MySuper.key2, "val", "superstruct key2")
    assert_eq(MySuper.dynamic, true, "superstruct dynamic key")
end

-- 2. Async/Await and __async_wrap
do
    print("2. Testing Async/Await logic...")
    local async_called = false
    function _G.__async_wrap(func)
        return function(...)
            async_called = true
            return func(...)
        end
    end

    async function myAsyncFunc(x)
        return x * 2
    end

    local res = myAsyncFunc(21)
    assert_true(async_called, "__async_wrap was called")
    assert_eq(res, 42, "Async function result")
end

-- 3. Advanced Generic Factory
do
    print("3. Testing Advanced Generics...")
    local wrap_called = false
    function _G.__generic_wrap(factory, params, mappings)
        wrap_called = true
        return factory(params[1])
    end

    local function MyGen(T)(val)
        return { type = T, value = val }
    end

    local instance = MyGen("int")(99)
    assert_true(wrap_called, "__generic_wrap called")
    assert_eq(instance.type, "int", "Generic type passed")
    assert_eq(instance.value, 99, "Generic value passed")
end

-- 4. Destructuring (local take)
do
    print("4. Testing Destructuring (take)...")
    local t = { a = 1, b = 2, c = 3 }
    local take { a, c } = t
    assert_eq(a, 1, "take a")
    assert_eq(c, 3, "take c")
    assert_true(b == nil, "b should not be destructured")
end

-- 5. ASM Junk/Obfuscation
-- Disabled due to register corruption issues
-- do
--     print("5. Testing ASM Junk...")
--     local function run_junk()
--         asm(
--             junk "some_random_string_data"
--             junk 5
--         )
--     end
--     run_junk()
--     print("ASM Junk syntax passed")
-- end

-- 6. ASM Control Flow
-- Disabled due to register corruption issues
-- do
--     print("6. Testing ASM Control Flow...")
--     local function run_asm()
--         local val = 0
--         asm(
--             newreg r0
--             LOADI r0 10
--             _if 1
--                 LOADI r0 20
--             _else
--                 LOADI r0 30
--             _endif

--             _if 0
--                LOADI r0 40
--             _endif
--         )
--     end
--     run_asm()
--     print("ASM Control Flow syntax passed")
-- end

-- 7. Global Function
do
    print("7. Testing Global Function...")
    local env = _ENV
    global function MyGlobalFunc()
        return "global_ok"
    end
    assert_eq(env.MyGlobalFunc(), "global_ok", "Global function defined in _ENV")
end

-- 8. Class Modifiers (Parser check)
do
    print("8. Testing Class Modifiers...")
    abstract class AbstractShape
        abstract function area(self)
    end
    final class FixedPoint
        x = 0
        y = 0
    end
    sealed class SafeBox
    end
    class AccessTest
        private _secret = 123
        public visible = 456
        static count = 0
        private function getSecret(self) return self._secret end
        static function getCount() return AccessTest.count end
        final function cannotOverride(self) end
    end
    local a = new AccessTest()
    assert_eq(a.visible, 456, "Public field access")
    assert_eq(AccessTest.count, 0, "Static field access")
end

-- 9. Properties (get/set)
do
    print("9. Testing Properties...")
    class PropTest
        _val = 0
        get val(self) return self._val end
        set val(self, v) self._val = v end
    end
    local p = new PropTest()
    p.val = 10
    assert_eq(p.val, 10, "Property getter/setter")
end

print("Parser Features Tests Completed.")
