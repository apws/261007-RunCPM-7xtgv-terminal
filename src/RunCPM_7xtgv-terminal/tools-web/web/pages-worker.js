// GitHub Pages cannot supply the isolation headers required by Wasm pthreads.
// No cache or card storage: this worker only adds headers to in-scope responses.
// Do not skipWaiting: an update must never replace/reload a running guest.
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (!url.href.startsWith(self.registration.scope) || url.origin !== self.location.origin) return;
  if (event.request.cache === 'only-if-cached' && event.request.mode !== 'same-origin') return;
  event.respondWith((async () => {
    const response = await fetch(event.request);
    if (response.status === 0) return response;
    const headers = new Headers(response.headers);
    headers.set('Cross-Origin-Opener-Policy', 'same-origin');
    headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
    headers.set('Cross-Origin-Resource-Policy', 'same-origin');
    return new Response(response.body, {
      status: response.status, statusText: response.statusText, headers,
    });
  })());
});
