#include "core/StateFile.hpp"

#include "core/Paths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace omaseed::core {

QString StateFile::path()
{
    return paths::configDir() + QStringLiteral("/state.json");
}

QJsonObject StateFile::read()
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject{{QStringLiteral("version"), 1}};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return QJsonObject{{QStringLiteral("version"), 1}};

    return doc.object();
}

void StateFile::write(const QJsonObject &state)
{
    const QString filePath = path();
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    // Written aside and renamed into place: a crash or a full disk mid-write leaves the old
    // file whole instead of a truncated one that read() would turn into an empty state.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
    file.commit();
}

QJsonObject StateFile::readSection(const QString &key)
{
    return read().value(key).toObject();
}

void StateFile::writeSection(const QString &key, const QJsonObject &value)
{
    QJsonObject state = read();
    state.insert(key, value);
    write(state);
}

} // namespace omaseed::core
