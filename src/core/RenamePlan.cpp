#include "core/RenamePlan.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace omaseed::core {

namespace {

/// The replacement for one match: `$1`…`$9` from the match (regex only), `{n…}` the counter
/// padded to that many digits, everything else as written.
QString
expand(const QString &replace, const QRegularExpressionMatch &match, bool regex, int counter)
{
    QString out;
    out.reserve(replace.size());
    for (qsizetype i = 0; i < replace.size(); ++i) {
        const QChar c = replace.at(i);
        if (regex && c == QLatin1Char('$') && i + 1 < replace.size() &&
            replace.at(i + 1).isDigit()) {
            out += match.captured(replace.at(i + 1).digitValue());
            ++i;
            continue;
        }
        if (c == QLatin1Char('{')) {
            qsizetype end = i + 1;
            while (end < replace.size() && replace.at(end) == QLatin1Char('n'))
                ++end;
            if (end > i + 1 && end < replace.size() && replace.at(end) == QLatin1Char('}')) {
                const int width = int(end - i - 1);
                out += QStringLiteral("%1").arg(counter, width, 10, QLatin1Char('0'));
                i = end;
                continue;
            }
        }
        out += c;
    }
    return out;
}

bool validName(const QString &name, bool stemEmpty)
{
    return !name.isEmpty() && !stemEmpty && name != QLatin1String(".") &&
           name != QLatin1String("..") && !name.contains(QLatin1Char('/'));
}

} // namespace

RenamePlan RenamePlan::make(const QStringList &paths,
                            const QStringList &otherPaths,
                            const RenameOptions &options,
                            const std::function<bool(const QString &)> &takenOnDisk)
{
    RenamePlan plan;
    QRegularExpression pattern;
    if (!options.find.isEmpty()) {
        pattern.setPattern(options.regex ? options.find : QRegularExpression::escape(options.find));
        QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
        if (!options.matchCase)
            flags |= QRegularExpression::CaseInsensitiveOption;
        pattern.setPatternOptions(flags);
        if (!pattern.isValid())
            plan.m_error = pattern.errorString();
    }

    int counter = 0;
    for (const QString &path : paths) {
        RenameRow row;
        row.path = path;
        row.newPath = path;
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        const QString folder = slash >= 0 ? path.left(slash + 1) : QString();
        const QString name = path.mid(slash + 1);
        row.newName = name;
        if (options.find.isEmpty() || !plan.m_error.isEmpty()) {
            plan.m_rows.append(row);
            continue;
        }

        const qsizetype dot = options.includeExtension ? -1 : name.lastIndexOf(QLatin1Char('.'));
        const QString stem = dot > 0 ? name.left(dot) : name;
        const QString extension = dot > 0 ? name.mid(dot) : QString();

        QList<QRegularExpressionMatch> matches;
        if (options.everyMatch) {
            auto it = pattern.globalMatch(stem);
            while (it.hasNext())
                matches.append(it.next());
        } else {
            const auto match = pattern.match(stem);
            if (match.hasMatch())
                matches.append(match);
        }
        if (matches.isEmpty()) {
            plan.m_rows.append(row);
            continue;
        }

        ++counter;
        QString newStem;
        qsizetype at = 0;
        for (const auto &match : matches) {
            newStem += stem.mid(at, match.capturedStart() - at);
            const QString replacement = expand(options.replace, match, options.regex, counter);
            if (!replacement.isEmpty())
                row.marks.append({newStem.size(), replacement.size()});
            newStem += replacement;
            at = match.capturedEnd();
        }
        newStem += stem.mid(at);
        row.newName = newStem + extension;
        row.newPath = folder + row.newName;
        if (row.newPath == path)
            row.status = RenameRow::Status::Unchanged;
        else if (!validName(row.newName, newStem.isEmpty() && !extension.isEmpty()))
            row.status = RenameRow::Status::Invalid;
        else
            row.status = RenameRow::Status::Changed;
        plan.m_rows.append(row);
    }

    // #64: a new path held on disk by a file the torrent does not own; qBittorrent would let
    // the rename replace it. Checked first, so such a file counts below as staying put.
    if (takenOnDisk) {
        for (RenameRow &row : plan.m_rows) {
            if (row.status == RenameRow::Status::Changed && takenOnDisk(row.newPath))
                row.status = RenameRow::Status::Collides;
        }
    }

    // Where every file ends up: the others and the unchanged ones where they are, the
    // changed ones at their new paths. A path twice is a collision.
    QHash<QString, int> landing;
    for (const QString &other : otherPaths)
        landing[other] += 1;
    for (const RenameRow &row : std::as_const(plan.m_rows))
        landing[row.status == RenameRow::Status::Changed ? row.newPath : row.path] += 1;
    for (RenameRow &row : plan.m_rows) {
        if (row.status == RenameRow::Status::Changed && landing.value(row.newPath) > 1)
            row.status = RenameRow::Status::Collides;
    }

    // Order the renames so each target is free when its turn comes: a rename waits while its
    // target is still some other changing file's current path. What never frees up is a
    // cycle, and collides.
    QList<int> pending;
    for (int i = 0; i < plan.m_rows.size(); ++i) {
        if (plan.m_rows.at(i).status == RenameRow::Status::Changed)
            pending.append(i);
    }
    QSet<QString> occupied;
    for (int i : std::as_const(pending))
        occupied.insert(plan.m_rows.at(i).path);
    QList<int> order;
    bool moved = true;
    while (!pending.isEmpty() && moved) {
        moved = false;
        for (auto it = pending.begin(); it != pending.end();) {
            const RenameRow &row = plan.m_rows.at(*it);
            if (occupied.contains(row.newPath)) {
                ++it;
                continue;
            }
            occupied.remove(row.path);
            order.append(*it);
            it = pending.erase(it);
            moved = true;
        }
    }
    for (int i : std::as_const(pending))
        plan.m_rows[i].status = RenameRow::Status::Collides;

    for (const RenameRow &row : std::as_const(plan.m_rows)) {
        switch (row.status) {
        case RenameRow::Status::Changed:
            ++plan.m_changed;
            break;
        case RenameRow::Status::Collides:
            ++plan.m_collisions;
            break;
        case RenameRow::Status::Invalid:
            ++plan.m_invalid;
            break;
        case RenameRow::Status::Unchanged:
            break;
        }
    }
    if (plan.canRun())
        plan.m_order = order;
    return plan;
}

bool RenamePlan::canRun() const
{
    return m_error.isEmpty() && m_changed > 0 && m_collisions == 0 && m_invalid == 0;
}

} // namespace omaseed::core
