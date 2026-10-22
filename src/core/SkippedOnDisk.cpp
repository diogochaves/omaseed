#include "core/SkippedOnDisk.hpp"

#include "core/PieceStates.hpp"
#include "core/SharedData.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>

#include <sys/stat.h>

namespace omaseed::core {

namespace {

constexpr qint64 kGiB = 1024LL * 1024 * 1024;

bool holdsDownloadedPiece(const QByteArray &states, int first, int last)
{
    if (first < 0 || last < first || states.isEmpty())
        return false;
    const int end = std::min<int>(last, int(states.size()) - 1);
    for (int piece = first; piece <= end; ++piece) {
        if (quint8(states.at(piece)) == PieceStates::Have)
            return true;
    }
    return false;
}

} // namespace

bool SkippedReading::bandShown() const
{
    // DECISIONS C17 D2: more than 1 GiB, or more than 5 % of the torrent.
    if (held > kGiB)
        return true;
    return torrentSize > 0 && held * 20 > torrentSize;
}

QHash<int, qint64> SkippedReading::allocatedByIndex() const
{
    QHash<int, qint64> result;
    for (const SkippedFile &file : files) {
        if (file.allocated >= 0)
            result.insert(file.index, file.allocated);
    }
    return result;
}

QStringList FreePlan::deletePaths() const
{
    QStringList paths;
    for (const FreeItem &item : items) {
        if (item.verdict == FreeItem::Verdict::Delete)
            paths.append(item.file.localPath);
    }
    return paths;
}

QStringList FreePlan::piecesPaths() const
{
    QStringList paths;
    for (const FreeItem &item : items) {
        if (item.verdict == FreeItem::Verdict::Pieces)
            paths.append(item.file.localPath);
    }
    return paths;
}

SkippedOnDisk::SkippedOnDisk(AllocatedReader reader, QObject *parent)
    : QObject(parent)
    , m_reader(std::move(reader))
{}

qint64 SkippedOnDisk::readAllocated(const QString &localPath)
{
    if (localPath.isEmpty())
        return -1;
    struct stat info{};
    if (::lstat(QFile::encodeName(localPath).constData(), &info) != 0 || !S_ISREG(info.st_mode))
        return -1;
    return qint64(info.st_blocks) * 512;
}

SkippedReading SkippedOnDisk::read(const QString &hash,
                                   const QList<ContentFile> &files,
                                   const QByteArray &pieceStates,
                                   qint64 torrentSize,
                                   const LocalPathOf &localPathOf,
                                   const AllocatedReader &reader)
{
    SkippedReading reading;
    reading.hash = hash;
    reading.torrentSize = torrentSize;
    reading.seen = bool(localPathOf);
    for (qsizetype at = 0; at < files.size(); ++at) {
        const ContentFile &entry = files.at(at);
        if (entry.priority.value_or(int(FilePriority::Normal)) != int(FilePriority::Skip))
            continue;
        SkippedFile file;
        file.index = entry.index.value_or(int(at));
        file.path = entry.path;
        file.size = entry.size;
        file.progress = entry.progress;
        file.pieceFirst = entry.pieces ? entry.pieces->first : -1;
        file.pieceLast = entry.pieces ? entry.pieces->second : -1;
        // docs/44 §2: progress 0 is the quick check; a piece shared with a wanted neighbour
        // counts as downloaded, so such a file is treated as holding pieces.
        file.holdsPieces =
            file.progress > 0 || holdsDownloadedPiece(pieceStates, file.pieceFirst, file.pieceLast);
        if (file.progress > 0)
            ++reading.partlyWritten;
        if (localPathOf) {
            file.localPath = localPathOf(file.path);
            if (file.localPath.isEmpty())
                reading.seen = false;
            else if (reader)
                file.allocated = reader(file.localPath);
        }
        if (file.holdsSpace()) {
            reading.held += file.allocated;
            ++reading.holding;
        }
        reading.files.append(file);
    }
    if (!reading.seen) {
        // D4: a server whose disks this machine can't see holds an unknown amount.
        for (SkippedFile &file : reading.files) {
            file.allocated = -1;
            file.localPath.clear();
        }
        reading.held = 0;
        reading.holding = 0;
    }
    return reading;
}

FreePlan SkippedOnDisk::plan(const SkippedReading &reading,
                             bool hybrid,
                             const QString &contentLocal,
                             const QStringList &sharedRoots,
                             const QHash<QString, QString> &openBy)
{
    FreePlan plan;
    plan.hybrid = hybrid;
    for (const SkippedFile &file : reading.files) {
        if (!file.holdsSpace() || file.localPath.isEmpty())
            continue;
        FreeItem item;
        item.file = file;
        const QString path = SharedData::normalize(file.localPath);
        const bool shared =
            std::any_of(sharedRoots.cbegin(), sharedRoots.cend(), [&](const QString &root) {
                return !root.isEmpty() && SharedData::isUnder(path, SharedData::normalize(root));
            });
        if (contentLocal.isEmpty() ||
            !SharedData::isUnder(path, SharedData::normalize(contentLocal))) {
            item.verdict = FreeItem::Verdict::Outside;
        } else if (shared) {
            item.verdict = FreeItem::Verdict::Shared;
        } else if (openBy.contains(file.localPath)) {
            item.verdict = FreeItem::Verdict::Open;
            item.openBy = openBy.value(file.localPath);
        } else if (file.holdsPieces && hybrid) {
            // C17 D3: never a recheck on a hybrid (#115).
            item.verdict = FreeItem::Verdict::Pieces;
            ++plan.leftWithPieces;
            plan.leftWithPiecesBytes += file.allocated;
        } else {
            item.verdict = FreeItem::Verdict::Delete;
            ++plan.deletes;
            plan.freed += file.allocated;
            plan.recheck = plan.recheck || file.holdsPieces;
        }
        plan.items.append(item);
    }
    // Deletes first, the biggest first; then what stays, the biggest first.
    std::stable_sort(
        plan.items.begin(), plan.items.end(), [](const FreeItem &a, const FreeItem &b) {
            const bool aDeletes = a.verdict == FreeItem::Verdict::Delete;
            const bool bDeletes = b.verdict == FreeItem::Verdict::Delete;
            if (aDeletes != bDeletes)
                return aDeletes;
            return a.file.allocated > b.file.allocated;
        });
    return plan;
}

QHash<QString, QString> SkippedOnDisk::openFileHolders(const QSet<QString> &paths,
                                                       const QString &procRoot)
{
    QHash<QString, QString> holders;
    if (paths.isEmpty())
        return holders;
    const QDir proc(procRoot);
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool numeric = false;
        pid.toLongLong(&numeric);
        if (!numeric)
            continue;
        const QDir fds(proc.filePath(pid + QStringLiteral("/fd")));
        const QStringList entries =
            fds.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &fd : entries) {
            const QString target = QFileInfo(fds.filePath(fd)).symLinkTarget();
            if (target.isEmpty() || !paths.contains(target) || holders.contains(target))
                continue;
            QFile comm(proc.filePath(pid + QStringLiteral("/comm")));
            QString name = pid;
            if (comm.open(QIODevice::ReadOnly))
                name = QString::fromUtf8(comm.readAll()).trimmed();
            holders.insert(target, name);
        }
    }
    return holders;
}

