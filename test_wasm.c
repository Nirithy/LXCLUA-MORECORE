/*
 * wasm3 测试模块
 * 编译: make wasm-c SRC=test_wasm.c EXPORT_ALL=1
 */

#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* 两数相加 */
EXPORT int32_t add(int32_t a, int32_t b) {
    return a + b;
}

/* 两数相乘 */
EXPORT int32_t mul(int32_t a, int32_t b) {
    return a * b;
}

/* 阶乘 */
EXPORT int32_t factorial(int32_t n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

/* 斐波那契数列 */
EXPORT int32_t fib(int32_t n) {
    if (n <= 1) return n;
    int32_t a = 0, b = 1;
    for (int32_t i = 2; i <= n; i++) {
        int32_t tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

/* 浮点数运算 */
EXPORT double add_double(double a, double b) {
    return a + b;
}

/* 字符串长度（需要传入指针） */
EXPORT int32_t str_len(const char* s) {
    if (!s) return 0;
    int32_t len = 0;
    while (s[len]) len++;
    return len;
}
