// Complete isolation setup BEFORE loading Wasm or creating a volatile card.
const status = document.querySelector('#status');
const notice = document.querySelector('#notice');
const retryKey = `xtgv-pages-isolation:${new URL('./', import.meta.url).pathname}`;

async function boot() {
  if (!window.isSecureContext || !('serviceWorker' in navigator)) {
    throw Error('This host needs HTTPS and service workers. Use a normal browser tab, or the local tools-web launcher.');
  }
  // Register on returning visits as well, but never reload for a worker update.
  await navigator.serviceWorker.register(new URL('pages-worker.js', import.meta.url), {
    scope: './', updateViaCache: 'none',
  });
  if (!window.crossOriginIsolated) {
    status.textContent = 'Preparing browser worker isolation…';
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => { cleanup(); reject(Error('Service worker startup timed out.')); }, 15000);
      const check = () => { if (navigator.serviceWorker.controller) { cleanup(); resolve(); } };
      const cleanup = () => { clearTimeout(timer); navigator.serviceWorker.removeEventListener('controllerchange', check); };
      navigator.serviceWorker.addEventListener('controllerchange', check);
      check();
    });
    if (sessionStorage.getItem(retryKey)) {
      throw Error('Worker isolation is unavailable in this browser. No emulator was started. Try the local tools-web launcher.');
    }
    sessionStorage.setItem(retryKey, '1');
    location.reload(); // First visit only; no guest or user card exists yet.
    return;
  }
  sessionStorage.removeItem(retryKey);
  await new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = new URL('xtgv_web.js', import.meta.url).href;
    script.onload = resolve;
    script.onerror = () => reject(Error('Cannot load the browser runtime.'));
    document.head.append(script);
  });
  await import('./standalone.js');
}

boot().catch(error => {
  status.textContent = 'Browser startup unavailable';
  notice.textContent = error.message;
  console.error(error);
});
