_OPERATORS = {}
-- 自定义命令
command echo(msg)
    print(msg)
end
echo "Hello World"  -- 等价于 echo("Hello World")

-- 自定义运算符 (++ 前缀/后缀)
operator ++ (x)
    return x + 1
end
-- 调用: $$++(val)

-- 预处理指令
$define DEBUG 1

$if DEBUG
    print("Debug mode on")
$else
    print("Debug mode off")
$end
