local val = 1
local print_fn = print
-- Switch 语句
switch (val) do
    case 1:
        print_fn("One")
    case "test":
        print_fn("Test string")
    default:
        print_fn("Other")
end

-- Switch 表达式
local res = switch(val) do
    case 1: return "A"
    case 2: return "B"
end

-- Try-Catch 异常处理
try
    error("Something went wrong")
catch(e)
    print_fn("Caught error: " .. e)
finally
    print_fn("Cleanup resource")
end

-- Defer 延迟执行 (当前作用域结束时触发)
local f = io.open("file.txt", "r")
if f then
    defer do f:close() end  -- 自动关闭文件
    -- read file...
end

-- Namespace 命名空间
namespace MyLib {
    int version = 1;
    function test() print_fn("test") end
}
-- MyLib::test()

-- using namespace MyLib;
print_fn(version)  -- 1
