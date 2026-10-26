#include "core/LooseCarry.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <QSet>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("LooseCarry", text);
}

constexpr qint64 kChunk = 1024 * 1024;
constexpr qint64 kProgressEvery = 64 * kChunk;

QByteArray local(const QString &path)
{
    return QFile::encodeName(path);
}

QString errorText(int error)
{
    return QString::fromLocal8Bit(std::strerror(error));
}

/// A rename that never replaces. 0, or the errno.
int renameNoReplace(const QString &from, const QString &to)
{
    if (::renameat2(
            AT_FDCWD, local(from).constData(), AT_FDCWD, local(to).constData(), RENAME_NOREPLACE) ==
        0)
        return 0;
    return errno;
}

/// Puts the checked temporary copy in place without replacing anything: renameat2 where the
/// filesystem has it, else a hard link (fails when the name exists), else a check and a rename.
int placeNoReplace(const QString &temp, const QString &target)
{
    const int renamed = renameNoReplace(temp, target);
    if (renamed == 0 || renamed == EEXIST)
        return renamed;
    if (::link(local(temp).constData(), local(target).constData()) == 0) {
        ::unlink(local(temp).constData());
        return 0;
    }
    if (errno == EEXIST)
        return EEXIST;
    struct stat there{};
    if (::lstat(local(target).constData(), &there) == 0)
        return EEXIST;
    if (::rename(local(temp).constData(), local(target).constData()) == 0)
        return 0;
    return errno;
}

/// The hash of a whole file, or empty when it can't be read.
QByteArray hashOf(const QString &path, const std::function<bool()> &stop, bool *stopped)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer;
    while (!file.atEnd()) {
        if (stop && stop()) {
            *stopped = true;
            return {};
        }
        buffer = file.read(kChunk);
        if (buffer.isEmpty() && file.error() != QFileDevice::NoError)
            return {};
        hash.addData(buffer);
    }
    return hash.result();
}

bool sameAs(const struct stat &a, const CarryFile &file)
{
    const qint64 ms = qint64(a.st_mtim.tv_sec) * 1000 + a.st_mtim.tv_nsec / 1000000;
    return qint64(a.st_size) == file.size &&
           (!file.modified.isValid() || ms == file.modified.toMSecsSinceEpoch());
}

struct Copied
{
    enum class How { Done, NoRoom, Changed, Stopped, Failed } how = How::Failed;
    QString why;
};

/// Copies `source` to `temp`, hashing it as it reads; checks the source didn't change under
/// it; reads the copy back and compares; gives the copy the source's time and mode.
Copied copyChecked(const QString &source,
                   const QString &temp,
                   const struct stat &before,
                   const std::function<bool()> &stop,
                   const std::function<void(qint64, bool)> &advanced)
{
    Copied out;
    const int in = ::open(local(source).constData(), O_RDONLY | O_CLOEXEC);
    if (in < 0) {
        out.why = tr("can't read it: %1").arg(errorText(errno));
        return out;
    }
    const int fd = ::open(local(temp).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        const int error = errno;
        ::close(in);
        out.how = error == ENOSPC || error == EDQUOT ? Copied::How::NoRoom : Copied::How::Failed;
        out.why = tr("can't write there: %1").arg(errorText(error));
        return out;
    }
    auto drop = [&](Copied::How how, const QString &why) {
        ::close(fd);
        ::unlink(local(temp).constData());
        out.how = how;
        out.why = why;
        return out;
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunk, Qt::Uninitialized);
    qint64 copied = 0;
    qint64 told = 0;
    for (;;) {
        if (stop && stop()) {
            ::close(in);
            return drop(Copied::How::Stopped, {});
        }
        const ssize_t got = ::read(in, buffer.data(), size_t(buffer.size()));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            const int error = errno;
            ::close(in);
            return drop(Copied::How::Failed, tr("can't read it: %1").arg(errorText(error)));
        }
        if (got == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), got));
        ssize_t written = 0;
        while (written < got) {
            const ssize_t put = ::write(fd, buffer.constData() + written, size_t(got - written));
            if (put < 0) {
                if (errno == EINTR)
                    continue;
                const int error = errno;
                ::close(in);
                if (error == ENOSPC || error == EDQUOT)
                    return drop(Copied::How::NoRoom, tr("the destination is full"));
                return drop(Copied::How::Failed, tr("can't write there: %1").arg(errorText(error)));
            }
            written += put;
        }
        copied += got;
        if (copied - told >= kProgressEvery) {
            advanced(copied - told, false);
            told = copied;
        }
    }
    struct stat after{};
    const bool statted = ::fstat(in, &after) == 0;
    ::close(in);
    if (::fsync(fd) != 0) {
        const int error = errno;
        if (error == ENOSPC || error == EDQUOT)
            return drop(Copied::How::NoRoom, tr("the destination is full"));
        return drop(Copied::How::Failed, tr("can't write there: %1").arg(errorText(error)));
    }
    if (!statted || after.st_size != before.st_size ||
        after.st_mtim.tv_sec != before.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != before.st_mtim.tv_nsec || copied != qint64(before.st_size))
        return drop(Copied::How::Changed, tr("changed while it was copied"));

    ::fchmod(fd, before.st_mode & 07777);
    const struct timespec times[2] = {before.st_atim, before.st_mtim};
    ::futimens(fd, times);
    if (::close(fd) != 0) {
        ::unlink(local(temp).constData());
        out.how = Copied::How::Failed;
        out.why = tr("can't write there: %1").arg(errorText(errno));
        return out;
    }

    advanced(copied - told, true);
    bool stopped = false;
    const QByteArray back = hashOf(temp, stop, &stopped);
    if (stopped) {
        ::unlink(local(temp).constData());
        out.how = Copied::How::Stopped;
        return out;
    }
    if (back.isEmpty() || back != hash.result()) {
        ::unlink(local(temp).constData());
        out.how = Copied::How::Failed;
        out.why = tr("the copy read back different");
        return out;
    }
    out.how = Copied::How::Done;
    return out;
}

} // namespace

