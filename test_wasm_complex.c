/*
 * wasm3 复杂功能测试模块（无动态内存分配）
 * 编译: make wasm-c-all SRC=test_wasm_complex.c
 */

#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

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

/* 平方根（整数） */
EXPORT int32_t int_sqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n;
    int32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
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

/* ==================== 数论 ==================== */

/* 欧拉函数 */
EXPORT int32_t euler_phi(int32_t n) {
    int32_t result = n;
    for (int32_t i = 2; i * i <= n; i++) {
        if (n % i == 0) {
            while (n % i == 0) n /= i;
            result -= result / i;
        }
    }
    if (n > 1) result -= result / n;
    return result;
}

/* 费马小定理检验 */
EXPORT int32_t fermat_test(int32_t a, int32_t p, int32_t mod) {
    int64_t result = 1;
    int64_t base = a % mod;
    while (p > 0) {
        if (p & 1) result = (result * base) % mod;
        base = (base * base) % mod;
        p >>= 1;
    }
    return (int32_t)result;
}

/* ==================== 字符串哈希 ==================== */

/* 简单哈希 */
EXPORT uint32_t simple_hash(const char* s) {
    uint32_t hash = 0;
    while (*s) {
        hash = hash * 31 + (uint8_t)(*s);
        s++;
    }
    return hash;
}

/* FNV-1a 哈希 */
EXPORT uint32_t fnv1a_hash(const char* s) {
    uint32_t hash = 2166136261u;
    while (*s) {
        hash ^= (uint8_t)(*s);
        hash *= 16777619u;
        s++;
    }
    return hash;
}

/* ==================== 数值转换 ==================== */

/* 十进制转二进制位数 */
EXPORT int32_t count_bits(int32_t n) {
    int32_t count = 0;
    while (n) {
        count++;
        n >>= 1;
    }
    return count;
}

/* 数字反转 */
EXPORT int32_t reverse_number(int32_t n) {
    int32_t rev = 0;
    while (n) {
        rev = rev * 10 + n % 10;
        n /= 10;
    }
    return rev;
}

/* 回文数检测 */
EXPORT int32_t is_palindrome(int32_t n) {
    if (n < 0) return 0;
    return n == reverse_number(n);
}

/* 完全数检测 */
EXPORT int32_t is_perfect(int32_t n) {
    if (n <= 1) return 0;
    int32_t sum = 1;
    for (int32_t i = 2; i * i <= n; i++) {
        if (n % i == 0) {
            sum += i;
            if (i != n / i) sum += n / i;
        }
    }
    return sum == n;
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

/* 判断是否为三角数 */
EXPORT int32_t is_triangular(int64_t x) {
    int32_t n = (int32_t)((__builtin_sqrt((double)(8 * x + 1)) - 1) / 2);
    return (int64_t)n * (n + 1) / 2 == x;
}
