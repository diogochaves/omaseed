#include "integration/BarWidgetInstaller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>

namespace omaseed::integration::BarWidgetInstaller {

namespace {

bool holdsManifest(const QString &dir)
{
    return !dir.isEmpty() &&
           QFileInfo(QDir(dir).filePath(QStringLiteral("manifest.json"))).isFile();
}

QString configHome()
{
    const QString config =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_CONFIG_HOME"));
    return config.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : config;
}

/// A bar or plugins[] entry's id: the entry itself when it is a bare string, else its "id".
QString entryId(const QJsonValue &entry)
{
    return entry.isObject() ? entry.toObject().value(QStringLiteral("id")).toString()
                            : entry.toString();
}

bool listsId(const QJsonValue &list, const QString &id)
{
    const QJsonArray entries = list.toArray();
    return std::any_of(entries.begin(), entries.end(), [&id](const QJsonValue &entry) {
        return entryId(entry) == id;
    });
}

/// Removes whatever is at `path`: a link (never what it points at), a file or a folder.
bool removeEntry(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || info.isFile())
        return QFile::remove(path);
    if (info.isDir())
        return QDir(path).removeRecursively();
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

    const QStringList dataDirs =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
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
    return QDir(configHome()).filePath(QStringLiteral("omarchy/plugins/") + pluginId());
}

QString shellConfigPath()
{
    return QDir(configHome()).filePath(QStringLiteral("omarchy/shell.json"));
}

QString enableCommand()
{
    return QStringLiteral("omarchy plugin enable ") + pluginId();
}

Presence presence(const QString &target)
{
    const QFileInfo info(target);
    if (info.isSymLink())
        return Presence::Linked;
    if (!info.exists())
        return Presence::Absent;
    if (QFileInfo::exists(QDir(target).filePath(QStringLiteral(".git"))))
        return Presence::Checkout;
    return Presence::Folder;
}

QString linkTarget(const QString &target)
{
    const QFileInfo info(target);
    return info.isSymLink() ? info.symLinkTarget() : QString();
}

bool isInBar(const QString &shellJson)
{
    QFile file(shellJson);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject config = QJsonDocument::fromJson(file.readAll()).object();
    const QString id = pluginId();
    const QJsonObject bar = config.value(QStringLiteral("bar")).toObject();
    if (bar.value(QStringLiteral("id")).toString() == id)
        return true;
    const QJsonObject layout = bar.value(QStringLiteral("layout")).toObject();
    for (const QString &section :
         {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
        if (listsId(layout.value(section), id))
            return true;
    }
    return listsId(config.value(QStringLiteral("plugins")), id);
}

Result link(const QString &source, const QString &target)
{
    Result result;
    if (!holdsManifest(source)) {
        result.message = source.isEmpty()
                             ? QStringLiteral("omaseed's bar widget is not installed here (no "
                                              "omaseed/plugin in the data directories)")
                             : QStringLiteral("%1 holds no manifest.json").arg(source);
        return result;
    }
    const Presence before = presence(target);
    if (before == Presence::Checkout) {
        result.message = QStringLiteral("%1 was added with `omarchy plugin add`; it updates with "
                                        "`omarchy plugin update %2`")
                             .arg(target, pluginId());
        return result;
    }
    const QString absoluteSource = QDir(source).absolutePath();
    if (before == Presence::Linked && linkTarget(target) == absoluteSource) {
        result.ok = true;
        result.message = QStringLiteral("%1 already points at %2").arg(target, absoluteSource);
        return result;
    }

    const QFileInfo targetInfo(target);
    const QString parent = targetInfo.absolutePath();
    if (!QDir().mkpath(parent)) {
        result.message = QStringLiteral("cannot create %1").arg(parent);
        return result;
    }
    // The shell watches the plugins folder and loads every folder in it, so the helpers have
    // hidden names, as `omarchy plugin add` stages its clone (`.add.tmp.N`).
    const QString staging =
        QDir(parent).filePath(QStringLiteral(".%1.new").arg(targetInfo.fileName()));
    const QString previous =
        QDir(parent).filePath(QStringLiteral(".%1.old").arg(targetInfo.fileName()));
    removeEntry(staging);
    removeEntry(previous);
    if (!QFile::link(absoluteSource, staging)) {
        result.message = QStringLiteral("cannot create the link %1").arg(staging);
        return result;
    }
    // A link is swapped in one rename. A folder cannot be renamed over, so it moves aside
    // first and goes once the link is in place.
    if (before == Presence::Folder && !QDir().rename(target, previous)) {
        removeEntry(staging);
        result.message = QStringLiteral("cannot move the old copy at %1 aside").arg(target);
        return result;
    }
    if (std::rename(QFile::encodeName(staging).constData(),
                    QFile::encodeName(target).constData()) != 0) {
        removeEntry(staging);
        if (before == Presence::Folder)
            QDir().rename(previous, target);
        result.message = QStringLiteral("cannot put the link at %1").arg(target);
        return result;
    }
    removeEntry(previous);

    result.ok = true;
    result.changed = true;
    result.message = QStringLiteral("%1 now points at %2").arg(target, absoluteSource);
    return result;
}

Result unlink(const QString &target)
{
    Result result;
    switch (presence(target)) {
    case Presence::Absent:
        result.ok = true;
        result.message = QStringLiteral("%1 is not there").arg(target);
        return result;
    case Presence::Linked:
        if (!QFile::remove(target)) {
            result.message = QStringLiteral("cannot remove %1").arg(target);
            return result;
        }
        result.ok = true;
        result.message = QStringLiteral("removed %1").arg(target);
        return result;
    case Presence::Folder:
    case Presence::Checkout:
        break;
    }
    result.message = QStringLiteral("%1 was put there another way; remove it with `omarchy "
                                    "plugin remove %2`")
                         .arg(target, pluginId());
    return result;
}

} // namespace omaseed::integration::BarWidgetInstaller
