/*
 * wasm3 C语言高级功能测试模块（纯计算，无系统调用）
 * 编译: make wasm-c-all SRC=test_wasm_advanced.c
 */

#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* ==================== 结构体定义 ==================== */

typedef struct {
    int32_t x;
    int32_t y;
} Point;

typedef struct {
    int32_t width;
    int32_t height;
} Rectangle;

/* ==================== 内存操作（使用线性内存） ==================== */

/* 读取 int32 */
EXPORT int32_t read_i32(const void* ptr, int32_t offset) {
    const int32_t* p = (const int32_t*)ptr;
    return p[offset];
}

/* 写入 int32 */
EXPORT void write_i32(void* ptr, int32_t offset, int32_t value) {
    int32_t* p = (int32_t*)ptr;
    p[offset] = value;
}

/* 读取 int64 */
EXPORT int64_t read_i64(const void* ptr, int32_t offset) {
    const int64_t* p = (const int64_t*)ptr;
    return p[offset];
}

/* 写入 int64 */
EXPORT void write_i64(void* ptr, int32_t offset, int64_t value) {
    int64_t* p = (int64_t*)ptr;
    p[offset] = value;
}

/* 读取 float */
EXPORT float read_f32(const void* ptr, int32_t offset) {
    const float* p = (const float*)ptr;
    return p[offset];
}

/* 写入 float */
EXPORT void write_f32(void* ptr, int32_t offset, float value) {
    float* p = (float*)ptr;
    p[offset] = value;
}

/* 读取 double */
EXPORT double read_f64(const void* ptr, int32_t offset) {
    const double* p = (const double*)ptr;
    return p[offset];
}

/* 写入 double */
EXPORT void write_f64(void* ptr, int32_t offset, double value) {
    double* p = (double*)ptr;
    p[offset] = value;
}

/* ==================== 数组操作 ==================== */

/* 数组求和 */
EXPORT int32_t array_sum(const int32_t* arr, int32_t len) {
    int32_t sum = 0;
    for (int32_t i = 0; i < len; i++) {
        sum += arr[i];
    }
    return sum;
}

/* 数组最大值 */
EXPORT int32_t array_max(const int32_t* arr, int32_t len) {
    if (len <= 0) return 0;
    int32_t max = arr[0];
    for (int32_t i = 1; i < len; i++) {
        if (arr[i] > max) max = arr[i];
    }
    return max;
}

/* 数组最小值 */
EXPORT int32_t array_min(const int32_t* arr, int32_t len) {
    if (len <= 0) return 0;
    int32_t min = arr[0];
    for (int32_t i = 1; i < len; i++) {
        if (arr[i] < min) min = arr[i];
    }
    return min;
}

/* 数组平均值 */
EXPORT double array_avg(const int32_t* arr, int32_t len) {
    if (len <= 0) return 0.0;
    int64_t sum = 0;
    for (int32_t i = 0; i < len; i++) {
        sum += arr[i];
    }
    return (double)sum / len;
}

/* 数组反转 */
EXPORT void array_reverse(int32_t* arr, int32_t len) {
    for (int32_t i = 0; i < len / 2; i++) {
        int32_t tmp = arr[i];
        arr[i] = arr[len - 1 - i];
        arr[len - 1 - i] = tmp;
    }
}

