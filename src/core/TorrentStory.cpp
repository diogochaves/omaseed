#include "core/TorrentStory.hpp"

#include "core/Format.hpp"
#include "core/TorrentProblem.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>
#include <tuple>
#include <utility>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("TorrentStory", text);
}

QString counted(qint64 count, const char *one, const char *many)
{
    return (count == 1 ? tr(one) : tr(many)).arg(count);
}

// DECISIONS C38 R3: a folder as the dim line names it, "Disc 1/".
QString folderWords(const QString &folder)
{
    return folder.endsWith(QLatin1Char('/')) ? folder : folder + QLatin1Char('/');
}

/// A name's entry (C38), not a filing or place one (C43).
bool isName(StoryEntry::What what)
{
    using What = StoryEntry::What;
    return what == What::Torrent || what == What::File || what == What::Folder ||
           what == What::Files;
}

// DECISIONS C43 Y3: the names in the tag list's order (the record keeps them so), the first
// three and "and N more".
QString listWords(const QStringList &names)
{
    constexpr int kShown = 3;
    const QString shown = names.mid(0, kShown).join(QStringLiteral(", "));
    if (names.size() <= kShown)
        return shown;
    return tr("%1 and %2 more").arg(shown, QString::number(names.size() - kShown));
}

// DECISIONS C43 Y2, Y8: a category's (Deluge: a label's) line, in the outcome strip's words.
std::pair<QString, QString> categoryWords(const StoryEntry &edit, bool labels)
{
    QString title;
    QStringList parts;
    if (edit.now.isEmpty()) {
        title = labels ? tr("Taken out of its label") : tr("Taken out of its category");
        if (!edit.was.isEmpty())
            parts << (labels ? tr("was %1") : tr("was in %1")).arg(edit.was);
    } else {
        title = (labels ? tr("Labelled %1") : tr("Put in %1")).arg(edit.now);
        parts << (edit.was.isEmpty() ? (labels ? tr("had no label") : tr("had no category"))
                                     : (labels ? tr("was %1") : tr("was in %1")).arg(edit.was));
    }
    // Y8: the name left the server, whoever removed it; else who changed it, when not omaseed.
    if (edit.removed)
        parts << (labels ? tr("the label was removed") : tr("the category was removed"));
    else if (edit.by == StoryEntry::By::Elsewhere)
        parts << tr("changed outside omaseed");
    return {title, parts.join(QStringLiteral(" · "))};
}

// DECISIONS C43 Y3, Y8: a tag edit's (Transmission: a label edit's) line. omaseed's own edit
// of one kind has no dim line.
std::pair<QString, QString> tagWords(const StoryEntry &edit, bool labels)
{
    QString title;
    QStringList parts;
    if (edit.untagged.isEmpty()) {
        title = (labels ? tr("Labelled %1") : tr("Tagged %1")).arg(listWords(edit.tagged));
    } else if (edit.tagged.isEmpty()) {
        title = (labels ? tr("Unlabelled %1") : tr("Untagged %1")).arg(listWords(edit.untagged));
    } else {
        title = labels ? tr("Labels changed") : tr("Tags changed");
        parts << (labels ? tr("labelled %1") : tr("tagged %1")).arg(listWords(edit.tagged))
              << (labels ? tr("unlabelled %1") : tr("untagged %1")).arg(listWords(edit.untagged));
    }
    if (edit.removed)
        parts << (labels ? tr("the label was removed") : tr("the tag was removed"));
    else if (edit.by == StoryEntry::By::Elsewhere)
        parts << tr("changed outside omaseed");
    return {title, parts.join(QStringLiteral(" · "))};
}

// DECISIONS C43 Y6, Y7: a save path seen changed in a poll, a move omaseed didn't make; "Will
// land in" while it waits in the incomplete folder.
std::pair<QString, QString> placeWords(const StoryEntry &place)
{
    const QString title = (place.landed ? tr("Moved to %1") : tr("Will land in %1")).arg(place.now);
    QString detail;
    if (place.by == StoryEntry::By::Server) {
        if (place.cause == QLatin1String("category"))
            detail = place.causeName.isEmpty()
                         ? tr("automatic management")
                         : tr("by its category %1 · automatic management").arg(place.causeName);
        else if (place.cause == QLatin1String("label"))
            detail = tr("by its label %1").arg(place.causeName);
    } else {
        // C18 D7's words.
        detail = tr("moved outside omaseed");
    }
    return {title, detail};
}

