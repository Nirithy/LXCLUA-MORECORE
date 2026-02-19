interface Drawable
    function draw(self)
end

class Shape implements Drawable
    function draw()
        print("Drawing shape")
    end
end

class Circle extends Shape
    private _radius = 0

    -- 构造函数
    function __init__(r)
        self._radius = r
    end

    -- Getter 属性
    public get radius(self)
        return self._radius
    end

    -- Setter 属性
    public set radius(self, v)
        if v >= 0 then self._radius = v end
    end

    -- 重写方法
    function draw()
        super.draw()  -- 调用父类方法
        print("Drawing circle: " .. self._radius)
    end

    -- 静态方法
    static function create(r)
        return new Circle(r)
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20
c:draw()
