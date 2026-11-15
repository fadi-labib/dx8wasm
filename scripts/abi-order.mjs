// SPDX-License-Identifier: GPL-3.0-only
// The vtable order of every D3D8 interface in runtime/d3d8/d3d8.h, checked against D3D8's own.
//
// An engine compiled against the real d3d8.h (GeneralsX uses DXVK's) calls methods by vtable SLOT,
// so a declaration out of order in our header silently routes a call to a different method. That
// happened: SetPixelShader / GetPixelShader / CreatePixelShader were declared in that order instead
// of D3D8's Create / Set / Get. CreatePixelShader then landed on SetPixelShader (a different wasm
// type, so "function signature mismatch" at boot once the engine first called it), and
// SetPixelShader(h) landed on GetPixelShader(DWORD*) -- same wasm type, so it ran silently and wrote
// through h as a pointer. abi_smoke cannot see this: it compiles the caller against OUR header.
//
// CANON is each interface's own methods (base-class methods excluded) in the order of D3D8's public
// COM interfaces -- an API fact, names and order only, no code. It was read off a real d3d8.h (DXVK
// 2.6's, which follows Microsoft's) and is the order any game binary was compiled against.
// Usage: node scripts/abi-order.mjs
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const CANON = {
 "IDirect3D8": [
  "RegisterSoftwareDevice",
  "GetAdapterCount",
  "GetAdapterIdentifier",
  "GetAdapterModeCount",
  "EnumAdapterModes",
  "GetAdapterDisplayMode",
  "CheckDeviceType",
  "CheckDeviceFormat",
  "CheckDeviceMultiSampleType",
  "CheckDepthStencilMatch",
  "GetDeviceCaps",
  "GetAdapterMonitor",
  "CreateDevice"
 ],
 "IDirect3DBaseTexture8": [
  "SetLOD",
  "GetLOD",
  "GetLevelCount"
 ],
 "IDirect3DCubeTexture8": [
  "GetLevelDesc",
  "GetCubeMapSurface",
  "LockRect",
  "UnlockRect",
  "AddDirtyRect"
 ],
 "IDirect3DDevice8": [
  "TestCooperativeLevel",
  "GetAvailableTextureMem",
  "ResourceManagerDiscardBytes",
  "GetDirect3D",
  "GetDeviceCaps",
  "GetDisplayMode",
  "GetCreationParameters",
  "SetCursorProperties",
  "SetCursorPosition",
  "ShowCursor",
  "CreateAdditionalSwapChain",
  "Reset",
  "Present",
  "GetBackBuffer",
  "GetRasterStatus",
  "SetGammaRamp",
  "GetGammaRamp",
  "CreateTexture",
  "CreateVolumeTexture",
  "CreateCubeTexture",
  "CreateVertexBuffer",
  "CreateIndexBuffer",
  "CreateRenderTarget",
  "CreateDepthStencilSurface",
  "CreateImageSurface",
  "CopyRects",
  "UpdateTexture",
  "GetFrontBuffer",
  "SetRenderTarget",
  "GetRenderTarget",
  "GetDepthStencilSurface",
  "BeginScene",
  "EndScene",
  "Clear",
  "SetTransform",
  "GetTransform",
  "MultiplyTransform",
  "SetViewport",
  "GetViewport",
  "SetMaterial",
  "GetMaterial",
  "SetLight",
  "GetLight",
  "LightEnable",
  "GetLightEnable",
  "SetClipPlane",
  "GetClipPlane",
  "SetRenderState",
  "GetRenderState",
  "BeginStateBlock",
  "EndStateBlock",
  "ApplyStateBlock",
  "CaptureStateBlock",
  "DeleteStateBlock",
  "CreateStateBlock",
  "SetClipStatus",
  "GetClipStatus",
  "GetTexture",
  "SetTexture",
  "GetTextureStageState",
  "SetTextureStageState",
  "ValidateDevice",
  "GetInfo",
  "SetPaletteEntries",
  "GetPaletteEntries",
  "SetCurrentTexturePalette",
  "GetCurrentTexturePalette",
  "DrawPrimitive",
  "DrawIndexedPrimitive",
  "DrawPrimitiveUP",
  "DrawIndexedPrimitiveUP",
  "ProcessVertices",
  "CreateVertexShader",
  "SetVertexShader",
  "GetVertexShader",
  "DeleteVertexShader",
  "SetVertexShaderConstant",
  "GetVertexShaderConstant",
  "GetVertexShaderDeclaration",
  "GetVertexShaderFunction",
  "SetStreamSource",
  "GetStreamSource",
  "SetIndices",
  "GetIndices",
  "CreatePixelShader",
  "SetPixelShader",
  "GetPixelShader",
  "DeletePixelShader",
  "SetPixelShaderConstant",
  "GetPixelShaderConstant",
  "GetPixelShaderFunction",
  "DrawRectPatch",
  "DrawTriPatch",
  "DeletePatch"
 ],
 "IDirect3DIndexBuffer8": [
  "Lock",
  "Unlock",
  "GetDesc"
 ],
 "IDirect3DResource8": [
  "GetDevice",
  "SetPrivateData",
  "GetPrivateData",
  "FreePrivateData",
  "SetPriority",
  "GetPriority",
  "PreLoad",
  "GetType"
 ],
 "IDirect3DSurface8": [
  "GetDevice",
  "SetPrivateData",
  "GetPrivateData",
  "FreePrivateData",
  "GetContainer",
  "GetDesc",
  "LockRect",
  "UnlockRect"
 ],
 "IDirect3DSwapChain8": [
  "Present",
  "GetBackBuffer"
 ],
 "IDirect3DTexture8": [
  "GetLevelDesc",
  "GetSurfaceLevel",
  "LockRect",
  "UnlockRect",
  "AddDirtyRect"
 ],
 "IDirect3DVertexBuffer8": [
  "Lock",
  "Unlock",
  "GetDesc"
 ],
 "IDirect3DVolume8": [
  "GetDevice",
  "SetPrivateData",
  "GetPrivateData",
  "FreePrivateData",
  "GetContainer",
  "GetDesc",
  "LockBox",
  "UnlockBox"
 ],
 "IDirect3DVolumeTexture8": [
  "GetLevelDesc",
  "GetVolumeLevel",
  "LockBox",
  "UnlockBox",
  "AddDirtyBox"
 ]
};

