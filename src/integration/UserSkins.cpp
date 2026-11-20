#include "integration/UserSkins.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>

#include <algorithm>

Q_LOGGING_CATEGORY(lcSkins, "omaseed.skins")

namespace omaseed::integration {

UserSkins::UserSkins(QString dir, QObject *parent)
    : QObject(parent)
    , m_dir(std::move(dir))
{}

QString UserSkins::nameProblem(const QString &name)
{
    // C37 U1: a name an agent and a shell both handle without quoting; dots are fine
    // (`warm.v2`), a leading one hides the folder, a trailing one reads as a typo.
    static const QRegularExpression rule(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (name.startsWith(QLatin1Char('.')))
        return QStringLiteral("hidden");
    if (name.size() > 63)
        return QStringLiteral("longer than 63 characters");
    if (!rule.match(name).hasMatch())
        return QStringLiteral("a skin's name is letters, digits, . _ and -");
    if (name.endsWith(QLatin1Char('.')))
        return QStringLiteral("a skin's name doesn't end in .");
    if (name.compare(QLatin1String("desktop"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("desktop is Follow the desktop");
    for (const QString &builtIn : SkinReader::builtInNames())
        if (name.compare(builtIn, Qt::CaseInsensitive) == 0)
            return QStringLiteral("named like a built-in skin");
    return {};
}

Skin UserSkins::readFolder(const QString &path, const QString &name)
{
    QFile file(path + QStringLiteral("/skin.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        Skin missing = SkinReader::builtIn(QStringLiteral("omarchy"));
        missing.reason = QStringLiteral("missing");
        missing.version = 0;
        return missing;
    }
    Skin skin = SkinReader::parse(file.readAll());
    if (!skin.drawn())
        return skin;
    skin.name = name; // C37 U1: the folder names the skin, never the file's "skin"
    if (QFileInfo(path + QStringLiteral("/faces")).exists()) {
        const QString token = QStringLiteral("faces/");
        skin.dropped << token;
        skin.drops << SkinDrop{
            token,
            QJsonValue(QJsonValue::Null),
            QStringLiteral("files in faces/ never load: a face names a built-in skin (C37 U2)")};
    }
    return skin;
}

QList<UserSkin> UserSkins::list() const
{
    QList<UserSkin> skins;
    const QDir dir(m_dir);
    const auto entries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (const QString problem = nameProblem(name); !problem.isEmpty()) {
            qCInfo(lcSkins).noquote() << "skins: left out" << name << "-" << problem;
            continue;
        }
        const Skin skin = readFolder(entry.filePath(), name);
        skins << UserSkin{name, entry.filePath(), skin, skin.description};
    }
    std::sort(skins.begin(), skins.end(), [](const UserSkin &a, const UserSkin &b) {
        const int order = a.name.compare(b.name, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.name < b.name;
    });
    return skins;
}

Skin UserSkins::read(const QString &name) const
{
    if (SkinReader::builtInNames().contains(name))
        return SkinReader::builtIn(name);
    if (nameProblem(name).isEmpty()) {
        const QString path = m_dir + QLatin1Char('/') + name;
        if (QFileInfo(path).isDir())
            return readFolder(path, name);
    }
    Skin missing = SkinReader::builtIn(QStringLiteral("omarchy"));
    missing.reason = QStringLiteral("missing");
    missing.version = 0;
    return missing;
}

} // namespace omaseed::integration
