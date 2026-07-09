// SPDX-License-Identifier: GPL-3.0-only
// Client-side asset onboarding. The user points at their OWN game install; we
// copy the needed data into OPFS on THEIR machine. Copyrighted game assets never
// touch a server — we host only the (GPL) engine. This is what makes an
// own-the-assets port both legal to ship and playable by a stranger.
//
// A "profile" describes one game: how to recognize its install and which files
// to import. See GENERALS below. The picker (File System Access API) is
// Chromium/Edge only; that's fine — multi-GB folders can't go through the
// in-memory <input webkitdirectory> fallback anyway.

export function pickerSupported() {
  return typeof window !== 'undefined' && typeof window.showDirectoryPicker === 'function';
}

// Prompt the user to choose their install folder. Requires a user gesture.
export async function pickGameFolder() {
  if (!pickerSupported()) {
    throw new Error('This browser cannot open a folder picker — use desktop Chrome or Edge.');
  }
  return window.showDirectoryPicker({ id: 'dx8wasm-game', mode: 'read' });
}

// Recursively walk a FileSystemDirectoryHandle → { path, name, handle }.
async function* walk(dir, prefix = '') {
  for await (const [name, h] of dir.entries()) {
    const path = prefix ? `${prefix}/${name}` : name;
    if (h.kind === 'directory') yield* walk(h, path);
    else yield { path, name, handle: h };
  }
}

async function opfsSubdir(root, relDir) {
  let d = root;
  for (const p of relDir.split('/').filter(Boolean)) d = await d.getDirectoryHandle(p, { create: true });
  return d;
}

// Ask the browser not to evict a multi-GB import. Best-effort.
export async function requestPersistence() {
  try { return await navigator.storage?.persist?.() ?? false; } catch { return false; }
}

// Is this game already imported and complete?
export async function isImported(profile) {
  try {
    const root = await navigator.storage.getDirectory();
    const d = await root.getDirectoryHandle(profile.opfsRoot);
    await d.getFileHandle(COMPLETE);
    return true;
  } catch { return false; }
}

const COMPLETE = '.dx8wasm-complete';

// Confirm a picked folder looks like the expected game before a long import.
export async function validateInstall(dirHandle, profile) {
  return profile.validate(dirHandle);
}

// Copy the profile's files from the picked folder into OPFS, with progress.
// onProgress: { file, filesDone, filesTotal, bytesDone, bytesTotal }
export async function importInstall(dirHandle, profile, { onProgress } = {}) {
  await requestPersistence();
  const root = await navigator.storage.getDirectory();
  await root.removeEntry(profile.opfsRoot, { recursive: true }).catch(() => {}); // drop stale/partial
  const target = await root.getDirectoryHandle(profile.opfsRoot, { create: true });

  // Enumerate first so we can show real byte progress.
  const wanted = [];
  for await (const f of walk(dirHandle)) {
    if (profile.include(f.path, f.name)) wanted.push(f);
  }
  if (!wanted.length) throw new Error('No game files matched — is this the right folder?');

  const files = [];
  let bytesTotal = 0;
  for (const f of wanted) {
    const file = await f.handle.getFile();
    files.push({ ...f, file });
    bytesTotal += file.size;
  }

  let bytesDone = 0;
  for (let i = 0; i < files.length; i++) {
    const { path, name, file } = files[i];
    const dir = path.includes('/') ? await opfsSubdir(target, path.slice(0, path.lastIndexOf('/'))) : target;
    const fh = await dir.getFileHandle(name, { create: true });
    const w = await fh.createWritable();
    await w.write(file);          // streams from disk; no full in-memory copy
    await w.close();
    bytesDone += file.size;
    onProgress?.({ file: path, filesDone: i + 1, filesTotal: files.length, bytesDone, bytesTotal });
  }

  const marker = await (await target.getFileHandle(COMPLETE, { create: true })).createWritable();
  await marker.write(JSON.stringify({ game: profile.id, files: files.length, bytes: bytesTotal }));
  await marker.close();
  return { files: files.length, bytes: bytesTotal, dir: target };
}

// ── game profiles ─────────────────────────────────────────────────────────────

// C&C Generals: Zero Hour. Data lives in .big archives plus loose Data/ and
// Maps/ trees; fonts (.ttf) are staged alongside. Recognize an install by the
// presence of at least one .big at the top level.
export const GENERALS = {
  id: 'generals-zh',
  name: 'C&C Generals: Zero Hour',
  opfsRoot: 'game-generals-zh',
  async validate(dir) {
    for await (const [name, h] of dir.entries()) {
      if (h.kind === 'file' && name.toLowerCase().endsWith('.big')) return { ok: true };
    }
    return { ok: false, reason: 'No .big archives at the top level — pick the folder containing the game .big files.' };
  },
  include(path, name) {
    const n = name.toLowerCase();
    return n.endsWith('.big') || n.endsWith('.ttf') || /(^|\/)(data|maps)\//i.test(path);
  },
};
