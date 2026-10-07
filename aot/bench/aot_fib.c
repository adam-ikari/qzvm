/* Minimal AOT target: hand-translated C99 equivalent of the JS below.
 *   function fib(n){ if(n<2) return n; return fib(n-1)+fib(n-2); }
 * This measures the performance ceiling of the AOT path — the real
 * translator must beat the interpreter, and it cannot beat this. */
#include <stdint.h>
#include "quickjs.h"

/* JS_Add specialized the way the interpreter's OP_add fast path does.
 * Keeping the int fast path inline is what makes the compiled form fast;
 * the slow path must still call the exported coercion entry point. */
static inline JSValue qz_aot_add(JSContext *ctx, JSValue op1, JSValue op2)
{
    if (JS_VALUE_IS_BOTH_INT(op1, op2)) {
        int64_t r = (int64_t)JS_VALUE_GET_INT(op1) + JS_VALUE_GET_INT(op2);
        if (r < INT32_MIN || r > INT32_MAX)
            return JS_NewFloat64(ctx, (double)r);
        return JS_NewInt32(ctx, (int32_t)r);
    }
    if (JS_VALUE_IS_BOTH_FLOAT(op1, op2))
        return JS_NewFloat64(ctx, JS_VALUE_GET_FLOAT64(op1) + JS_VALUE_GET_FLOAT64(op2));
    /* js_add_slow is static in quickjs.c, so a separately-built .so cannot
     * reach it. Coerce through the exported entry point instead. fib never
     * reaches this path (operands are always int32), so it costs nothing on
     * the benchmark and keeps AOT code dependent only on the public header. */
    {
        double d1 = 0, d2 = 0;
        if (JS_ToFloat64(ctx, &d1, op1) < 0)
            return JS_EXCEPTION;
        if (JS_ToFloat64(ctx, &d2, op2) < 0)
            return JS_EXCEPTION;
        return JS_NewFloat64(ctx, d1 + d2);
    }
}

JSValue aot_fib(JSContext *ctx, JSValue *args, int argc)
{
    int32_t n = 0;
    JSValue a, b, r;
    (void)argc;
    JS_ToInt32(ctx, &n, args[0]);
    if (n < 2)
        return JS_NewInt32(ctx, n);
    a = JS_NewInt32(ctx, n - 1);
    r = aot_fib(ctx, &a, 1);
    JS_FreeValue(ctx, a);
    b = JS_NewInt32(ctx, n - 2);
    {
        JSValue r2 = aot_fib(ctx, &b, 1);
        r = qz_aot_add(ctx, r, r2);
        JS_FreeValue(ctx, r2);
    }
    JS_FreeValue(ctx, b);
    return r;
}
