// SPDX-License-Identifier: GPL-3.0-only
// Cross-origin isolation via service worker, for static hosts that can't set
// COOP/COEP response headers themselves. Injects those headers so the page
// becomes crossOriginIsolated (required for SharedArrayBuffer / wasm threads).
// If the server already sends the headers (e.g. tools/serve-https.py), this is
// a harmless no-op. Same idea as gzuidhof/coi-serviceworker (MIT).
//
// Load from the page with:
//   <script src="coi-serviceworker.js"></script>
// It registers itself and reloads once so the SW controls the page.

if (typeof window === 'undefined') {
  // ── service-worker context ──
  self.addEventListener('install', () => self.skipWaiting());
  self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));

  self.addEventListener('fetch', (event) => {
    const req = event.request;
    if (req.cache === 'only-if-cached' && req.mode !== 'same-origin') return;
    event.respondWith((async () => {
      const res = await fetch(req);
      if (res.status === 0) return res; // opaque; leave as-is
      const headers = new Headers(res.headers);
      headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
      headers.set('Cross-Origin-Opener-Policy', 'same-origin');
      return new Response(res.body, { status: res.status, statusText: res.statusText, headers });
    })());
  });
} else {
  // ── page context: register + reload once ──
  (async () => {
    if (window.crossOriginIsolated) return;           // already isolated
    if (!window.isSecureContext) return;              // needs https or localhost
    const reg = await navigator.serviceWorker?.register(window.document.currentScript.src)
      .catch(() => null);
    if (reg && !navigator.serviceWorker.controller) {
      // Reload so the freshly-installed SW controls this page.
      window.sessionStorage.setItem('coiReloaded', '1');
      if (!window.sessionStorage.getItem('coiReloadedTwice')) {
        window.sessionStorage.setItem('coiReloadedTwice', '1');
        window.location.reload();
      }
    }
  })();
}