// DECISIONS C38: a rename's line, its title and its dim line.
std::pair<QString, QString> renameWords(const StoryEntry &rename)
{
    using What = StoryEntry::What;
    const QString change = tr("%1 → %2").arg(rename.was, rename.now);
    if (rename.what == What::Torrent) {
        // R1 a: what was lost, the old name. R4 a: C18 D7's words when it wasn't omaseed.
        // R7 a: Transmission renamed its folder on disk too (K28), in board 56's order.
        QStringList parts{tr("was %1").arg(rename.was)};
        if (rename.by == StoryEntry::By::Elsewhere)
            parts << tr("renamed outside omaseed");
        if (rename.disk)
            parts << tr("its folder on disk too");
        return {tr("Renamed"), parts.join(QStringLiteral(" · "))};
    }
    // R3 a: one line per run; file and folder lines read the same on every engine (R7).
    if (rename.what == What::Folder)
        return {tr("Renamed a folder"), change};
    if (rename.what == What::Files && rename.count > 1) {
        QString detail =
            tr("the first %1, and %2 more").arg(change, QString::number(rename.count - 1));
        if (rename.folders > 1)
            detail = tr("in %1 folders · %2").arg(QString::number(rename.folders), detail);
        else if (!rename.in.isEmpty())
            detail = tr("in %1 · %2").arg(folderWords(rename.in), detail);
        return {tr("Renamed %1 files").arg(rename.count), detail};
    }
    return {tr("Renamed a file"),
            rename.in.isEmpty() ? change : tr("%1 in %2").arg(change, folderWords(rename.in))};
}

} // namespace

QString TorrentStory::timeWords(const QDateTime &at, const QDateTime &now)
{
    const QDateTime local = at.toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QString time = QLocale::c().toString(local.time(), QStringLiteral("HH:mm"));
    if (local.date() == today)
        return time;
    if (local.date() == today.addDays(-1))
        return tr("yesterday %1").arg(time);
    if (local.date().year() == today.year())
        return QLocale::c().toString(local.date(), QStringLiteral("d MMM")) + QLatin1Char(' ') +
               time;
    return QLocale::c().toString(local.date(), QStringLiteral("d MMM yyyy"));
}

QString TorrentStory::kindName(StoryEvent::Kind kind)
{
    switch (kind) {
    case StoryEvent::Kind::Move:
        return QStringLiteral("move");
    case StoryEvent::Kind::Warn:
        return QStringLiteral("warn");
    case StoryEvent::Kind::Error:
        return QStringLiteral("error");
    case StoryEvent::Kind::Rename:
        return QStringLiteral("rename");
    case StoryEvent::Kind::Edit:
        return QStringLiteral("edit");
    case StoryEvent::Kind::Plain:
        break;
    }
    return QStringLiteral("plain");
}

