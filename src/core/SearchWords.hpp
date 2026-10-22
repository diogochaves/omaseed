#pragma once

// SearchWords — the one way a typed search meets a name (DECISIONS C22, #125).
//
// Release names are written with dots, underscores or dashes between the words
// ("Big.Buck.Bunny.2008.1080p"), people type spaces. Both sides are folded the same way
// before they meet: case folded, and every run of `.`, `_`, `-` and white space made one
// space. A name then matches when every word of the query appears in it, anywhere and in any
// order ("bunny 1080p"); a word is a plain substring, so "1080" finds "1080p".
//
// rank() orders the matches while a query is typed: the whole query as one phrase first, then
// its words in the query's order, then in any order.
//
// The library's search (TorrentFilterModel) and the Files tab's filter (TorrentFilesModel)
// both use it. A hash prefix is matched apart from this, exactly.

#include <QString>
#include <QStringList>

namespace omaseed::core::SearchWords {

/// `text` folded: case folded, each run of `.`, `_`, `-` and white space one space, no
/// space at either end.
QString fold(const QString &text);

/// A query, folded once.
struct Query
{
    QString phrase;    ///< the folded query
    QStringList words; ///< its words, in the query's order

    bool isEmpty() const { return words.isEmpty(); }
};

Query parse(const QString &text);

/// Whether every word of `query` appears in `folded` (a fold()ed text). An empty query
/// matches everything.
bool matches(const Query &query, const QString &folded);

/// How well `folded` (a fold()ed name) matches `query`, best first.
enum Rank {
    Phrase = 0,   ///< the whole query, as one phrase
    InOrder = 1,  ///< every word, in the query's order
    AnyOrder = 2, ///< every word, in another order
    NoMatch = 3,
};

Rank rank(const Query &query, const QString &folded);

} // namespace omaseed::core::SearchWords
