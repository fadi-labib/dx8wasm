# cmake/

Toolchain notes for the Emscripten build. The configure preset itself lives in the
root `CMakePresets.json` (`cmake --preset emscripten`); a game consumes the SDK by
adding this repo with `add_subdirectory(...)` and linking `dx8_d3d8webgl` — see
`docs/INTEGRATION.md` §1 for the link flags.

## Known toolchain gotcha — pin Emscripten

Emscripten **6.0.2**'s `wasm-opt` crashes on the DWARF emitted with `-g`:

```
Assertion failed: !endMap.contains(span.end)
```

Workaround: re-link with `-g0` (drop DWARF). The Emscripten version is pinned in
`.emscripten-version` and checked by `scripts/ci.sh` so this doesn't resurface as
version drift.
