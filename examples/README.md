# examples/
Wired-up sample ports live here (Phase 6). First target: a DirectDraw-2D classic or an OSS reimplementation to prove generality.

## Live pipeline demo (available now)
`runtime/demo/spin_demo.cpp` renders the fixed-function pipeline in a real
browser canvas — a spinning textured quad (MODULATE) beside a lit quad whose
brightness tracks a sweeping directional light. It's a visual companion to the
headless pixel smokes, not a game.

```
node scripts/demo.mjs            # build + serve at http://127.0.0.1:8080
node scripts/demo.mjs --shot out.png   # build + render headlessly to a PNG
```
