/* Three-way comparison in one process against one JSRuntime:
 *
 *   native C     — the ceiling. No JSValue ever crosses a call boundary.
 *   aot          — JS translated to C99, gcc -O2, dlopen'd .so, but still
 *                  living inside QuickJS's value model.
 *   interpreter  — QuickJS bytecode dispatch.
 *
 * native vs aot is therefore the cost of the value model (boxing, refcounting,
 * GC), which is the hard ceiling on what any JS->C translation can reach.
 * aot vs interpreter is the payoff.
 *
 * usage: bench [n] [reps] */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "quickjs.h"

int32_t native_fib(int32_t n);

typedef JSValue (*fib_fn)(JSContext *, JSValue *, int);

static const char *SRC =
    "globalThis.fib = function fib(n){"
    "  if (n < 2) return n;"
    "  return fib(n-1) + fib(n-2);"
    "};\n";

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

typedef JSValue (*call_fn)(void *, JSContext *, JSValue *, int);

static double time_calls(call_fn call, void *ud, JSContext *ctx, JSValue *arg,
                         int reps, int warm, int32_t *out)
{
    double *t = malloc(sizeof(double) * reps);
    int i;
    for (i = 0; i < warm; i++) {
        JSValue r = call(ud, ctx, arg, 1);
        JS_FreeValue(ctx, r);
    }
    for (i = 0; i < reps; i++) {
        double t0 = now_ms();
        JSValue r = call(ud, ctx, arg, 1);
        t[i] = now_ms() - t0;
        JS_FreeValue(ctx, r);
    }
    {
        JSValue r = call(ud, ctx, arg, 1);
        JS_ToInt32(ctx, out, r);
        JS_FreeValue(ctx, r);
    }
    qsort(t, reps, sizeof(double), cmp_double);
    {
        double med = t[reps / 2];
        free(t);
        return med;
    }
}

static JSValue call_interp(void *ud, JSContext *ctx, JSValue *arg, int argc)
{
    JSValue fnval = *(JSValue *)ud;
    JSValue nargs[1];
    nargs[0] = *arg;
    (void)argc;
    return JS_Call(ctx, fnval, JS_UNDEFINED, 1, nargs);
}

static JSValue call_aot(void *ud, JSContext *ctx, JSValue *arg, int argc)
{
    return ((fib_fn)ud)(ctx, arg, argc);
}

static JSValue call_native(void *ud, JSContext *ctx, JSValue *arg, int argc)
{
    int32_t n = 0;
    (void)ud; (void)argc;
    JS_ToInt32(ctx, &n, *arg);
    return JS_NewInt32(ctx, native_fib(n));
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 27;
    int reps = argc > 2 ? atoi(argv[2]) : 11;
    int warm = 3;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue global, fnval, arg;
    int32_t vi = 0, va = 0, vn = 0;
    double ti, ta, tn;
    void *h;
    fib_fn aot;

    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);

    {
        JSValue setup = JS_Eval(ctx, SRC, strlen(SRC), "<setup>",
                               JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(setup)) {
            JSValue e = JS_GetException(ctx);
            const char *m = JS_ToCString(ctx, e);
            fprintf(stderr, "setup eval failed: %s\n", m ? m : "?");
            return 1;
        }
        JS_FreeValue(ctx, setup);
    }
    global = JS_GetGlobalObject(ctx);
    fnval = JS_GetPropertyStr(ctx, global, "fib");
    if (JS_VALUE_GET_TAG(fnval) != JS_TAG_OBJECT) {
        fprintf(stderr, "fib is not a function\n");
        return 1;
    }

    arg = JS_NewInt32(ctx, n);

    ti = time_calls(call_interp, &fnval, ctx, &arg, reps, warm, &vi);
    tn = time_calls(call_native, NULL, ctx, &arg, reps, warm, &vn);

    h = dlopen("./libaotfib.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    aot = (fib_fn)dlsym(h, "aot_fib");
    if (!aot) {
        fprintf(stderr, "dlsym(aot_fib) failed\n");
        return 1;
    }
    ta = time_calls(call_aot, (void *)aot, ctx, &arg, reps, warm, &va);

    printf("fib(%d), median of %d runs (%d warmup)\n", n, reps, warm);
    printf("  native C    : %9.3f ms   result=%d\n", tn, vn);
    printf("  aot (c99)   : %9.3f ms   result=%d\n", ta, va);
    printf("  interpreter : %9.3f ms   result=%d\n", ti, vi);
    if (vi != va || vi != vn) {
        printf("  MISMATCH    : interp=%d aot=%d native=%d\n", vi, va, vn);
        return 1;
    }
    printf("\n  aot vs interpreter     : %7.2fx   <- the payoff\n", ti / ta);
    printf("  aot vs native          : %7.2fx   <- cost of the JS value model\n",
           ta / tn);
    printf("  interpreter vs native  : %7.2fx\n", ti / tn);

    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, fnval);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    dlclose(h);
    return 0;
}