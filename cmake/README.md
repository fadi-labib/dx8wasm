# cmake/

Emscripten toolchain preset + template CMakeLists a game plugs into.

## Known toolchain gotcha — pin Emscripten

Emscripten **6.0.2**'s `wasm-opt` crashes on the DWARF emitted with `-g`:

```
Assertion failed: !endMap.contains(span.end)
```

Workaround: re-link with `-g0` (drop DWARF). Pin the Emscripten version in the
preset so this doesn't resurface as version drift. (SPEC §14 Phase 4.)
