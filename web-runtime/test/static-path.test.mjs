// SPDX-License-Identifier: GPL-3.0-only
// The ad-hoc static servers (scripts/*.mjs, web-runtime/test/*.browser.test.mjs) map a request URL
// onto a build directory. `path.join(root, url)` normalises ".." but does not confine the result:
// GET /../../.git/config served the repo's own git config (verified 2026-09-03). resolveUnder()
// confines, and answers null for anything that escapes the root or cannot be decoded -- a 404, not
// an uncaught URIError that takes the server down.
import assert from 'node:assert/strict';
import { sep, join } from 'node:path';
import { resolveUnder } from '../../scripts/lib/static-path.mjs';

const root = join(sep, 'srv', 'build');
assert.equal(resolveUnder(root, '/'), join(root, 'index.html'), 'the root maps to index.html by default');
assert.equal(resolveUnder(root, '/', 'spin_demo.html'), join(root, 'spin_demo.html'), 'a caller can name the root document');
assert.equal(resolveUnder(root, '/a/b.js'), join(root, 'a', 'b.js'));
assert.equal(resolveUnder(root, '/a/b.js?x=1'), join(root, 'a', 'b.js'), 'the query string is dropped');
assert.equal(resolveUnder(root, '/sp%20ace.wasm'), join(root, 'sp ace.wasm'), 'percent-encoding is decoded');
assert.equal(resolveUnder(root, '//etc/passwd'), join(root, 'etc', 'passwd'), 'a doubled slash stays under the root');
assert.equal(resolveUnder(root, '/../../.git/config'), null, 'dot-dot cannot leave the root');
assert.equal(resolveUnder(root, '/a/../../x'), null, 'dot-dot inside a path cannot leave the root');
assert.equal(resolveUnder(root, '/%2e%2e/%2e%2e/etc/passwd'), null, 'encoded dot-dot cannot leave the root');
assert.equal(resolveUnder(root, '/%zz'), null, 'a malformed escape is a 404, not an uncaught URIError');
assert.equal(resolveUnder(root, '/.'), null, 'the root directory itself is not a file');
console.log('ok — static-path confines every request to its root');