void SkippedOnDisk::refresh(const QString &hash,
                            const QList<ContentFile> &files,
                            const QByteArray &pieceStates,
                            qint64 torrentSize,
                            const LocalPathOf &localPathOf)
{
    const quint64 serial = ++m_serial;
    // The worker touches nothing of this object's: it hops back through the application
    // object, and only there is the QPointer read (as App's .torrent parse does).
    QPointer<SkippedOnDisk> self(this);
    const AllocatedReader reader = m_reader;
    QThreadPool::globalInstance()->start(
        [self, serial, hash, files, pieceStates, torrentSize, localPathOf, reader]() {
            const SkippedReading reading =
                read(hash, files, pieceStates, torrentSize, localPathOf, reader);
            QCoreApplication *application = QCoreApplication::instance();
            if (!application)
                return;
            QMetaObject::invokeMethod(application, [self, reading, serial]() {
                if (!self.isNull())
                    self->apply(reading, serial);
            });
        });
}

void SkippedOnDisk::refreshNow(const QString &hash,
                               const QList<ContentFile> &files,
                               const QByteArray &pieceStates,
                               qint64 torrentSize,
                               const LocalPathOf &localPathOf)
{
    const quint64 serial = ++m_serial;
    apply(read(hash, files, pieceStates, torrentSize, localPathOf, m_reader), serial);
}

void SkippedOnDisk::clear()
{
    ++m_serial;
    if (m_reading.hash.isEmpty())
        return;
    m_reading = SkippedReading();
    Q_EMIT readingChanged();
}

void SkippedOnDisk::apply(const SkippedReading &reading, quint64 serial)
{
    if (serial != m_serial)
        return;
    m_reading = reading;
    Q_EMIT readingChanged();
}

} // namespace omaseed::core
