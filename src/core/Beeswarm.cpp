#include "core/Beeswarm.hpp"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <utility>

namespace omaseed::core::Beeswarm {

namespace {

struct Dot
{
    double x = 0, y = 0, r = 0;
};

using Cell = std::pair<qint64, qint64>;

} // namespace

QList<double>
place(const QList<double> &xs, const QList<double> &rs, double midY, double step, int tries)
{
    const qsizetype n = std::min(xs.size(), rs.size());
    QList<double> ys;
    ys.reserve(n);
    if (n == 0)
        return ys;

    // Two dots can touch only when their centres are at most r1 + r2 + 1 apart, so with cells
    // that wide a dot touches nothing beyond the 3 × 3 cells around its own.
    double biggest = 0;
    for (qsizetype i = 0; i < n; ++i) {
        if (std::isfinite(rs.at(i)))
            biggest = std::max(biggest, rs.at(i));
    }
    const double size = 2 * biggest + 1;
    const auto cellOf = [size](double x, double y) {
        return Cell{qint64(std::floor(x / size)), qint64(std::floor(y / size))};
    };
    QHash<Cell, QList<Dot>> grid;
    grid.reserve(n);

    const auto clear = [&](double x, double y, double r) {
        const Cell c = cellOf(x, y);
        for (qint64 gx = c.first - 1; gx <= c.first + 1; ++gx) {
            for (qint64 gy = c.second - 1; gy <= c.second + 1; ++gy) {
                const auto it = grid.constFind(Cell{gx, gy});
                if (it == grid.cend())
                    continue;
                for (const Dot &p : *it) {
                    // As the ladder had it: anything but "further than r1 + r2 + 1" touches.
                    if (!(std::hypot(p.x - x, p.y - y) > p.r + r + 1))
                        return false;
                }
            }
        }
        return true;
    };

    for (qsizetype i = 0; i < n; ++i) {
        const double x = xs.at(i);
        const double r = rs.at(i);
        double y = midY;
        for (int k = 0; k < tries; ++k) {
            const int out = (k + 1) / 2;
            y = midY + out * step * (k % 2 ? -1 : 1);
            if (clear(x, y, r))
                break;
        }
        ys.append(y);
        if (std::isfinite(x) && std::isfinite(y))
            grid[cellOf(x, y)].append(Dot{x, y, r});
    }
    return ys;
}

} // namespace omaseed::core::Beeswarm
