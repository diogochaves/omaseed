#include "core/ProcParent.hpp"

#include <QFile>

namespace omaseed::core::ProcParent {

std::optional<Stat> parseStat(const QByteArray &line)
{
    const qsizetype open = line.indexOf('(');
    const qsizetype close = line.lastIndexOf(')');
    if (open <= 0 || close < open)
        return std::nullopt;
    bool good = false;
    const uint pid = line.left(open).trimmed().toUInt(&good);
    if (!good || pid == 0)
        return std::nullopt;
    // After the comm: " STATE PPID …".
    const QList<QByteArray> rest = line.mid(close + 1).simplified().split(' ');
    if (rest.size() < 2)
        return std::nullopt;
    const uint ppid = rest.at(1).toUInt(&good);
    if (!good)
        return std::nullopt;
    Stat stat;
    stat.pid = pid;
    stat.ppid = ppid;
    stat.comm = QString::fromUtf8(line.mid(open + 1, close - open - 1));
    return stat;
}

std::optional<Stat> read(uint pid, const QString &root)
{
    if (pid == 0)
        return std::nullopt;
    QFile file(QStringLiteral("%1/%2/stat").arg(root).arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    // A stat line is well under 1 KiB; a comm can't hold a newline that matters here.
    const std::optional<Stat> stat = parseStat(file.read(4096).trimmed());
    if (!stat || stat->pid != pid)
        return std::nullopt;
    return stat;
}

Caller callerOf(uint pid, const QString &root)
{
    Caller caller;
    caller.wordPid = pid;
    const std::optional<Stat> word = read(pid, root);
    if (!word)
        return caller;
    caller.word = word->comm;
    if (const std::optional<Stat> parent = read(word->ppid, root)) {
        caller.parentPid = parent->pid;
        caller.parent = parent->comm;
    }
    return caller;
}

} // namespace omaseed::core::ProcParent
