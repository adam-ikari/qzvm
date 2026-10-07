#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "quickjs.h"

typedef JSValue (*qz_aot_fn)(JSContext *, JSValue *, int);
typedef qz_aot_fn (*lookup_fn)(const char *);

int main(void) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    void *h = dlopen("./libaot.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL dlopen: %s\n", dlerror()); return 1; }
    printf("OK   dlopen + constructor executed\n");

    lookup_fn look = (lookup_fn)dlsym(h, "qz_aot_lookup");
    if (!look) { printf("FAIL dlsym lookup\n"); return 1; }
    qz_aot_fn f = look("len");
    if (!f) { printf("FAIL registry lookup\n"); return 1; }
    printf("OK   registry lookup found 'len'\n");

    /* risk #2: native fn reading a JSValue produced by the real runtime */
    JSValue args[1]; args[0] = JS_NewInt32(ctx, 5);
    JSValue r = f(ctx, args, 1);
    int32_t out = -1; JS_ToInt32(ctx, &out, r);
    printf("%s   native executed in runtime: len(5) => %d (want 105)\n",
           out == 105 ? "OK  " : "FAIL", out);

    /* cross-ABI check: value survives the .so boundary both ways */
    JS_FreeValue(ctx, r); JS_FreeValue(ctx, args[0]);
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    dlclose(h);
    return out == 105 ? 0 : 1;
}
