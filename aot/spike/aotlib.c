/* AOT lib: registers a native impl via constructor. Signature must use only
   exported JS_* types (JSContext/JSValue are plain structs in quickjs.h). */
#include <stdio.h>
#include "quickjs.h"

typedef JSValue (*qz_aot_fn)(JSContext *ctx, JSValue *args, int argc);

/* simple registry so host can find us after dlopen */
qz_aot_fn qz_aot_lookup(const char *name);

static qz_aot_fn g_add;
static qz_aot_fn g_len;

static JSValue aot_len(JSContext *ctx, JSValue *args, int argc) {
    /* prove: we can read JSValue args produced by the real runtime */
    int32_t n = 0;
    JS_ToInt32(ctx, &n, args[0]);
    return JS_NewInt32(ctx, n + 100);
}

__attribute__((constructor))
static void qz_aot_register(void) {
    g_len = aot_len;
    fprintf(stderr, "[aot] constructor ran, registered 'len'\n");
}

qz_aot_fn qz_aot_lookup(const char *name) {
    if (name[0]=='l' && name[1]=='e' && name[2]=='n' && !name[3]) return g_len;
    return 0;
}
