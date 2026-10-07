// Asciidoctor.js Live Preview replaces the page body after navigation.
// Restore an inter-document fragment jump after that replacement.
if (location.pathname.endsWith('.adoc') && location.hash.length > 1) {
  const id = decodeURIComponent(location.hash.slice(1))
  document.getElementById(id)?.scrollIntoView({ block: 'start' })
}
