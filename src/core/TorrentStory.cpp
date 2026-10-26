#include "core/TorrentStory.hpp"

#include "core/Format.hpp"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

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
    case StoryEvent::Kind::Plain:
        break;
    }
    return QStringLiteral("plain");
}

QList<StoryEvent> TorrentStory::events(const StoryInput &input)
{
    const Torrent &t = input.torrent;
    QList<StoryEvent> out;
    auto add = [&](const QDateTime &at, StoryEvent::Kind kind, QString title, QString detail) {
        if (!at.isValid())
            return;
        out.append({at, timeWords(at, input.now), kind, std::move(title), std::move(detail)});
    };

    add(t.addedOn,
        StoryEvent::Kind::Plain,
        tr("Added"),
        t.category.isEmpty() ? tr("with no category") : tr("into %1").arg(t.category));

    for (const StoryMove &move : input.moves) {
        QString detail;
        if (!move.folder.isEmpty())
            detail =
                move.others > 0
                    ? tr("with the folder %1, %2 torrents").arg(move.folder).arg(move.others + 1)
                    : tr("with the folder %1").arg(move.folder);
        else if (move.others > 0)
            detail = counted(move.others, "with 1 other torrent", "with %1 other torrents");
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
    if (!input.problem.isEmpty())
        add(input.problemAt, StoryEvent::Kind::Error, input.problem, tr("from qBittorrent's log"));

    // Newest first (DECISIONS C32 D15, amending C24 H6's order): sorted by time with two at
    // the same second in the order above (added before finished), then read backwards, so
    // the later of the two leads as the later time does.
    std::stable_sort(out.begin(), out.end(), [](const StoryEvent &a, const StoryEvent &b) {
        return a.at < b.at;
    });
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace omaseed::core
