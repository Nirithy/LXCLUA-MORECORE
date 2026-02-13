struct TestTypes {
  int i;
  float f;
  bool b;
  string s;
}

local t = TestTypes{i=1, f=2.5, b=true, s="hello"}
print("TestTypes:", t.i, t.f, t.b, t.s)
assert(t.i == 1)
assert(t.f == 2.5)
assert(t.b == true)
assert(t.s == "hello")

struct Point {
  int x;
  int y;
}

struct Rect {
  Point p1;
  Point p2;
}

local r = Rect{p1=Point{x=0, y=0}, p2=Point{x=10, y=10}}
print("Rect:", r.p1.x, r.p1.y, r.p2.x, r.p2.y)
assert(r.p1.x == 0)
assert(r.p2.y == 10)

struct WithDefaults {
  int i = 100;
  float f = 3.14;
}

local wd = WithDefaults{}
print("WithDefaults:", wd.i, wd.f)
assert(wd.i == 100)
assert(wd.f == 3.14)

print("All struct type tests passed!")
