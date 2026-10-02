#include "integration/BarWidgetInstaller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace omaseed::integration::BarWidgetInstaller {

namespace {

constexpr QFileDevice::Permissions kFileMode = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                               | QFileDevice::ReadGroup | QFileDevice::ReadOther;
constexpr QFileDevice::Permissions kDirMode = kFileMode | QFileDevice::ExeOwner
                                              | QFileDevice::ExeGroup | QFileDevice::ExeOther;

bool holdsManifest(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo(QDir(dir).filePath(QStringLiteral("manifest.json"))).isFile();
}

/// Copies the regular files under `source` into the empty folder `into`; false with `error`
/// set at the first failure.
bool copyTree(const QString &source, const QString &into, QString *error)
{
    const QDir from(source);
    QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = from.relativeFilePath(path);
        // A checkout's own bookkeeping is not part of the plugin.
        if (relative.startsWith(QLatin1String(".git/")) || relative == QLatin1String(".git"))
            continue;
        const QString target = QDir(into).filePath(relative);
        const QString parent = QFileInfo(target).absolutePath();
        if (!QDir().mkpath(parent)) {
            *error = QStringLiteral("cannot create %1").arg(parent);
            return false;
        }
        QFile::setPermissions(parent, kDirMode);
        // QFile::copy reads through a symlink, so the copy is always a regular file.
        if (!QFile::copy(path, target)) {
            *error = QStringLiteral("cannot copy %1 to %2").arg(path, target);
            return false;
        }
        QFile::setPermissions(target, kFileMode);
    }
    return true;
}

} // namespace

QString pluginId()
{
    return QStringLiteral("chaves.omaseed");
}

QString findSource(const QString &devSourceDir)
{
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("OMASEED_PLUGIN_DIR"));
    if (holdsManifest(fromEnv))
        return QDir(fromEnv).absolutePath();

    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dir : dataDirs) {
        const QString candidate = QDir(dir).filePath(QStringLiteral("omaseed/plugin"));
        if (holdsManifest(candidate))
            return candidate;
    }

    if (QCoreApplication::instance()) {
        const QString besideBinary = QDir(QCoreApplication::applicationDirPath())
                                         .filePath(QStringLiteral("../share/omaseed/plugin"));
        if (holdsManifest(besideBinary))
            return QDir(besideBinary).absolutePath();
    }

    if (holdsManifest(devSourceDir))
        return QDir(devSourceDir).absolutePath();
    return QString();
}

QString targetDir()
{
    QString config =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_CONFIG_HOME"));
    if (config.isEmpty())
        config = QDir::home().filePath(QStringLiteral(".config"));
    return QDir(config).filePath(QStringLiteral("omarchy/plugins/") + pluginId());
}

QString enableCommand()
{
    return QStringLiteral("omarchy plugin enable ") + pluginId();
}

Result install(const QString &source, const QString &target)
{
    Result result;
    if (!holdsManifest(source)) {
        result.message = source.isEmpty()
                             ? QStringLiteral("omaseed's bar widget is not installed here (no "
                                              "omaseed/plugin in the data directories)")
                             : QStringLiteral("%1 holds no manifest.json").arg(source);
        return result;
    }
    if (QFileInfo(QDir(target).filePath(QStringLiteral(".git"))).exists()) {
        result.message = QStringLiteral("%1 was added with `omarchy plugin add`; update it with "
                                        "`omarchy plugin update %2` instead")
                             .arg(target, pluginId());
        return result;
    }

    const QFileInfo targetInfo(target);
    const QString parent = targetInfo.absolutePath();
    if (!QDir().mkpath(parent)) {
        result.message = QStringLiteral("cannot create %1").arg(parent);
        return result;
    }

    // Copy beside the target, then swap: a failed copy never leaves half a plugin behind. The
    // shell watches the plugins folder and loads every folder in it, so both helpers are hidden
    // names, as `omarchy plugin add` stages its clone (`.add.tmp.N`).
    const QString staging = QDir(parent).filePath(QStringLiteral(".%1.new").arg(targetInfo.fileName()));
    const QString previous = QDir(parent).filePath(QStringLiteral(".%1.old").arg(targetInfo.fileName()));
    QDir(staging).removeRecursively();
    QDir(previous).removeRecursively();
    QString error;
    if (!QDir().mkpath(staging) || !copyTree(source, staging, &error)) {
        QDir(staging).removeRecursively();
        result.message = error.isEmpty() ? QStringLiteral("cannot create %1").arg(staging) : error;
        return result;
    }
    QFile::setPermissions(staging, kDirMode);

    const bool replacing = targetInfo.exists() || targetInfo.isSymLink();
    if (targetInfo.isSymLink() || targetInfo.isFile()) {
        QFile::remove(target);
    } else if (replacing && !QDir().rename(target, previous)) {
        QDir(staging).removeRecursively();
        result.message = QStringLiteral("cannot move the old copy at %1 aside").arg(target);
        return result;
    }
    if (!QDir().rename(staging, target)) {
        if (QFileInfo::exists(previous))
            QDir().rename(previous, target);
        QDir(staging).removeRecursively();
        result.message = QStringLiteral("cannot put the new copy at %1").arg(target);
        return result;
    }
    QDir(previous).removeRecursively();

    result.ok = true;
    result.message = QStringLiteral("%1 the omaseed bar widget at %2\nTurn it on with:\n  %3")
                         .arg(replacing ? QStringLiteral("Updated") : QStringLiteral("Installed"),
                              target,
                              enableCommand());
    return result;
}

} // namespace omaseed::integration::BarWidgetInstaller
