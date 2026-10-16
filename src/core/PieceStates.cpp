#include "core/PieceStates.hpp"

#include <algorithm>

namespace omaseed::core::PieceStates {

QList<int> bucket(const QByteArray &states, int buckets)
{
    const qsizetype pieces = states.size();
    if (pieces == 0 || buckets <= 0)
        return {};

    const qsizetype cells = std::min<qsizetype>(pieces, buckets);
    QList<int> result;
    result.reserve(cells);
    for (qsizetype cell = 0; cell < cells; ++cell) {
        // Piece ranges [begin, end) spread as evenly as integers allow; every range holds at
        // least one piece because cells <= pieces.
        const qsizetype begin = cell * pieces / cells;
        const qsizetype end = (cell + 1) * pieces / cells;
        bool all = true;
        bool any = false;
        for (qsizetype i = begin; i < end; ++i) {
            const char state = states.at(i);
            if (state != char(Have))
                all = false;
            if (state == char(Downloading)) {
                any = true;
                break;
            }
        }
        result.append(any ? Downloading : all ? Have : Missing);
    }
    return result;
}

int count(const QByteArray &states, State state)
{
    return int(std::count(states.cbegin(), states.cend(), char(state)));
}

} // namespace omaseed::core::PieceStates
