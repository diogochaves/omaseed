#include "core/RowWords.hpp"

#include "core/Format.hpp"
#include "core/ScopeSentence.hpp"
#include "core/TorrentFilterModel.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("RowWords", text);
}

/// `one` for 1, `many` for any other count, with the count put in.
QString counted(qint64 count, const char *one, const char *many)
{
    return (count == 1 ? tr(one) : tr(many)).arg(count);
}

const QString kApart = QStringLiteral(" · ");

/// "2.4×": a ratio as the row's fact says it, one decimal.
QString times(double ratio)
{
    if (ratio >= 9999.0)
        return QStringLiteral("∞×");
    return tr("%1×").arg(QString::number(std::max(0.0, ratio), 'f', 1));
}

bool isChecking(TorrentState state)
{
    return state == TorrentState::CheckingDownload || state == TorrentState::CheckingUpload ||
           state == TorrentState::CheckingResumeData || state == TorrentState::Allocating;
}

bool isMetadata(TorrentState state)
{
    return state == TorrentState::MetaDownload || state == TorrentState::ForcedMetaDownload;
}

bool isQueued(TorrentState state)
{
    return state == TorrentState::QueuedDownload || state == TorrentState::QueuedUpload;
}

/// "48.9 % of 480 MB".
QString partOf(const Torrent &t)
{
    if (t.size <= 0)
        return Format::percent(t.progress);
    return tr("%1 of %2").arg(Format::percent(t.progress), Format::size(t.size));
}

/// The needs-you words: the short cause, the long one, the fix.
void needsYou(const Torrent &t, const RowTrouble &trouble, RowWords &words)
{
    words.caption = tr("needs you");
    // C20: an errored torrent that was stopped says "stopped" in the Status column; the row
    // keeps the cause it stopped on, when the log has one.
    const bool stoppedWord = trouble.cause == QLatin1String("stopped");
    if (!trouble.cause.isEmpty() && !stoppedWord)
        words.fact = trouble.cause;
    else if (trouble.failureKind == QLatin1String("move"))
        words.fact = tr("didn't move");
    else if (trouble.stuckMove)
        words.fact = tr("move failed");
    else if (t.state == TorrentState::MissingFiles || trouble.missingFiles)
        words.fact = tr("files missing");
    else if (!trouble.failureKind.isEmpty())
        words.fact = tr("failed");
    else
        words.fact = stoppedWord ? tr("stopped") : tr("error");

    if (!trouble.headline.isEmpty())
        words.sentence = trouble.headline;
    else if (!trouble.failureHeadline.isEmpty())
        words.sentence = trouble.failureHeadline;
    else if (t.state == TorrentState::MissingFiles)
        words.sentence = t.savePath.isEmpty() ? tr("its files are gone")
                                              : tr("its files are gone from %1").arg(t.savePath);
    else
        words.sentence = tr("qBittorrent stopped it with an error");

    // The error block's first action leads (docs/28); else the failure block's (docs/32);
    // an error the log says nothing about can still be retried (C20).
    QString action;
    QString from;
    for (const QString &id : trouble.actions) {
        if (!RowWords::fixWords(id, trouble.stuckMove).isEmpty()) {
            action = id;
            from = QStringLiteral("problem");
            break;
        }
    }
    if (action.isEmpty()) {
        for (const QString &id : trouble.failureActions) {
            if (id != QLatin1String("log") && id != QLatin1String("dismiss") &&
                !RowWords::fixWords(id, false).isEmpty()) {
                action = id;
                from = QStringLiteral("failure");
                break;
            }
        }
    }
    if (action.isEmpty() && t.state == TorrentState::Error) {
        action = QStringLiteral("start");
        from = QStringLiteral("problem");
    }
    if (!action.isEmpty()) {
        words.fix = RowWords::fixWords(action, trouble.stuckMove);
        words.fixAction = from + QLatin1Char(':') + action;
    }
}

/// The arriving words, by state.
void arriving(const Torrent &t, RowWords &words)
{
    if (isMetadata(t.state)) {
        words.fact = tr("finding files");
        words.caption = tr("magnet");
        const int peers = t.numSeeds + t.numLeechs;
        words.sentence = peers > 0 ? counted(peers,
                                             "asking %1 peer for the file list",
                                             "asking %1 peers for the file list")
                                   : tr("looking for peers who have the file list");
        return;
    }
    if (isChecking(t.state)) {
        words.fact = tr("checking");
        words.caption = Format::percent(t.progress);
        words.sentence = tr("making sure the files on disk are whole");
        return;
    }
    if (isQueued(t.state)) {
        words.fact = t.priority == 1 ? tr("next") : tr("queued");
        words.caption = tr("in line");
        words.sentence = tr("starts when a download slot frees");
        return;
    }
    if (t.state == TorrentState::Moving) {
        words.fact = tr("moving");
        words.caption = Format::percent(t.progress);
        words.sentence = tr("qBittorrent is moving its files");
        return;
    }
    if (t.state == TorrentState::StalledDownload) {
        words.fact = tr("waiting");
        words.caption = tr("no seed");
        QString line = tr("no seed online");
        if (t.lastActivity.isValid() && t.lastActivity.toMSecsSinceEpoch() > 0)
            line += kApart + tr("last data %1").arg(Format::relativeTime(t.lastActivity));
        else
            line += kApart + partOf(t);
        words.sentence = line;
        return;
    }
    // Downloading.
    words.fact = t.hasEta() && t.eta >= 0 && t.eta < 8640000 ? ScopeSentence::soon(t.eta)
                                                             : QStringLiteral("∞");
    words.caption = tr("to ready");
    QString from;
    if (t.dlspeed > 0)
        from = counted(t.numSeeds, "from %1 seed", "from %1 seeds") + QLatin1Char(' ') +
               tr("at %1").arg(Format::speed(t.dlspeed));
    else if (t.numLeechs > 0)
        from = counted(t.numSeeds, "from %1 seed", "from %1 seeds") + QLatin1Char(' ') +
               counted(t.numLeechs, "and %1 peer", "and %1 peers");
    else
        from = counted(t.numSeeds, "from %1 seed", "from %1 seeds");
    words.sentence = partOf(t) + kApart + from;
}

} // namespace

