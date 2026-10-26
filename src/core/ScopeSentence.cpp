#include "core/ScopeSentence.hpp"

#include "core/Format.hpp"
#include "core/TorrentFilterModel.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ScopeSentence", text);
}

/// `one` for 1, `many` for any other count, with the count put in.
QString counted(qint64 count, const char *one, const char *many)
{
    return (count == 1 ? tr(one) : tr(many)).arg(count);
}

QString torrents(qint64 count)
{
    return counted(count, "%1 torrent", "%1 torrents");
}

/// "1.6": a ratio said in a sentence, one decimal.
QString ratioWords(double ratio)
{
    return ratio >= 9999.0 ? QStringLiteral("∞") : QString::number(ratio, 'f', 1);
}

const QString kApart = QStringLiteral(" · ");

/// #201 F14: arriving torrents with no seed online and no other estimate: "waiting for a
/// seed" when that is all of them, else how many wait.
QString waitingWords(const ScopeFacts &facts)
{
    if (facts.waitingSeed >= facts.arriving)
        return facts.arriving == 1 ? tr("waiting for a seed") : tr("all waiting for a seed");
    return tr("%1 waiting for a seed").arg(facts.waitingSeed);
}

} // namespace

void ScopeFacts::add(const Torrent &torrent, int question)
{
    ++total;
    bytes += std::max<qint64>(0, torrent.size);
    upSpeed += std::max<qint64>(0, torrent.upspeed);
    switch (question) {
    case TorrentFilterModel::NeedsYou:
        ++needsYou;
        break;
    case TorrentFilterModel::Arriving:
        ++arriving;
        arrivingSpeed += std::max<qint64>(0, torrent.dlspeed);
        // #201 F14: qBittorrent keeps an estimate for a stalled torrent from its old speed,
        // but with no seed online there is no time to ready.
        if (torrent.state == TorrentState::StalledDownload)
            ++waitingSeed;
        else if (torrent.hasEta() && torrent.eta >= 0 && torrent.eta < 8640000) {
            nextEta = nextEta < 0 ? torrent.eta : std::min(nextEta, torrent.eta);
            lastEta = std::max(lastEta, torrent.eta);
        }
        break;
    case TorrentFilterModel::LandedToday:
        ++landedToday;
        if (!torrent.inGroup(StatusGroup::Stopped))
            ++landedSharing;
        break;
    case TorrentFilterModel::GivingBack:
        ++givingBack;
        givingSpeed += std::max<qint64>(0, torrent.upspeed);
        givenUp += std::max<qint64>(0, torrent.uploaded);
        givenDown += std::max<qint64>(0, torrent.downloaded);
        break;
    case TorrentFilterModel::Resting:
        ++resting;
        break;
    default:
        break;
    }
}

QString ScopeSentence::library(const ScopeFacts &facts)
{
    if (facts.total == 0)
        return tr("no torrents yet");
    QStringList parts;
    if (facts.needsYou > 0)
        parts << counted(facts.needsYou, "%1 needs you", "%1 need you");
    if (facts.arriving > 0)
        parts
            << (facts.nextEta >= 0
                    ? tr("%1 arriving, the next in %2").arg(facts.arriving).arg(soon(facts.nextEta))
                    : tr("%1 arriving").arg(facts.arriving));
    if (facts.landedToday > 0)
        parts << tr("%1 landed today").arg(facts.landedToday);
    if (facts.upSpeed > 0)
        parts << tr("giving back %1").arg(Format::speed(facts.upSpeed));
    else if (facts.givingBack > 0)
        parts << tr("%1 giving back").arg(facts.givingBack);
    if (parts.isEmpty())
        parts << tr("%1 resting").arg(facts.resting);
    return parts.join(kApart);
}