QList<StoryEvent> TorrentStory::events(const StoryInput &input)
{
    using What = StoryEntry::What;
    const Torrent &t = input.torrent;
    QList<StoryEvent> out;
    auto add = [&](const QDateTime &at,
                   StoryEvent::Kind kind,
                   QString title,
                   QString detail,
                   int rank = 0) {
        if (!at.isValid())
            return;
        out.append({at, timeWords(at, input.now), kind, std::move(title), std::move(detail), rank});
    };

    // DECISIONS C43 Y5: the category it came with, the oldest category line's `was` (the
    // entries are newest first); with none, the category now, the best omaseed knows.
    QString addedInto = t.category;
    for (auto entry = input.entries.crbegin(); entry != input.entries.crend(); ++entry) {
        if (entry->what == What::Category) {
            addedInto = entry->was;
            break;
        }
    }
    add(t.addedOn,
        StoryEvent::Kind::Plain,
        tr("Added"),
        !input.categories ? QString()
        : addedInto.isEmpty()
            ? (input.categoriesAreLabels ? tr("with no label") : tr("with no category"))
            : tr("into %1").arg(addedInto));

    for (const StoryMove &move : input.moves) {
        QString detail;
        if (!move.folder.isEmpty())
            detail =
                move.others > 0
                    ? tr("with the folder %1, %2 torrents").arg(move.folder).arg(move.others + 1)
                    : tr("with the folder %1").arg(move.folder);
        else if (move.others > 0)
            detail = counted(move.others, "with %1 other torrent", "with %1 other torrents");
        else
            detail = tr("on its own");
        add(move.at, StoryEvent::Kind::Move, tr("Moved to %1").arg(move.to), detail);
    }

    // A torrent added already complete (a seed added in place) "finishes" when it is added;
    // that says nothing, so it is left out.
    if (t.completionOn.isValid() && t.addedOn.isValid() && t.completionOn > t.addedOn) {
        const qint64 took = t.addedOn.secsTo(t.completionOn);
        const qint64 size = t.totalSize > 0 ? t.totalSize : t.size;
        QString detail = took > 0 ? tr("in %1").arg(Format::duration(took)) : QString();
        if (size > 0)
            detail =
                detail.isEmpty() ? Format::size(size) : tr("%1 %2").arg(Format::size(size), detail);
        add(t.completionOn, StoryEvent::Kind::Plain, tr("Finished downloading"), detail);
    }

    if (!input.failure.isEmpty())
        add(input.failureAt, StoryEvent::Kind::Warn, input.failure, tr("from qBittorrent's log"));
    // Board 46's words for a move that didn't happen (docs/32's "The move to …"), with the
    // strip's reason in its dim line: the engine said nothing, and the torrent kept working.
    if (!input.notMovedTo.isEmpty()) {
        const QString where = ProblemMatch::lastTwoFolders(input.notMovedTo);
        add(input.notMovedAt,
            StoryEvent::Kind::Warn,
            where.isEmpty() ? tr("The move didn't happen")
                            : tr("The move to %1 didn't happen").arg(where),
            tr("%1 didn't move it and doesn't say why").arg(input.notMovedEngine));
    }
    if (!input.problem.isEmpty())
        add(input.problemAt,
            StoryEvent::Kind::Error,
            input.problem,
            input.problemEngine.isEmpty() ? tr("from qBittorrent's log")
            : input.problemPaused         ? tr("%1 paused it").arg(input.problemEngine)
                                          : tr("%1 stopped it").arg(input.problemEngine));

    // DECISIONS C38, C43: each entry the record keeps, at the server's OK or the poll that saw
    // it (C38 R1, R4; C43 Y8). Oldest first here, as the moves' history is read, so two at the
    // same time read the later one first once reversed below. Renames (C38 R2) and filing
    // edits (C43 Y4) in the accent; a place is a move (Y4).
    for (auto entry = input.entries.crbegin(); entry != input.entries.crend(); ++entry) {
        if (isName(entry->what)) {
            auto [title, detail] = renameWords(*entry);
            add(entry->at, StoryEvent::Kind::Rename, std::move(title), std::move(detail), 3);
        } else if (entry->what == What::Category) {
            auto [title, detail] = categoryWords(*entry, input.categoriesAreLabels);
            add(entry->at, StoryEvent::Kind::Edit, std::move(title), std::move(detail), 1);
        } else if (entry->what == What::Tags) {
            auto [title, detail] = tagWords(*entry, input.tagsAreLabels);
            add(entry->at, StoryEvent::Kind::Edit, std::move(title), std::move(detail), 1);
        } else if (entry->what == What::Place) {
            auto [title, detail] = placeWords(*entry);
            add(entry->at, StoryEvent::Kind::Move, std::move(title), std::move(detail), 2);
        }
    }

    // Newest first (DECISIONS C32 D15, amending C24 H6's order): sorted by time with two at
    // the same second in the order above (added before finished), then read backwards, so
    // the later of the two leads as the later time does. At the same second the rank goes
    // first, whatever the milliseconds: a filing edit above the rest, the place a poll saw
    // above the filing edit of that poll (C43 Y7, docs/58 §10), and a rename, the later act,
    // above them all (C38's amendment of H6).
    const auto order = [](const StoryEvent &event) {
        return std::tuple(event.at.toSecsSinceEpoch(), event.rank, event.at.toMSecsSinceEpoch());
    };
    std::stable_sort(out.begin(), out.end(), [&](const StoryEvent &a, const StoryEvent &b) {
        return order(a) < order(b);
    });
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace omaseed::core
