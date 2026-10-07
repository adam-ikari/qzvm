/* v2 isolates where the 9x actually lives. Same JS semantics, but the
 * recursion stays in int32 and only boxes at the JSValue boundary.
 * If this lands near native, the gap was needless coercion in v1.
 * If it stays ~9x, the gap is JSValue semantics themselves. */
#include <stdint.h>
#include "quickjs.h"

static int32_t fib_i(int32_t n)
{
    if (n < 2) return n;
    return fib_i(n - 1) + fib_i(n - 2);
}

JSValue aot_fib(JSContext *ctx, JSValue *args, int argc)
{
    int32_t n;
    (void)argc;
    /* read the int straight out of the value instead of calling the exported
     * coercion entry point — v1 paid a real function call per invocation. */
    if (JS_VALUE_IS_BOTH_INT(args[0], args[0]))
        n = JS_VALUE_GET_INT(args[0]);
    else
        JS_ToInt32(ctx, &n, args[0]);
    return JS_NewInt32(ctx, fib_i(n));
}
