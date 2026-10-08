// Load after rendering in Asciidoctor.js Live Preview.
// Keep source titles portable; split only this project's displayed guide title.
if (location.pathname.endsWith('.adoc')) {
  const title = document.querySelector('#header > h1, #content > h1:first-child')
  const prefix = 'RunCPM 7xtgv-terminal: '
  if (title?.textContent.startsWith(prefix)) {
    const guide = title.textContent.slice(prefix.length).trim()
    if (guide) {
      title.replaceChildren(
        document.createTextNode('RunCPM 7xtgv-terminal'),
        document.createElement('br'),
        document.createTextNode(guide)
      )
    }
  }

  // Restore fragment position after rendering AND the title-height change.
  if (location.hash.length > 1) {
    const id = decodeURIComponent(location.hash.slice(1))
    document.getElementById(id)?.scrollIntoView({ block: 'start' })
  }
}
