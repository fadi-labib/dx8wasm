// SPDX-License-Identifier: GPL-3.0-only
// A repeatable framebuffer digest for determinism checking. FNV-1a over the read-back bytes,
// chained across passes so a whole sequence of sub-scenes folds into one 32-bit value. Lives in
// runtime/test/ on purpose: this is a harness tool, not part of the SDK's public contract.
#ifndef DX8WASM_FRAME_DIGEST_H
#define DX8WASM_FRAME_DIGEST_H
#include <GLES3/gl3.h>
#include <cstdint>
#include <vector>

namespace digest {
constexpr uint32_t kSeed = 0x811c9dc5u;   // FNV-1a offset basis

// Reads back w*h RGBA pixels and folds them into `seed`. Chain the return value into the next
// call to digest a whole sequence. Reads the full rect (not sampled points) so a difference in
// any pixel changes the result — sampling would let a real divergence hide between the samples.
inline uint32_t fnv1a_framebuffer(uint32_t seed, int w, int h) {
  std::vector<unsigned char> px((size_t)w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (unsigned char b : px) { seed ^= b; seed *= 16777619u; }
  return seed;
}
}   // namespace digest
#endif