QString LooseCarry::tempPathFor(const QString &target)
{
    const QFileInfo info(target);
    return info.path() + QStringLiteral("/.") + info.fileName() + QStringLiteral(".omaseed-carry");
}

LooseCarry::Result LooseCarry::run(const QString &from,
                                   const QString &to,
                                   QList<CarryFile> files,
                                   const QDateTime &now,
                                   std::chrono::seconds minAge,
                                   const std::function<bool()> &stop,
                                   const std::function<void(const Progress &)> &progress)
{
    Progress state;
    state.total = int(files.size());
    for (const CarryFile &file : std::as_const(files)) {
        if (file.outcome == CarryFile::Outcome::ToDo || file.carried())
            state.bytesTotal += file.size;
        if (file.outcome != CarryFile::Outcome::ToDo) {
            ++state.done;
            if (file.carried())
                state.bytesDone += file.size;
        }
    }
    auto tell = [&]() {
        if (progress)
            progress(state);
    };

    const QString fromRoot = QDir::cleanPath(from);
    const QString toRoot = QDir::cleanPath(to);
    QSet<QString> emptied; // source folders that lost a file
    bool full = false;
    bool stopped = false;

    for (CarryFile &file : files) {
        if (file.outcome != CarryFile::Outcome::ToDo)
            continue;
        if (full) {
            file.outcome = CarryFile::Outcome::NoRoom;
            file.why = tr("the destination is full");
            ++state.done;
            continue;
        }
        if (stop && stop()) {
            stopped = true;
            break;
        }
        const QString source = fromRoot + QLatin1Char('/') + file.relative;
        const QString target = toRoot + QLatin1Char('/') + file.relative;
        const QString temp = tempPathFor(target);
        state.current = file.relative;
        state.checking = false;
        tell();

        auto settle = [&](CarryFile::Outcome outcome, const QString &why) {
            file.outcome = outcome;
            file.why = why;
            ++state.done;
            if (file.carried()) {
                state.bytesDone += file.size;
                emptied.insert(QFileInfo(source).path());
            }
        };

        // An earlier run stopped mid-copy: its temporary copy is ours to remove.
        ::unlink(local(temp).constData());

        struct stat here{};
        struct stat there{};
        const bool hasSource = ::lstat(local(source).constData(), &here) == 0;
        const bool hasTarget = ::lstat(local(target).constData(), &there) == 0;
        if (!hasSource) {
            if (hasTarget && S_ISREG(there.st_mode) && qint64(there.st_size) == file.size)
                settle(CarryFile::Outcome::Carried, tr("carried earlier"));
            else
                settle(CarryFile::Outcome::Gone, tr("gone since the dry run"));
            continue;
        }
        if (!S_ISREG(here.st_mode)) {
            settle(CarryFile::Outcome::Changed, tr("not a plain file any more"));
            continue;
        }
        if (!sameAs(here, file)) {
            settle(CarryFile::Outcome::Changed, tr("changed since the dry run"));
            continue;
        }
        const qint64 modifiedMs =
            qint64(here.st_mtim.tv_sec) * 1000 + here.st_mtim.tv_nsec / 1000000;
        if (now.toMSecsSinceEpoch() - modifiedMs < qint64(minAge.count()) * 1000) {
            settle(CarryFile::Outcome::Changed,
                   tr("changed in the last %1 min").arg(minAge.count() / 60));
            continue;
        }

        if (hasTarget) {
            bool halted = false;
            bool same = false;
            if (S_ISREG(there.st_mode) && there.st_size == here.st_size) {
                const QByteArray mine = hashOf(source, stop, &halted);
                const QByteArray theirs = halted ? QByteArray() : hashOf(target, stop, &halted);
                same = !halted && !mine.isEmpty() && mine == theirs;
            }
            if (halted) {
                stopped = true;
                break;
            }
            if (!same) {
                settle(CarryFile::Outcome::Different,
                       tr("a different file is already at %1").arg(target));
                continue;
            }
            if (::unlink(local(source).constData()) != 0) {
                settle(CarryFile::Outcome::Kept,
                       tr("already at the target, but can't be removed here: %1")
                           .arg(errorText(errno)));
                continue;
            }
            settle(CarryFile::Outcome::AlreadyThere, tr("the same file was already there"));
            continue;
        }

        if (!QDir().mkpath(QFileInfo(target).path())) {
            settle(CarryFile::Outcome::Failed, tr("can't create %1").arg(QFileInfo(target).path()));
            continue;
        }

        // Same drive: a rename, which never replaces.
        const int renamed = renameNoReplace(source, target);
        if (renamed == 0) {
            settle(CarryFile::Outcome::Carried, tr("renamed"));
            continue;
        }
        if (renamed == EEXIST) {
            settle(CarryFile::Outcome::Different,
                   tr("a different file is already at %1").arg(target));
            continue;
        }
        if (renamed != EXDEV && renamed != EINVAL && renamed != ENOSYS && renamed != EPERM) {
            settle(CarryFile::Outcome::Failed, tr("can't move it: %1").arg(errorText(renamed)));
            continue;
        }

        // Another drive: copy, check, place, then remove.
        const qint64 base = state.bytesDone;
        const Copied copied =
            copyChecked(source, temp, here, stop, [&](qint64 more, bool checking) {
                state.bytesDone += more;
                state.checking = checking;
                tell();
            });
        state.bytesDone = base;
        state.checking = false;
        if (copied.how == Copied::How::Stopped) {
            stopped = true;
            break;
        }
        if (copied.how == Copied::How::NoRoom) {
            settle(CarryFile::Outcome::NoRoom, copied.why);
            full = true;
            continue;
        }
        if (copied.how == Copied::How::Changed) {
            settle(CarryFile::Outcome::Changed, copied.why);
            continue;
        }
        if (copied.how == Copied::How::Failed) {
            settle(CarryFile::Outcome::Failed, copied.why);
            continue;
        }
        const int placed = placeNoReplace(temp, target);
        if (placed != 0) {
            ::unlink(local(temp).constData());
            if (placed == EEXIST)
                settle(CarryFile::Outcome::Different,
                       tr("a different file is already at %1").arg(target));
            else
                settle(CarryFile::Outcome::Failed,
                       tr("can't put it in place: %1").arg(errorText(placed)));
            continue;
        }
        if (::unlink(local(source).constData()) != 0) {
            settle(CarryFile::Outcome::Kept,
                   tr("copied and checked, but can't be removed here: %1").arg(errorText(errno)));
            continue;
        }
        settle(CarryFile::Outcome::Carried, tr("copied and checked"));
    }
    state.current.clear();
    tell();

    Result result = tally(files);
    result.files = files;
    result.full = full;
    result.stopped = stopped;

    // Folders the carry left empty at the source go, deepest first, never the folder itself.
    QStringList folders;
    for (const QString &folder : std::as_const(emptied)) {
        QString at = QDir::cleanPath(folder);
        while (at.startsWith(fromRoot + QLatin1Char('/'))) {
            if (!folders.contains(at))
                folders << at;
            at = QFileInfo(at).path();
        }
    }
    std::sort(folders.begin(), folders.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char('/')) > b.count(QLatin1Char('/'));
    });
    for (const QString &folder : std::as_const(folders)) {
        if (::rmdir(local(folder).constData()) == 0)
            ++result.foldersRemoved;
    }
    return result;
}

