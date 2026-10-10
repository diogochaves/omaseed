#pragma once

// Beeswarm — where the ratio ladder's dots sit (Kit/RatioLadder.qml, docs/22 `renderLadder`).
//
// Dots come in drawing order, each with its x and radius; each is given the first height,
// from the middle line out in `step`s, up first on screen (0, −1, +1, −2, +2, … steps), at
// which it keeps more than 1 unit clear of every dot placed before it. After `tries` heights
// it takes the last one tried, overlapping or not.
//
// The ladder used to test each height against every dot placed so far, which grows with the
// square of the library: 1.7 s for 3,325 dots, every two seconds (#45). Placed dots are kept
// in a grid of cells as wide as the two biggest dots can reach, so a height is tested only
// against the dots in its own cell and the eight around it. The heights are the same.

#include <QList>

namespace omaseed::core::Beeswarm {

/// The height of each dot, in the order given. `xs` and `rs` are the same length (the
/// shorter one counts); a radius is at least 0.
QList<double>
place(const QList<double> &xs, const QList<double> &rs, double midY, double step, int tries = 40);

} // namespace omaseed::core::Beeswarm
