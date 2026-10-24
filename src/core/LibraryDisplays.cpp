#include "core/LibraryDisplays.hpp"

#include "core/TorrentFilterModel.hpp"

namespace omaseed::core {

QStringList LibraryDisplays::names()
{
    return {QStringLiteral("rows"), QStringLiteral("table"), QStringLiteral("overview")};
}

bool LibraryDisplays::isName(const QString &name)
{
    return names().contains(name);
}

QString LibraryDisplays::libraryKey()
{
    return QStringLiteral("library");
}

QString LibraryDisplays::scopeKey(int question,
                                  const QVariant &category,
                                  const QVariant &tag,
                                  const QVariant &tracker,
                                  const QString &folder)
{
    if (!folder.isEmpty())
        return QStringLiteral("folder:") + folder;
    if (category.isValid())
        return QStringLiteral("category:") + category.toString();
    if (tag.isValid())
        return QStringLiteral("tag:") + tag.toString();
    if (tracker.isValid())
        return QStringLiteral("tracker:") + tracker.toString();
    const QString asked = TorrentFilterModel::questionKey(question);
    if (!asked.isEmpty())
        return QStringLiteral("question:") + asked;
    return libraryKey();
}

bool LibraryDisplays::isExistingInstall(const QJsonObject &state, bool fileExists)
{
    if (!fileExists)
        return false;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        if (it.key() != QLatin1String("version"))
            return true;
    }
    return false;
}

QString LibraryDisplays::initialLibrary(bool existingInstall)
{
    return existingInstall ? QStringLiteral("table") : QStringLiteral("rows");
}

LibraryDisplays::LibraryDisplays(const QString &library)
    : m_library(isName(library) ? library : QStringLiteral("rows"))
{}

QString LibraryDisplays::displayFor(const QString &scopeKey) const
{
    if (scopeKey == libraryKey())
        return m_library;
    return m_scopes.value(scopeKey, m_library);
}

bool LibraryDisplays::set(const QString &scopeKey, const QString &display)
{
    if (!isName(display) || scopeKey.isEmpty())
        return false;
    if (scopeKey == libraryKey()) {
        if (m_library == display)
            return false;
        m_library = display;
        return true;
    }
    if (m_scopes.value(scopeKey) == display)
        return false;
    m_scopes.insert(scopeKey, display);
    return true;
}

QJsonObject LibraryDisplays::toJson() const
{
    QJsonObject scopes;
    for (auto it = m_scopes.constBegin(); it != m_scopes.constEnd(); ++it)
        scopes.insert(it.key(), it.value());
    return {{QStringLiteral("library"), m_library}, {QStringLiteral("scopes"), scopes}};
}

LibraryDisplays LibraryDisplays::fromJson(const QJsonObject &json, const QString &fallback)
{
    const QString library = json.value(QStringLiteral("library")).toString();
    LibraryDisplays displays(isName(library) ? library : fallback);
    const QJsonObject scopes = json.value(QStringLiteral("scopes")).toObject();
    for (auto it = scopes.constBegin(); it != scopes.constEnd(); ++it) {
        const QString display = it.value().toString();
        if (isName(display) && it.key() != libraryKey() && !it.key().isEmpty())
            displays.m_scopes.insert(it.key(), display);
    }
    return displays;
}

} // namespace omaseed::core