LooseCarry::Result LooseCarry::tally(const QList<CarryFile> &files)
{
    Result result;
    result.files = files;
    for (const CarryFile &file : files) {
        if (file.outcome == CarryFile::Outcome::ToDo)
            continue;
        if (file.carried()) {
            ++result.carried;
            result.bytesCarried += file.size;
        } else {
            ++result.stayed;
        }
        if (file.outcome == CarryFile::Outcome::NoRoom)
            result.full = true;
    }
    return result;
}

namespace {

/// "17:02" today, "20 Oct 17:02" another day (C18 D7's words for a time).
QString whenOf(const QDateTime &at, const QDateTime &now)
{
    const QDateTime here = at.toLocalTime();
    const QString time = QLocale().toString(here.time(), QLocale::ShortFormat);
    if (here.date() == now.toLocalTime().date())
        return time;
    return QLocale().toString(here.date(), QStringLiteral("d MMM")) + QLatin1Char(' ') + time;
}

QDateTime modifiedOf(const struct stat &st)
{
    return QDateTime::fromMSecsSinceEpoch(qint64(st.st_mtim.tv_sec) * 1000 +
                                          st.st_mtim.tv_nsec / 1000000);
}

} // namespace

QList<CarryFile> LooseCarry::asCarried(const QString &at, const QList<CarryFile> &files)
{
    const QString root = QDir::cleanPath(at);
    QList<CarryFile> out;
    for (const CarryFile &file : files) {
        if (!file.carried())
            continue;
        CarryFile kept;
        kept.relative = file.relative;
        kept.size = file.size;
        kept.modified = file.modified;
        struct stat st{};
        const QString path = root + QLatin1Char('/') + file.relative;
        if (::lstat(local(path).constData(), &st) == 0 && S_ISREG(st.st_mode)) {
            kept.size = qint64(st.st_size);
            kept.modified = modifiedOf(st);
        }
        out << kept;
    }
    return out;
}

