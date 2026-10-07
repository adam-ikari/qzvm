/* Pure C99 baseline — the true native ceiling. No JSValue boxing, no runtime,
 * no GC. Whatever gap remains between this and the AOT build is the cost of
 * living inside QuickJS's value model, and therefore the ceiling on what any
 * AOT translation of JS can achieve. */
#include <stdint.h>

int32_t native_fib(int32_t n)
{
    if (n < 2)
        return n;
    return native_fib(n - 1) + native_fib(n - 2);
}