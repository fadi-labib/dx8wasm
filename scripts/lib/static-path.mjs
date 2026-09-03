// SPDX-License-Identifier: GPL-3.0-only
// Map a request URL onto a file under `root`, or null. Shared by every ad-hoc static server in this
// repo (scripts/*.mjs, web-runtime/test/*.browser.test.mjs). `path.join(root, url)` normalises
// ".." without confining the result, so GET /../../.git/config used to serve the repo's git config;
// and decodeURIComponent throws on a malformed escape, which took the whole server down. Both are a
// 404 here. Tested by web-runtime/test/static-path.test.mjs.
import { resolve, sep } from 'node:path';

export function resolveUnder(root, url, indexFile = 'index.html') {
  let p;
  try { p = decodeURIComponent(String(url).split('?')[0]); } catch { return null; }
  if (p === '' || p === '/') p = '/' + indexFile;
  const base = resolve(root);
  const file = resolve(base, '.' + p);   // "." + "/a/b" keeps it relative to base; ".." is then judged below
  return file.startsWith(base + sep) ? file : null;
}
