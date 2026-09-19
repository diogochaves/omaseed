#include "core/StateFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace omaseed::core {

QString StateFile::path()
{
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty())
        configHome = QDir::homePath() + QStringLiteral("/.config");
    return configHome + QStringLiteral("/omaseed/state.json");
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

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
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
