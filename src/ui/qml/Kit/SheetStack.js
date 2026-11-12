.pragma library

// The open sheets, oldest first, across every window (Kit/Sheet's `open()` and `close()`).
// A closing sheet asks it whether a sheet opened over it is still open, rather than asking the
// window which item holds the keyboard: an inactive window holds none (`activeFocusItem` is
// null until it is active again), so that question cannot tell "nothing holds it" from "the
// window is in the background" (#192).

const _open = [];

/// `sheet` is open, on top of the ones already open.
function push(sheet) {
    remove(sheet);
    _open.push(sheet);
}

/// `sheet` is closed (or gone).
function remove(sheet) {
    const at = _open.indexOf(sheet);
    if (at >= 0)
        _open.splice(at, 1);
}

/// The sheet opened right after `sheet`, in the same window, that is still open; null when
/// there is none. Call it before `remove(sheet)`.
function above(sheet, window) {
    const at = _open.indexOf(sheet);
    if (at < 0)
        return null;
    for (let i = at + 1; i < _open.length; ++i) {
        if (_open[i]._window === window)
            return _open[i];
    }
    return null;
}
