-- 定义结构体
struct Point {
    int x;
    int y;
}

-- 实例化结构体
local p = Point()
p.x = 10
p.y = 20

-- 定义枚举
enum Color {
    Red,
    Green,
    Blue = 10
}
print(Color.Red)   -- 0
print(Color.Blue)  -- 10

-- 强类型变量声明
int counter = 100;
string message = "Hello";

-- 解构赋值 (Destructuring)
local data = { x = 1, y = 2 }
local take { x, y } = data
print(x, y)  -- 1, 2
