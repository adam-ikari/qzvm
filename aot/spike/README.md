# AOT spike — feasibility verdict

Question: can a `dlopen`'d shared object, built outside the engine, execute code
that manipulates real QuickJS `JSValue`s?

**Verdict: yes.** All three risks cleared. Host: `host2.c`, AOT library: `aotlib.c`.

```
gcc -fPIC -shared -I . -o libaot.so aotlib.c
clang -O2 -I . host2.c <libqjs.a> -lm -lpthread -ldl -rdynamic -o host2
./host2
# [aot] constructor ran, registered 'len'
# OK   dlopen + constructor executed
# OK   registry lookup found 'len'
# OK   native executed in runtime: len(5) => 105 (want 105)
```

## Risk 1 — symbol resolution: needs a build change

Initially **failed**: `dlsym(RTLD_DEFAULT, "JS_NewRuntime")` returned nil even with
`-rdynamic`.

Two independent causes, both must be fixed:

1. **`quickjs.h:71-75`** — `JS_EXTERN` expands to `visibility("default")` only when
   `BUILDING_QJS_SHARED` is defined. In a static build it expands to nothing.
2. **`CMakeLists.txt:8`** — `set(CMAKE_C_VISIBILITY_PRESET hidden)` marks everything
   else hidden.

Together these put every `JS_*` symbol in `.symtab` as **local** (`t`), so no
link-time remedy works: a version script and `--export-dynamic` both fail, because
hidden visibility is baked in at compile time.

Fix (compile time + link time):

```
-DBUILDING_QJS_SHARED -fvisibility=default   # when compiling libqjs
-rdynamic                                    # when linking the host
```

After that 10/12 probed symbols resolve. The three that don't — `JS_NewInt32`,
`JS_NewString`, `JS_ToCString` — are `static inline` in `quickjs.h`, so they have
no out-of-line symbol to export. AOT code can inline them itself; no obstacle.

## Risk 2 — native code reaching runtime state: no interpreter internals needed

The original design assumed AOT functions would operate the interpreter's `sp`,
`var_refs`, and `local_buf`. **Not necessary.** A plain

```c
JSValue f(JSContext *ctx, JSValue *args, int argc)
```

receives already-evaluated arguments and can allocate its own locals as ordinary C
variables. `JSValue` is a plain struct declared in `quickjs.h`, so it crosses the
`.so` boundary with no marshalling.

This is the finding that makes the perry-style approach tractable: generated code
is ordinary C, not a hand-rolled interpreter reimplementation.

## Risk 3 — registration and lookup: works as designed

`__attribute__((constructor))` runs at `dlopen`; the library exposes a lookup
symbol the host resolves with `dlsym`. No engine cooperation needed.

## What this does not yet prove

The spike hard-codes one hand-written function. Still open: how AOT functions get
dispatched from inside `JS_CallInternal` instead of being called directly by the
host, and how bytecode maps onto the generated C (the actual translation).