QString ScopeSentence::question(int question, const ScopeFacts &facts)
{
    switch (question) {
    case TorrentFilterModel::NeedsYou:
        if (facts.needsYou == 0)
            return tr("nothing needs you");
        return counted(facts.needsYou, "%1 torrent waits for a fix", "%1 torrents wait for a fix") +
               kApart + tr("nothing else is wrong");
    case TorrentFilterModel::Arriving: {
        if (facts.arriving == 0)
            return tr("nothing arriving");
        QStringList parts{torrents(facts.arriving)};
        if (facts.nextEta >= 0) {
            if (facts.arriving == 1)
                parts << tr("ready in %1").arg(soon(facts.nextEta));
            else if (facts.lastEta > facts.nextEta)
                parts << tr("the next ready in %1, the last in about %2")
                             .arg(soon(facts.nextEta), soon(facts.lastEta));
            else
                parts << tr("the next ready in %1").arg(soon(facts.nextEta));
        } else if (facts.waitingSeed > 0) {
            parts << waitingWords(facts);
        }
        if (facts.arrivingSpeed > 0)
            parts << Format::speed(facts.arrivingSpeed);
        return parts.join(kApart);
    }
    case TorrentFilterModel::LandedToday: {
        if (facts.landedToday == 0)
            return tr("nothing landed yet today");
        QString sharing;
        if (facts.landedToday == 1)
            sharing = facts.landedSharing == 1 ? tr("seeding") : tr("stopped");
        else if (facts.landedSharing == facts.landedToday)
            sharing = facts.landedToday == 2 ? tr("both seeding") : tr("all seeding");
        else if (facts.landedSharing == 0)
            sharing = tr("none seeding");
        else
            sharing = tr("%1 seeding").arg(facts.landedSharing);
        return tr("%1 since midnight").arg(facts.landedToday) + kApart + sharing;
    }
    case TorrentFilterModel::GivingBack: {
        if (facts.givingBack == 0)
            return tr("nothing giving back");
        QString line = torrents(facts.givingBack);
        line += facts.givingSpeed > 0 ? tr(" at %1").arg(Format::speed(facts.givingSpeed))
                                      : kApart + tr("nobody asking now");
        if (facts.givenDown > 0)
            line += kApart +
                    tr("ratio %1").arg(ratioWords(double(facts.givenUp) / double(facts.givenDown)));
        return line;
    }
    case TorrentFilterModel::Resting:
        if (facts.resting == 0)
            return tr("nothing resting");
        return torrents(facts.resting) + kApart + tr("not sharing");
    default:
        return library(facts);
    }
}

QString ScopeSentence::groupLine(int question, const ScopeFacts &facts)
{
    switch (question) {
    case TorrentFilterModel::NeedsYou:
        if (facts.needsYou == 0)
            return {};
        return counted(facts.needsYou, "%1 fix waits", "%1 fixes wait");
    case TorrentFilterModel::Arriving:
        if (facts.nextEta >= 0)
            return facts.arriving == 1 ? tr("ready in %1").arg(soon(facts.nextEta))
                                       : tr("next ready in %1").arg(soon(facts.nextEta));
        if (facts.arrivingSpeed > 0)
            return Format::speed(facts.arrivingSpeed);
        return facts.waitingSeed > 0 ? waitingWords(facts) : QString();
    case TorrentFilterModel::LandedToday:
        return tr("since midnight");
    case TorrentFilterModel::GivingBack: {
        QStringList parts;
        parts << (facts.givingSpeed > 0 ? Format::speed(facts.givingSpeed)
                                        : tr("nobody asking now"));
        if (facts.givenDown > 0)
            parts
                << tr("ratio %1").arg(ratioWords(double(facts.givenUp) / double(facts.givenDown)));
        return parts.join(kApart);
    }
    case TorrentFilterModel::Resting:
        return tr("not sharing");
    default:
        return {};
    }
}

QString ScopeSentence::folderLine(const ScopeFacts &facts)
{
    QString line = torrents(facts.total);
    if (facts.bytes > 0)
        line += kApart + Format::size(facts.bytes);
    return line;
}

QString ScopeSentence::category(const ScopeFacts &facts, const QString &savePath)
{
    QStringList parts{torrents(facts.total)};
    // Magnets still fetching their metadata have no size yet: nothing to say then.
    if (facts.bytes > 0)
        parts << Format::size(facts.bytes);
    if (!savePath.isEmpty())
        parts << tr("saves to %1").arg(savePath);
    return parts.join(kApart);
}

