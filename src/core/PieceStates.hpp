#pragma once

// PieceStates — the General tab's pieces bar (docs/12 G3), out of `torrents/pieceStates`.
//
// The API answers one number per piece: 0 not downloaded, 1 downloading, 2 downloaded. Here
// that list is kept as one byte per piece, and cut into as many buckets as the bar has
// pixels. A bucket is "have" only when every piece in it is had, "downloading" when any of
// its pieces is downloading, and "missing" otherwise, so a region that still lacks pieces
// never reads as finished at a glance.
//
// There is no availability here: the WebUI API has per-piece states, not per-piece peer
// counts.

#include <QByteArray>
#include <QJsonArray>
#include <QList>

namespace omaseed::core::PieceStates {

enum State : quint8 {
    Missing = 0,
    Downloading = 1,
    Have = 2,
};

/// One byte per piece from the API's array. A value the API does not define reads as
/// missing.
QByteArray fromJson(const QJsonArray &array);

/// The bar's cells: at most `buckets` states, one per bucket, left to right. With fewer
/// pieces than buckets every piece is its own cell. Empty for no pieces or no room.
QList<int> bucket(const QByteArray &states, int buckets);

/// How many pieces are in `state`.
int count(const QByteArray &states, State state);

} // namespace omaseed::core::PieceStates
