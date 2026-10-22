#include "core/SearchWords.hpp"

#include <algorithm>

namespace omaseed::core::SearchWords {

namespace {

bool isSeparator(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('-') || c.isSpace();
}

} // namespace

QString fold(const QString &text)
{
    const QString lower = text.toCaseFolded();
    QString out;
    out.reserve(lower.size());
    bool pendingSpace = false;
    for (const QChar c : lower) {
        if (isSeparator(c)) {
            pendingSpace = !out.isEmpty();
            continue;
        }
        if (pendingSpace) {
            out.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        out.append(c);
    }
    return out;
}

Query parse(const QString &text)
{
    Query query;
    query.phrase = fold(text);
    query.words = query.phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return query;
}

bool matches(const Query &query, const QString &folded)
{
    return std::all_of(query.words.cbegin(), query.words.cend(), [&](const QString &word) {
        return folded.contains(word);
    });
}

Rank rank(const Query &query, const QString &folded)
{
    if (query.isEmpty())
        return Phrase;
    if (folded.contains(query.phrase))
        return Phrase;
    if (!matches(query, folded))
        return NoMatch;
    // In order: each word found after the end of the one before it. Taking the first place
    // each word appears from there is enough: a later place would only leave less room.
    qsizetype from = 0;
    for (const QString &word : query.words) {
        const qsizetype at = folded.indexOf(word, from);
        if (at < 0)
            return AnyOrder;
        from = at + word.size();
    }
    return InOrder;
}

} // namespace omaseed::core::SearchWords
