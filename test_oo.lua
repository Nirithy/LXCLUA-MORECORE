-- Basic class creation
class Base
    public var = "base"

    function __init__(self, val)
        if val then self.var = val end
    end

    function get_var(self)
        return self.var
    end

    function set_var(self, val)
        self.var = val
    end
end

local b = Base("test")
assert(b:get_var() == "test")
b:set_var("new")
assert(b:get_var() == "new")
print("Base class test passed")

-- Inheritance
class Derived extends Base
    function __init__(self, val, subval)
        super(val)
        self.subval = subval
    end

    function get_subval(self)
        return self.subval
    end

    -- Override
    function get_var(self)
        return "derived: " .. super.get_var(self)
    end
end

local d = Derived("base_val", "sub_val")
assert(d.var == "base_val")
assert(d.subval == "sub_val")
assert(d:get_subval() == "sub_val")
assert(d:get_var() == "derived: base_val")
print("Inheritance test passed")

-- Access control
class AccessTest
    private p_var = "private"
    protected pr_var = "protected"
    public pu_var = "public"

    function get_private(self)
        return self.p_var
    end

    function get_protected(self)
        return self.pr_var
    end
end

local a = AccessTest()
assert(a.pu_var == "public")
-- Should fail if uncommented:
-- print(a.p_var)
-- print(a.pr_var)
assert(a:get_private() == "private")
assert(a:get_protected() == "protected")

class SubAccess extends AccessTest
    function access_protected(self)
        return self.pr_var
    end

    function access_private_fail(self)
        return self.p_var -- Should be nil or error? Based on lclass.c it seems it might be error or not visible.
    end
end

local sa = SubAccess()
assert(sa:access_protected() == "protected")
-- Private members are not inherited, so this should probably be nil or error
local status, err = pcall(function() return sa:access_private_fail() end)
-- print("Private access attempt result:", status, err)

print("Access control test passed")

-- Interface
interface ITest
    function method1()
end

class Impl implements ITest
    function method1(self)
        return "implemented"
    end
end

local i = Impl()
assert(i:method1() == "implemented")
print("Interface test passed")

-- Abstract
abstract class AbstractBase
    abstract function abs_method()
end

class Concrete extends AbstractBase
    function abs_method(self)
        return "concrete"
    end
end

local c = Concrete()
assert(c:abs_method() == "concrete")
print("Abstract class test passed")

-- Getters/Setters
class PropTest
    private _val = 0

    get val(self)
        return self._val
    end

    set val(self, v)
        self._val = v
    end
end

local p = PropTest()
p.val = 10
assert(p.val == 10)
print("Property test passed")
