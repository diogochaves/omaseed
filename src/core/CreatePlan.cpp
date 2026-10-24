#include "core/CreatePlan.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace omaseed::core::CreatePlan {

namespace {

/// A hidden name: one that starts with a dot, in any part of `relative`.
bool isHidden(const QString &relative)
{
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts)
        if (part.startsWith(QLatin1Char('.')))
            return true;
    return false;
}

void settle(FolderCheck &check)
{
    if (check.state == FolderCheck::State::Unreadable)
        return;
    if (check.files > 0)
        check.state = FolderCheck::State::Ok;
    else if (check.hidden > 0)
        check.state = FolderCheck::State::OnlyHidden;
    else
        check.state = FolderCheck::State::Empty;
}

} // namespace

qint64 autoPieceSize(qint64 totalBytes)
{
    qint64 piece = kMinPiece;
    // piece ≈ 10·√size, rounded up to a power of two: size ≤ (piece / 10)².
    while (piece < kMaxAutoPiece) {
        const double tenth = double(piece) / 10.0;
        if (double(totalBytes) <= tenth * tenth)
            break;
        piece *= 2;
    }
    return piece;
}

qint64 pieceCount(qint64 totalBytes, qint64 pieceSize)
{
    if (pieceSize <= 0 || totalBytes <= 0)
        return 0;
    return (totalBytes + pieceSize - 1) / pieceSize;
}

qint64 torrentBytes(qint64 totalBytes, qint64 pieceSize, int fileCount)
{
    // 20 bytes of SHA-1 a piece, about 60 a file's entry, and a few hundred for the rest.
    return 20 * pieceCount(totalBytes, pieceSize) + 60 * qint64(qMax(fileCount, 1)) + 300;
}

QList<PieceRow> pieceRows(qint64 totalBytes, int fileCount)
{
    QList<PieceRow> rows;
    const qint64 automatic = autoPieceSize(totalBytes);
    for (qint64 size = kMinPiece; size <= kMaxPiece; size *= 2) {
        PieceRow row;
        row.size = size;
        row.count = pieceCount(totalBytes, size);
        row.torrentBytes = torrentBytes(totalBytes, size, fileCount);
        row.automatic = size == automatic;
        rows.append(row);
    }
    return rows;
}

FolderCheck checkLocal(const QString &path, bool ignoreDotfiles)
{
    FolderCheck check;
    check.sized = true;
    const QFileInfo info(path);
    if (!info.exists()) {
        check.state = FolderCheck::State::Missing;
        return check;
    }
    if (info.isFile()) {
        check.isFile = true;
        if (!info.isReadable()) {
            check.state = FolderCheck::State::Unreadable;
            check.unreadableFile = info.fileName();
            return check;
        }
        check.files = 1;
        check.bytes = info.size();
        check.state = FolderCheck::State::Ok;
        return check;
    }
    const QDir dir(path);
    if (!info.isReadable() || !info.isExecutable() || !dir.exists()) {
        check.state = FolderCheck::State::Unreadable;
        return check;
    }
    QDirIterator it(path,
                    QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo file(it.next());
        const QString relative = dir.relativeFilePath(file.filePath());
        if (isHidden(relative) && ignoreDotfiles) {
            ++check.hidden;
            check.hiddenBytes += file.size();
            if (check.hiddenNames.size() < 3)
                check.hiddenNames.append(file.fileName());
            continue;
        }
        if (!file.isReadable() && check.unreadableFile.isEmpty())
            check.unreadableFile = relative;
        ++check.files;
        check.bytes += file.size();
    }
    // A folder inside that can't be listed hides what is in it: say so rather than share less.
    QDirIterator dirs(
        path, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        const QFileInfo sub(dirs.next());
        const QString relative = dir.relativeFilePath(sub.filePath());
        if (ignoreDotfiles && isHidden(relative))
            continue;
        if ((!sub.isReadable() || !sub.isExecutable()) && check.unreadableFile.isEmpty())
            check.unreadableFile = relative + QLatin1Char('/');
    }
    if (!check.unreadableFile.isEmpty()) {
        check.state = FolderCheck::State::Unreadable;
        return check;
    }
    settle(check);
    return check;
}

FolderCheck fromListing(bool found, const QStringList &names, bool ignoreDotfiles)
{
    FolderCheck check;
    if (!found) {
        check.state = FolderCheck::State::Missing;
        return check;
    }
    for (const QString &name : names) {
        const QString last = nameOf(name);
        if (ignoreDotfiles && last.startsWith(QLatin1Char('.'))) {
            ++check.hidden;
            if (check.hiddenNames.size() < 3)
                check.hiddenNames.append(last);
        } else {
            ++check.files;
        }
    }
    settle(check);
    return check;
}

QString savedName(const QString &name, const QString &host)
{
    QString safe = host.isEmpty() ? name : QStringLiteral("%1 [%2]").arg(name, host);
    for (QChar &c : safe) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QChar(0) || c.unicode() < 32)
            c = QLatin1Char('_');
    }
    return safe + QStringLiteral(".torrent");
}

QString freeName(const QString &dir, const QString &fileName)
{
    const QDir folder(dir);
    if (!folder.exists(fileName))
        return fileName;
    const QString stem = fileName.endsWith(QLatin1String(".torrent"))
                             ? fileName.left(fileName.size() - 8)
                             : fileName;
    for (int n = 2;; ++n) {
        const QString next = QStringLiteral("%1 (%2).torrent").arg(stem).arg(n);
        if (!folder.exists(next))
            return next;
    }
}

QString nameOf(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    return trimmed.mid(trimmed.lastIndexOf(QLatin1Char('/')) + 1);
}

QString parentOf(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    const qsizetype slash = trimmed.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return trimmed.left(slash);
}

qint64 secondsLeft(int percent, qint64 elapsedMs)
{
    if (percent < 5 || percent >= 100 || elapsedMs < 2000)
        return -1;
    const double total = double(elapsedMs) * 100.0 / double(percent);
    return qint64((total - double(elapsedMs)) / 1000.0 + 0.5);
}

} // namespace omaseed::core::CreatePlan