QString RowWords::fixWords(const QString &action, bool stuckMove)
{
    // The error and failure blocks' words for the same ids (DetailsErrorBlock.qml).
    if (action == QLatin1String("start"))
        return tr("retry");
    if (action == QLatin1String("location"))
        return stuckMove ? tr("set location…") : tr("move…");
    if (action == QLatin1String("recheck"))
        return tr("recheck");
    if (action == QLatin1String("addAgain"))
        return tr("add it again…");
    if (action == QLatin1String("folder"))
        return tr("open folder");
    return {};
}

RowWords RowWords::of(const Torrent &t, int question, const RowTrouble &trouble)
{
    RowWords words;

    // Beside the name: what kind of place it is in and how big, "magnet" before the metadata.
    if (isMetadata(t.state) && t.totalSize < 0)
        words.meta = t.category.isEmpty() ? tr("magnet") : t.category + kApart + tr("magnet");
    else if (t.category.isEmpty())
        words.meta = Format::size(std::max<qint64>(0, t.size));
    else
        words.meta = t.category + kApart + Format::size(std::max<qint64>(0, t.size));

    switch (question) {
    case TorrentFilterModel::NeedsYou:
        needsYou(t, trouble, words);
        break;
    case TorrentFilterModel::Arriving:
        arriving(t, words);
        break;
    case TorrentFilterModel::LandedToday: {
        words.fact = t.completionOn.toLocalTime().toString(QStringLiteral("HH:mm"));
        words.caption = tr("landed");
        QStringList parts;
        if (!t.category.isEmpty())
            parts << tr("in %1").arg(t.category);
        parts << Format::size(std::max<qint64>(0, t.size));
        parts << tr("given back %1 so far").arg(times(t.ratio));
        words.sentence = parts.join(kApart);
        break;
    }
    case TorrentFilterModel::GivingBack: {
        words.fact = times(t.ratio);
        words.caption = tr("given");
        QStringList parts;
        if (isChecking(t.state))
            parts << tr("making sure the files on disk are whole");
        else if (isQueued(t.state))
            parts << tr("waiting for a seeding slot");
        else if (t.upspeed > 0)
            parts << tr("sending %1").arg(Format::speed(t.upspeed)) + QLatin1Char(' ') +
                         counted(std::max(1, t.numLeechs), "to %1 peer", "to %1 peers");
        else
            parts << tr("nobody is asking now");
        if (t.uploaded > 0)
            parts << tr("%1 given").arg(Format::size(t.uploaded));
        words.sentence = parts.join(kApart);
        break;
    }
    case TorrentFilterModel::Resting:
        words.fact = tr("stopped");
        if (t.isComplete()) {
            words.caption = times(t.ratio);
            words.sentence = tr("not sharing") + kApart +
                             tr("%1 on disk").arg(Format::size(std::max<qint64>(0, t.size)));
        } else {
            words.caption = Format::percent(t.progress);
            words.sentence = partOf(t) + kApart + tr("waiting for you to start it");
        }
        break;
    default:
        words.fact = t.stateText();
        break;
    }
    return words;
}

QString RowWords::monogram(const QString &name)
{
    // Words split on spaces and the separators release names use.
    static const QRegularExpression apart(QStringLiteral("[\\s._\\-]+"));
    const QStringList parts = name.split(apart, Qt::SkipEmptyParts);
    QString first;
    for (const QString &part : parts) {
        if (!part.isEmpty() && part.at(0).isLetter()) {
            first = part;
            break;
        }
    }
    if (first.isEmpty())
        return {};
    const int at = int(parts.indexOf(first));
    const QString next = at + 1 < parts.size() ? parts.at(at + 1) : QString();
    if (!next.isEmpty() && next.at(0).isLetter())
        return QString(first.at(0)).toUpper() + QString(next.at(0)).toUpper();
    // The first word's first two letters (digits and marks skipped): "distro-2026" is "DI".
    QString two;
    for (const QChar c : first) {
        if (c.isLetter())
            two += c;
        if (two.size() == 2)
            break;
    }
    return two.toUpper();
}

} // namespace omaseed::core
