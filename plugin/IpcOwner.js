.pragma library

// Who answers `omarchy-shell chaves.omaseed …` (open, close, toggle, altSpeed, settings).
//
// Quickshell keeps one IpcHandler per target. One registered while another holds the target
// is ignored for good, and when the holder goes the target goes with it ("Target not found").
// The widget can have several instances: one per monitor's bar, and a container widget (the
// console's rail) hosts its own, which the shell's bar does not list. Asking the bar who comes
// first left a hosted instance without the target, and an instance made before the old one
// went never got it back. So every instance queues here, in the one copy of this library the
// shell's engine shares, and the first live one owns the target. When it leaves, the next one
// is told, and takes the target once the old handler is gone.

var queue = []

function alive(widget) {
  try {
    return widget !== null && widget !== undefined && widget.moduleName !== undefined
  } catch (e) {
    return false
  }
}

function elect() {
  queue = queue.filter(alive)
  for (var i = 0; i < queue.length; i++) queue[i].ipcElected(i === 0)
}

function join(widget) {
  if (queue.indexOf(widget) === -1) queue.push(widget)
  elect()
}

function leave(widget) {
  var i = queue.indexOf(widget)
  if (i !== -1) queue.splice(i, 1)
  elect()
}

function first() {
  queue = queue.filter(alive)
  return queue.length > 0 ? queue[0] : null
}