QString ScopeSentence::tag(const ScopeFacts &facts)
{
    return torrents(facts.total) + kApart + tr("tag");
}

QString ScopeSentence::tracker(const ScopeFacts &facts)
{
    return torrents(facts.total) + kApart + tr("tracker");
}

QString ScopeSentence::questionWord(int question)
{
    switch (question) {
    case TorrentFilterModel::NeedsYou:
        return tr("needs you");
    case TorrentFilterModel::Arriving:
        return tr("arriving");
    case TorrentFilterModel::LandedToday:
        return tr("landed today");
    case TorrentFilterModel::GivingBack:
        return tr("giving back");
    case TorrentFilterModel::Resting:
        return tr("resting");
    default:
        return {};
    }
}

QString ScopeSentence::narrowed(int question, const QString &sentence)
{
    const QString word = questionWord(question);
    return word.isEmpty() ? sentence : word + kApart + sentence;
}

QString ScopeSentence::folder(
    int torrentCount, qint64 bytesOnDisk, int savePaths, int loose, const QString &path)
{
    QStringList parts{torrents(torrentCount),
                      tr("%1 on disk").arg(Format::size(std::max<qint64>(0, bytesOnDisk)))};
    if (savePaths > 1)
        parts << counted(savePaths, "%1 save path", "%1 save paths");
    if (loose > 0)
        parts << counted(loose, "%1 loose file", "%1 loose files");
    if (!path.isEmpty())
        parts << path;
    return parts.join(kApart);
}

QString ScopeSentence::folderMoving(const QString &newPath, int landed, int torrentCount)
{
    return tr("moving to %1").arg(newPath) + kApart +
           tr("%1 of %2 landed").arg(landed).arg(torrentCount);
}

QString ScopeSentence::moveJob(int torrentCount, int withServer)
{
    QString line = torrents(torrentCount);
    if (withServer > 0)
        line += kApart + tr("%1 with qBittorrent").arg(withServer);
    return line;
}

QString ScopeSentence::moveSheet(int torrentCount)
{
    return counted(
        torrentCount, "%1 torrent the move sheet listed", "%1 torrents the move sheet listed");
}

QString ScopeSentence::seed(const QString &upSpeed, const QString &today, const QString &scope)
{
    QStringList parts;
    if (!upSpeed.isEmpty())
        parts << tr("giving back %1").arg(upSpeed);
    if (!today.isEmpty())
        parts << today;
    if (!scope.isEmpty())
        parts << scope;
    return parts.join(kApart);
}

QString ScopeSentence::log(const QString &server,
                          int lines,
                          const QString &since,
                          int warnings,
                          int critical,
                          bool following)
{
    QString line =
        server.isEmpty() ? tr("qBittorrent's log") : tr("qBittorrent's log on %1").arg(server);
    if (lines > 0) {
        line += kApart + counted(lines, "%1 line", "%1 lines");
        if (!since.isEmpty())
            line += tr(" since %1").arg(since);
        // Board 51 D5 a: what needs reading, and whether new lines pull the view down (the
        // log's bar, gone into the rail and the title bar, said where it stood).
        QStringList problems;
        if (warnings > 0)
            problems << counted(warnings, "%1 warning", "%1 warnings");
        if (critical > 0)
            problems << tr("%1 critical").arg(critical);
        if (!problems.isEmpty())
            line += kApart + problems.join(QStringLiteral(", "));
        if (following)
            line += kApart + tr("following");
    }
    return line;
}

QString ScopeSentence::lost(const QString &listTime, const QString &headline)
{
    if (listTime.isEmpty())
        return headline;
    const QString list = tr("the list is from %1").arg(listTime);
    return headline.isEmpty() ? list : list + kApart + headline;
}

QString ScopeSentence::soon(qint64 seconds)
{
    if (seconds < 60)
        return tr("%1 s").arg(std::max<qint64>(0, seconds));
    if (seconds < 3600)
        return tr("%1 min").arg((seconds + 59) / 60);
    return Format::duration(seconds);
}

} // namespace omaseed::core
