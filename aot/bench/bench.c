/* AOT vs interpreter, same process and same JSRuntime — the only difference is
 * which implementation of fib() runs, so startup cost cancels out of the
 * comparison. Both sides are entered through the same JSValue-based ABI:
 * the interpreter through JS_Call, the AOT function through its C signature.
 *
 * usage: bench [n] [reps] */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "quickjs.h"

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

/* time `reps` calls, return median ms; result goes to *out */
static double time_calls(JSValue (*call)(void *, JSContext *, JSValue *, int),
                         void *ud, JSContext *ctx, JSValue *arg, int reps,
                         int warm, int32_t *out)
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
    JSValue *fnval = ud;
    JSValue nargs[1];
    nargs[0] = *arg;
    (void)argc;
    return JS_Call(ctx, *fnval, JS_UNDEFINED, 1, nargs);
}

static JSValue call_aot(void *ud, JSContext *ctx, JSValue *arg, int argc)
{
    return ((fib_fn)ud)(ctx, arg, argc);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 27;
    int reps = argc > 2 ? atoi(argv[2]) : 11;
    int warm = 3;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue global, fnval, arg;
    int32_t vi = 0, va = 0;
    double ti, ta;
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
    printf("  interpreter : %9.3f ms   result=%d\n", ti, vi);
    printf("  aot         : %9.3f ms   result=%d\n", ta, va);
    if (vi != va) {
        printf("  MISMATCH    : %d vs %d\n", vi, va);
        return 1;
    }
    printf("  speedup     : %8.2fx  (%s)\n", ti / ta,
           ta < ti ? "aot faster" : "AOT SLOWER");

    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, fnval);
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    dlclose(h);
    return 0;
}