/* 数组排序（冒泡） */
EXPORT void array_sort_bubble(int32_t* arr, int32_t len) {
    for (int32_t i = 0; i < len - 1; i++) {
        for (int32_t j = 0; j < len - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int32_t tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

/* 数组排序（快速排序） */
static void quick_sort_impl(int32_t* arr, int32_t low, int32_t high) {
    if (low < high) {
        int32_t pivot = arr[high];
        int32_t i = low - 1;
        for (int32_t j = low; j < high; j++) {
            if (arr[j] <= pivot) {
                i++;
                int32_t tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
        }
        int32_t tmp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = tmp;
        int32_t pi = i + 1;
        quick_sort_impl(arr, low, pi - 1);
        quick_sort_impl(arr, pi + 1, high);
    }
}

EXPORT void array_sort_quick(int32_t* arr, int32_t len) {
    quick_sort_impl(arr, 0, len - 1);
}

/* 二分查找 */
EXPORT int32_t array_binary_search(const int32_t* arr, int32_t len, int32_t target) {
    int32_t left = 0, right = len - 1;
    while (left <= right) {
        int32_t mid = left + (right - left) / 2;
        if (arr[mid] == target) return mid;
        if (arr[mid] < target) left = mid + 1;
        else right = mid - 1;
    }
    return -1;
}

/* 数组填充 */
EXPORT void array_fill(int32_t* arr, int32_t len, int32_t value) {
    for (int32_t i = 0; i < len; i++) {
        arr[i] = value;
    }
}

/* 数组拷贝 */
EXPORT void array_copy(int32_t* dst, const int32_t* src, int32_t len) {
    for (int32_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
}

/* 数组查找 */
EXPORT int32_t array_find(const int32_t* arr, int32_t len, int32_t value) {
    for (int32_t i = 0; i < len; i++) {
        if (arr[i] == value) return i;
    }
    return -1;
}

/* 数组计数 */
EXPORT int32_t array_count(const int32_t* arr, int32_t len, int32_t value) {
    int32_t count = 0;
    for (int32_t i = 0; i < len; i++) {
        if (arr[i] == value) count++;
    }
    return count;
}

/* ==================== Point 结构体操作 ==================== */

/* 创建点（返回结构体值） */
EXPORT Point point_create(int32_t x, int32_t y) {
    Point p = {x, y};
    return p;
}

/* 获取 X 坐标 */
EXPORT int32_t point_get_x(Point p) {
    return p.x;
}

/* 获取 Y 坐标 */
EXPORT int32_t point_get_y(Point p) {
    return p.y;
}

/* 点距离平方（使用分开的坐标参数） */
EXPORT int64_t point_distance_sq_xy(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int64_t dx = x1 - x2;
    int64_t dy = y1 - y2;
    return dx * dx + dy * dy;
}

/* 点距离平方 */
EXPORT int64_t point_distance_sq(Point a, Point b) {
    return point_distance_sq_xy(a.x, a.y, b.x, b.y);
}

/* 点距离（浮点，使用分开的坐标参数） */
EXPORT double point_distance_xy(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int64_t sq = point_distance_sq_xy(x1, y1, x2, y2);
    return __builtin_sqrt((double)sq);
}

/* 点距离（浮点） */
EXPORT double point_distance(Point a, Point b) {
    return point_distance_xy(a.x, a.y, b.x, b.y);
}

/* 点相等判断 */
EXPORT int32_t point_equals(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

/* 点的曼哈顿距离（使用分开的坐标参数） */
EXPORT int32_t point_manhattan_xy(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = x1 > x2 ? x1 - x2 : x2 - x1;
    int32_t dy = y1 > y2 ? y1 - y2 : y2 - y1;
    return dx + dy;
}

/* 点的曼哈顿距离 */
EXPORT int32_t point_manhattan(Point a, Point b) {
    return point_manhattan_xy(a.x, a.y, b.x, b.y);
}

/* 点移动 */
EXPORT Point point_move(Point p, int32_t dx, int32_t dy) {
    p.x += dx;
    p.y += dy;
    return p;
}

/* ==================== Rectangle 结构体操作 ==================== */

/* 创建矩形 */
EXPORT Rectangle rect_create(int32_t width, int32_t height) {
    Rectangle r = {width, height};
    return r;
}

/* 计算面积（使用分开的参数） */
EXPORT int32_t rect_area_wh(int32_t width, int32_t height) {
    return width * height;
}

/* 计算面积 */
EXPORT int32_t rect_area(Rectangle r) {
    return rect_area_wh(r.width, r.height);
}

/* 计算周长（使用分开的参数） */
EXPORT int32_t rect_perimeter_wh(int32_t width, int32_t height) {
    return 2 * (width + height);
}

/* 计算周长 */
EXPORT int32_t rect_perimeter(Rectangle r) {
    return rect_perimeter_wh(r.width, r.height);
}

/* 判断点是否在矩形内（使用分开的参数） */
EXPORT int32_t rect_contains_point_wh(int32_t width, int32_t height, int32_t x, int32_t y) {
    return x >= 0 && x < width && y >= 0 && y < height;
}

/* 判断点是否在矩形内 */
EXPORT int32_t rect_contains_point(Rectangle r, int32_t x, int32_t y) {
    return rect_contains_point_wh(r.width, r.height, x, y);
}

/* 矩形缩放 */
EXPORT Rectangle rect_scale(Rectangle r, int32_t factor) {
    r.width *= factor;
    r.height *= factor;
    return r;
}

/* ==================== 数学运算 ==================== */

/* 幂运算 */
EXPORT int32_t power(int32_t base, int32_t exp) {
    int32_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

/* 最大公约数 */
EXPORT int32_t gcd(int32_t a, int32_t b) {
    while (b != 0) {
        int32_t tmp = b;
        b = a % b;
        a = tmp;
    }
    return a;
}

/* 最小公倍数 */
EXPORT int32_t lcm(int32_t a, int32_t b) {
    return (a / gcd(a, b)) * b;
}

/* 判断素数 */
EXPORT int32_t is_prime(int32_t n) {
    if (n <= 1) return 0;
    if (n <= 3) return 1;
    if (n % 2 == 0 || n % 3 == 0) return 0;
    for (int32_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    }
    return 1;
}

/* 第n个素数 */
EXPORT int32_t nth_prime(int32_t n) {
    int32_t count = 0;
    int32_t num = 2;
    while (count < n) {
        if (is_prime(num)) count++;
        if (count < n) num++;
    }
    return num;
}

/* ==================== 位操作 ==================== */

/* 位计数 */
EXPORT int32_t popcount(uint32_t x) {
    int32_t count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

/* 反转位 */
EXPORT uint32_t reverse_bits(uint32_t x) {
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

/* 循环左移 */
EXPORT uint32_t rotl(uint32_t x, int32_t n) {
    n &= 31;
    return (x << n) | (x >> (32 - n));
}

/* 循环右移 */
EXPORT uint32_t rotr(uint32_t x, int32_t n) {
    n &= 31;
    return (x >> n) | (x << (32 - n));
}

/* ==================== 递归算法 ==================== */

/* 汉诺塔移动次数 */
EXPORT int64_t hanoi_moves(int32_t n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    return 2 * hanoi_moves(n - 1) + 1;
}

/* 组合数 C(n, k) */
EXPORT int64_t combination(int32_t n, int32_t k) {
    if (k < 0 || k > n) return 0;
    if (k == 0 || k == n) return 1;
    return combination(n - 1, k - 1) + combination(n - 1, k);
}

/* 杨辉三角第n行第k列 */
EXPORT int64_t pascal(int32_t n, int32_t k) {
    return combination(n, k);
}

/* ==================== 斐波那契变体 ==================== */

/* 斐波那契（迭代） */
EXPORT int64_t fib(int32_t n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int64_t a = 0, b = 1;
    for (int32_t i = 2; i <= n; i++) {
        int64_t tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

/* 卢卡斯数 */
EXPORT int64_t lucas(int32_t n) {
    if (n == 0) return 2;
    if (n == 1) return 1;
    int64_t a = 2, b = 1;
    for (int32_t i = 2; i <= n; i++) {
        int64_t tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

/* 佩尔数 */
EXPORT int64_t pell(int32_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    int64_t a = 0, b = 1;
    for (int32_t i = 2; i <= n; i++) {
        int64_t tmp = 2 * b + a;
        a = b;
        b = tmp;
    }
    return b;
}

/* ==================== 三角数 ==================== */

/* 三角数 */
EXPORT int64_t triangular(int32_t n) {
    return (int64_t)n * (n + 1) / 2;
}

/* 五角数 */
EXPORT int64_t pentagonal(int32_t n) {
    return (int64_t)n * (3 * n - 1) / 2;
}

/* 六角数 */
EXPORT int64_t hexagonal(int32_t n) {
    return (int64_t)n * (2 * n - 1);
}

/* ==================== 矩阵操作 ==================== */

/* 矩阵获取元素 */
EXPORT int32_t matrix_get(const int32_t* m, int32_t cols, int32_t row, int32_t col) {
    return m[row * cols + col];
}

/* 矩阵设置元素 */
EXPORT void matrix_set(int32_t* m, int32_t cols, int32_t row, int32_t col, int32_t value) {
    m[row * cols + col] = value;
}

/* 矩阵填充 */
EXPORT void matrix_fill(int32_t* m, int32_t rows, int32_t cols, int32_t value) {
    for (int32_t i = 0; i < rows * cols; i++) {
        m[i] = value;
    }
}

/* 矩阵求和 */
EXPORT int64_t matrix_sum(const int32_t* m, int32_t rows, int32_t cols) {
    int64_t sum = 0;
    for (int32_t i = 0; i < rows * cols; i++) {
        sum += m[i];
    }
    return sum;
}

/* 矩阵转置 */
EXPORT void matrix_transpose(const int32_t* src, int32_t* dst, int32_t rows, int32_t cols) {
    for (int32_t i = 0; i < rows; i++) {
        for (int32_t j = 0; j < cols; j++) {
            dst[j * rows + i] = src[i * cols + j];
        }
    }
}

/* 矩阵迹（对角线元素和） */
EXPORT int32_t matrix_trace(const int32_t* m, int32_t n) {
    int32_t trace = 0;
    for (int32_t i = 0; i < n; i++) {
        trace += m[i * n + i];
    }
    return trace;
}

/* 3x3 矩阵行列式 */
EXPORT int32_t matrix_det_3x3(const int32_t* m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7])
         - m[1] * (m[3] * m[8] - m[5] * m[6])
         + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

/* ==================== 回调函数模拟 ==================== */

/* 内置变换函数 */
EXPORT int32_t transform_double(int32_t x) { return x * 2; }
EXPORT int32_t transform_square(int32_t x) { return x * x; }
EXPORT int32_t transform_negate(int32_t x) { return -x; }
EXPORT int32_t transform_abs(int32_t x) { return x < 0 ? -x : x; }

/* 对数组应用变换 */
EXPORT void array_transform(int32_t* arr, int32_t len, int32_t transform_id) {
    int32_t (*funcs[4])(int32_t) = {
        transform_double,
        transform_square,
        transform_negate,
        transform_abs
    };
    int32_t (*f)(int32_t) = funcs[transform_id % 4];
    for (int32_t i = 0; i < len; i++) {
        arr[i] = f(arr[i]);
    }
}

/* ==================== 高级算法 ==================== */

/* 动态规划：最大子数组和 */
EXPORT int32_t max_subarray_sum(const int32_t* arr, int32_t len) {
    if (len <= 0) return 0;
    
    int32_t max_sum = arr[0];
    int32_t current_sum = arr[0];
    
    for (int32_t i = 1; i < len; i++) {
        if (current_sum > 0) {
            current_sum += arr[i];
        } else {
            current_sum = arr[i];
        }
        if (current_sum > max_sum) {
            max_sum = current_sum;
        }
    }
    return max_sum;
}

/* 动态规划：最长递增子序列长度（简化版，使用固定大小数组） */
#define LIS_MAX_LEN 100
static int32_t lis_dp[LIS_MAX_LEN];

EXPORT int32_t lis_length(const int32_t* arr, int32_t len) {
    if (len <= 0) return 0;
    if (len > LIS_MAX_LEN) len = LIS_MAX_LEN;
    
    for (int32_t i = 0; i < len; i++) {
        lis_dp[i] = 1;
        for (int32_t j = 0; j < i; j++) {
            if (arr[j] < arr[i] && lis_dp[j] + 1 > lis_dp[i]) {
                lis_dp[i] = lis_dp[j] + 1;
            }
        }
    }
    
    int32_t max_len = lis_dp[0];
    for (int32_t i = 1; i < len; i++) {
        if (lis_dp[i] > max_len) max_len = lis_dp[i];
    }
    
    return max_len;
}

/* ==================== 字符串操作 ==================== */

/* 字符串长度 */
EXPORT int32_t str_length(const char* s) {
    if (!s) return 0;
    int32_t len = 0;
    while (s[len]) len++;
    return len;
}

/* 字符串哈希 */
EXPORT uint32_t str_hash(const char* s) {
    uint32_t hash = 5381;
    while (*s) {
        hash = ((hash << 5) + hash) + (unsigned char)*s;
        s++;
    }
    return hash;
}

/* 字符串反转 */
EXPORT void str_reverse(char* s) {
    int32_t len = str_length(s);
    for (int32_t i = 0; i < len / 2; i++) {
        char tmp = s[i];
        s[i] = s[len - 1 - i];
        s[len - 1 - i] = tmp;
    }
}

/* 转大写 */
EXPORT void str_to_upper(char* s) {
    while (*s) {
        if (*s >= 'a' && *s <= 'z') {
            *s = *s - 'a' + 'A';
        }
        s++;
    }
}

/* 转小写 */
EXPORT void str_to_lower(char* s) {
    while (*s) {
        if (*s >= 'A' && *s <= 'Z') {
            *s = *s - 'A' + 'a';
        }
        s++;
    }
}