QList<CarryFile>
LooseCarry::backList(const QString &at, const QList<CarryFile> &carried, const QDateTime &now)
{
    const QString root = QDir::cleanPath(at);
    QList<CarryFile> out;
    for (const CarryFile &file : carried) {
        CarryFile item = file;
        item.outcome = CarryFile::Outcome::ToDo;
        item.why.clear();
        struct stat st{};
        const QString path = root + QLatin1Char('/') + file.relative;
        if (::lstat(local(path).constData(), &st) != 0) {
            item.outcome = CarryFile::Outcome::Gone;
            item.why = tr("no longer there");
        } else if (!S_ISREG(st.st_mode) || !sameAs(st, file)) {
            item.outcome = CarryFile::Outcome::Changed;
            item.why = tr("changed at %1, after the move").arg(whenOf(modifiedOf(st), now));
        }
        out << item;
    }
    return out;
}

namespace {

const char *const kOutcomes[] = {"toDo",
                                 "carried",
                                 "alreadyThere",
                                 "different",
                                 "changed",
                                 "recent",
                                 "unknown",
                                 "gone",
                                 "noRoom",
                                 "failed",
                                 "kept"};

} // namespace

QJsonArray LooseCarry::toJson(const QList<CarryFile> &files)
{
    QJsonArray array;
    for (const CarryFile &file : files) {
        array.append(QJsonObject{
            {QStringLiteral("relative"), file.relative},
            {QStringLiteral("size"), double(file.size)},
            {QStringLiteral("modified"),
             file.modified.isValid() ? double(file.modified.toMSecsSinceEpoch()) : -1.0},
            {QStringLiteral("outcome"), QString::fromLatin1(kOutcomes[int(file.outcome)])},
            {QStringLiteral("why"), file.why},
        });
    }
    return array;
}

QList<CarryFile> LooseCarry::fromJson(const QJsonArray &array)
{
    QList<CarryFile> files;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        CarryFile file;
        file.relative = object.value(QStringLiteral("relative")).toString();
        if (file.relative.isEmpty())
            continue;
        file.size = qint64(object.value(QStringLiteral("size")).toDouble());
        const qint64 ms = qint64(object.value(QStringLiteral("modified")).toDouble(-1));
        if (ms >= 0)
            file.modified = QDateTime::fromMSecsSinceEpoch(ms);
        const QString outcome = object.value(QStringLiteral("outcome")).toString();
        for (int i = 0; i < int(std::size(kOutcomes)); ++i) {
            if (outcome == QLatin1String(kOutcomes[i]))
                file.outcome = CarryFile::Outcome(i);
        }
        file.why = object.value(QStringLiteral("why")).toString();
        files << file;
    }
    return files;
}

} // namespace omaseed::core
