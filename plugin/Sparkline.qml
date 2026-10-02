import QtQuick
import qs.Commons

// The last hour of upload (contract §3 Now): 60 one-minute averages the app samples,
// oldest first, as a line over a faint fill, scaled to the hour's peak.
Canvas {
  id: root

  property var points: []
  property color color: Color.accent

  onPointsChanged: requestPaint()
  onColorChanged: requestPaint()
  onWidthChanged: requestPaint()
  onHeightChanged: requestPaint()

  onPaint: {
    var ctx = getContext("2d")
    ctx.reset()
    var values = Array.isArray(points) ? points : []
    if (values.length < 2 || width <= 0 || height <= 0) return
    var peak = 0
    for (var i = 0; i < values.length; i++) peak = Math.max(peak, Number(values[i]) || 0)
    if (peak <= 0) return
    var step = width / (values.length - 1)
    var line = Math.max(1, Style.space(1.5))
    var top = line
    var span = height - line - top
    function yAt(v) { return top + span - span * Math.max(0, Number(v) || 0) / peak }

    ctx.beginPath()
    ctx.moveTo(0, height)
    for (var j = 0; j < values.length; j++) ctx.lineTo(j * step, yAt(values[j]))
    ctx.lineTo(width, height)
    ctx.closePath()
    ctx.fillStyle = Qt.rgba(color.r, color.g, color.b, 0.18)
    ctx.fill()

    ctx.beginPath()
    for (var k = 0; k < values.length; k++) {
      if (k === 0) ctx.moveTo(0, yAt(values[0]))
      else ctx.lineTo(k * step, yAt(values[k]))
    }
    ctx.lineWidth = line
    ctx.strokeStyle = color
    ctx.stroke()
  }
}
