// Load after rendering in Asciidoctor.js Live Preview.
// Keep source headings portable. Split the first prose ": " at any rendered
// heading level, independent of the document's :leveloffset: setting.
if (location.pathname.endsWith('.adoc')) {
  const headings = document.querySelectorAll(
    '#header :is(h1,h2,h3,h4,h5,h6), #content :is(h1,h2,h3,h4,h5,h6)'
  )
  for (const heading of headings) {
    if (heading.dataset.mixworxColonBreak || heading.querySelector('br')) continue

    const walker = document.createTreeWalker(heading, NodeFilter.SHOW_TEXT)
    const nodes = []
    let text = ''
    let node
    while ((node = walker.nextNode())) {
      nodes.push({ node, start: text.length })
      text += node.data
    }
    for (const separator of text.matchAll(/:\s+(?=\S)/g)) {
      const start = nodes.findLast(entry => entry.start <= separator.index)
      if (start.node.parentElement.closest('code, kbd, samp, .anchor')) continue
      const endOffset = separator.index + separator[0].length
      const end = nodes.findLast(entry => entry.start < endOffset)

      // A Range changes only the separator, retaining inline emphasis, links,
      // heading IDs and anchor controls, even across inline markup boundaries.
      const range = document.createRange()
      range.setStart(start.node, separator.index - start.start)
      range.setEnd(end.node, endOffset - end.start)
      range.deleteContents()
      range.insertNode(document.createElement('br'))
      heading.dataset.mixworxColonBreak = 'true'
      break
    }
  }

  // Restore fragment position after rendering AND all heading-height changes.
  if (location.hash.length > 1) {
    const id = decodeURIComponent(location.hash.slice(1))
    document.getElementById(id)?.scrollIntoView({ block: 'start' })
  }
}