const repo = join(dirname(fileURLToPath(import.meta.url)), '..');
const src = readFileSync(join(repo, 'runtime', 'd3d8', 'd3d8.h'), 'utf8');
const IUNKNOWN = new Set(['QueryInterface', 'AddRef', 'Release']);

// struct/class IDirect3DX8 : [public] Base { ... };  ->  { base, methods[] }
const ours = {};
for (const m of src.matchAll(/(?:struct|class)\s+(IDirect3D\w*8)\s*:\s*(?:public\s+)?(\w+)\s*\{/g)) {
  const start = m.index + m[0].length;
  const end = src.indexOf('\n};', start);
  const body = src.slice(start, end < 0 ? undefined : end);
  ours[m[1]] = { base: m[2], methods: [...body.matchAll(/virtual\s+[\w\s*&]+?\b(\w+)\s*\(/g)].map((x) => x[1]) };
}
const inherited = (base) => {
  const acc = new Set(IUNKNOWN);
  for (let b = base; ours[b]; b = ours[b].base) for (const x of ours[b].methods) acc.add(x);
  return acc;
};

let failures = 0, checked = 0;
for (const [name, canon] of Object.entries(CANON)) {
  if (!ours[name]) continue;            // an interface we do not declare (cube/volume textures, swap chains)
  checked++;
  const own = ours[name].methods.filter((x) => !inherited(ours[name].base).has(x));
  const n = Math.max(own.length, canon.length);
  const bad = [];
  for (let i = 0; i < n; i++) if (own[i] !== canon[i]) bad.push(`slot ${i}: ours ${own[i] ?? '(none)'}, D3D8 ${canon[i] ?? '(none)'}`);
  if (bad.length) { failures++; console.log(`FAIL ${name}\n  ` + bad.join('\n  ')); }
  else console.log(`ok   ${name} (${own.length} methods in D3D8 order)`);
}
if (checked < 8) { console.log(`FAIL parsed only ${checked} interfaces -- the parser no longer matches d3d8.h`); failures++; }
console.log(failures ? `FAIL: abi-order -- ${failures} interface(s) out of D3D8 order` : `PASS: abi-order -- ${checked} interfaces in D3D8 vtable order`);
process.exit(failures ? 1 : 0);
