#include "core/SearchPlugins.hpp"

#include "core/QbtClient.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QVariantMap>

namespace omaseed::core {

SearchPlugins::SearchPlugins(QObject *parent)
    : QAbstractListModel(parent)
{
    m_installTimer.setInterval(m_watchPollMs);
    m_updateTimer.setInterval(m_watchPollMs);
    connect(&m_installTimer, &QTimer::timeout, this, &SearchPlugins::watchInstall);
    connect(&m_updateTimer, &QTimer::timeout, this, &SearchPlugins::watchUpdate);
}

int SearchPlugins::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_plugins.size());
}

QHash<int, QByteArray> SearchPlugins::roleNames() const
{
    return {{NameRole, "name"},
            {FullNameRole, "fullName"},
            {VersionRole, "version"},
            {UrlRole, "url"},
            {EnabledRole, "pluginOn"},
            {TypesTextRole, "typesText"},
            {CategoriesRole, "categories"},
            {NoteRole, "note"}};
}

QVariant SearchPlugins::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_plugins.size())
        return {};
    const Plugin &plugin = m_plugins.at(index.row());
    switch (role) {
    case NameRole:
        return plugin.name;
    case FullNameRole:
        return plugin.fullName.isEmpty() ? plugin.name : plugin.fullName;
    case VersionRole:
        return plugin.version;
    case UrlRole:
        return plugin.url;
    case EnabledRole:
        return plugin.enabled;
    case TypesTextRole: {
        QStringList words;
        for (const QVariant &category : plugin.categories) {
            const QVariantMap map = category.toMap();
            if (map.value(QStringLiteral("id")).toString() != QLatin1String("all"))
                words.append(map.value(QStringLiteral("name")).toString().toLower());
        }
        return words.join(QStringLiteral(" · "));
    }
    case CategoriesRole: {
        QStringList ids;
        for (const QVariant &category : plugin.categories) {
            const QString id = category.toMap().value(QStringLiteral("id")).toString();
            if (id != QLatin1String("all"))
                ids.append(id);
        }
        return ids;
    }
    case NoteRole:
        return m_notes.value(plugin.name);
    default:
        break;
    }
    return {};
}

void SearchPlugins::setClient(QbtClient *client)
{
    m_client = client;
}

void SearchPlugins::setWatch(int pollMs, int lengthMs)
{
    m_watchPollMs = pollMs;
    m_watchLengthMs = lengthMs;
    m_installTimer.setInterval(pollMs);
    m_updateTimer.setInterval(pollMs);
}

int SearchPlugins::enabledCount() const
{
    return int(std::count_if(
        m_plugins.cbegin(), m_plugins.cend(), [](const Plugin &plugin) { return plugin.enabled; }));
}

QVariantList SearchPlugins::types() const
{
    QVariantList list;
    QStringList seen;
    for (const Plugin &plugin : m_plugins) {
        if (!plugin.enabled)
            continue;
        for (const QVariant &category : plugin.categories) {
            const QVariantMap map = category.toMap();
            const QString id = map.value(QStringLiteral("id")).toString();
            if (id == QLatin1String("all") || seen.contains(id))
                continue;
            seen.append(id);
            list.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), map.value(QStringLiteral("name")).toString().toLower()}});
        }
    }
    return list;
}

void SearchPlugins::refresh()
{
    if (m_client.isNull())
        return;
    const quint64 generation = m_generation;
    m_client->searchPlugins()->then(this, [this, generation](const QbtResult<QJsonArray> &result) {
        if (generation != m_generation || result.isError())
            return;
        apply(result.value());
    });
}

void SearchPlugins::apply(const QJsonArray &list)
{
    beginResetModel();
    m_plugins.clear();
    for (const QJsonValue &value : list) {
        const QJsonObject object = value.toObject();
        Plugin plugin;
        plugin.name = object.value(QStringLiteral("name")).toString();
        plugin.fullName = object.value(QStringLiteral("fullName")).toString();
        plugin.version = object.value(QStringLiteral("version")).toString();
        plugin.url = object.value(QStringLiteral("url")).toString();
        plugin.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        for (const QJsonValue &category :
             object.value(QStringLiteral("supportedCategories")).toArray()) {
            // 4.x listed plain ids; 5.x lists {id, name}.
            if (category.isString())
                plugin.categories.append(
                    QVariantMap{{QStringLiteral("id"), category.toString()},
                                {QStringLiteral("name"), category.toString()}});
            else
                plugin.categories.append(category.toObject().toVariantMap());
        }
        m_plugins.append(plugin);
    }
    std::stable_sort(m_plugins.begin(), m_plugins.end(), [](const Plugin &a, const Plugin &b) {
        const QString left = a.fullName.isEmpty() ? a.name : a.fullName;
        const QString right = b.fullName.isEmpty() ? b.name : b.fullName;
        return QString::compare(left, right, Qt::CaseInsensitive) < 0;
    });
    m_loaded = true;
    endResetModel();
    Q_EMIT countChanged();
}

void SearchPlugins::setEnabled(const QString &name, bool enabled)
{
    if (m_client.isNull())
        return;
    // The switch shows its new state at once (M12); the list confirms it.
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins.at(i).name != name)
            continue;
        m_plugins[i].enabled = enabled;
        Q_EMIT dataChanged(index(i), index(i), {EnabledRole});
        Q_EMIT countChanged();
    }
    m_client->searchEnablePlugin({name}, enabled)
        ->then(this, [this](const QbtResult<QByteArray> &) { refresh(); });
}

void SearchPlugins::uninstall(const QString &name)
{
    if (m_client.isNull())
        return;
    m_notes.remove(name);
    m_client->searchUninstallPlugin({name})->then(
        this, [this](const QbtResult<QByteArray> &) { refresh(); });
}

void SearchPlugins::install(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (m_client.isNull() || trimmed.isEmpty())
        return;
    m_versionsBefore.clear();
    for (const Plugin &plugin : std::as_const(m_plugins))
        m_versionsBefore.insert(plugin.name, plugin.version);
    m_installSource = trimmed;
    m_installName.clear();
    m_installState = QStringLiteral("installing");
    m_installSeconds = 0;
    m_installStartMs = QDateTime::currentMSecsSinceEpoch();
    Q_EMIT installChanged();
    const quint64 generation = m_generation;
    m_client->searchInstallPlugin({trimmed})->then(
        this, [this, generation](const QbtResult<QByteArray> &result) {
            if (generation != m_generation)
                return;
            if (result.isError()) {
                m_installState = QStringLiteral("failed");
                Q_EMIT installChanged();
                return;
            }
            m_installTimer.start();
        });
}

void SearchPlugins::watchInstall()
{
    if (m_client.isNull()) {
        m_installTimer.stop();
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_installStartMs;
    m_installSeconds = int(elapsed / 1000);
    Q_EMIT installChanged();
    const quint64 generation = m_generation;
    m_client->searchPlugins()->then(
        this, [this, generation, elapsed](const QbtResult<QJsonArray> &result) {
            if (generation != m_generation || m_installState != QLatin1String("installing"))
                return;
            if (!result.isError()) {
                apply(result.value());
                for (const Plugin &plugin : std::as_const(m_plugins)) {
                    const auto before = m_versionsBefore.constFind(plugin.name);
                    if (before == m_versionsBefore.cend() || *before != plugin.version) {
                        m_installState = QStringLiteral("installed");
                        m_installName = plugin.fullName.isEmpty() ? plugin.name : plugin.fullName;
                        m_installTimer.stop();
                        Q_EMIT installChanged();
                        return;
                    }
                }
            }
            if (elapsed >= m_watchLengthMs) {
                m_installState = QStringLiteral("failed");
                m_installTimer.stop();
                Q_EMIT installChanged();
            }
        });
}

void SearchPlugins::dismissInstall()
{
    m_installTimer.stop();
    m_installState.clear();
    m_installSource.clear();
    m_installName.clear();
    Q_EMIT installChanged();
}

void SearchPlugins::checkUpdates()
{
    if (m_client.isNull())
        return;
    m_updateState = QStringLiteral("checking");
    m_updateText.clear();
    m_updateStartMs = QDateTime::currentMSecsSinceEpoch();
    m_updateSinceSecs = QDateTime::currentSecsSinceEpoch() - 1;
    Q_EMIT updateChanged();
    const quint64 generation = m_generation;
    m_client->searchUpdatePlugins()->then(this,
                                          [this, generation](const QbtResult<QByteArray> &result) {
                                              if (generation != m_generation)
                                                  return;
                                              if (result.isError()) {
                                                  m_updateState = QStringLiteral("done");
                                                  m_updateText = result.error().message();
                                                  Q_EMIT updateChanged();
                                                  return;
                                              }
                                              m_updateTimer.start();
                                          });
}

void SearchPlugins::watchUpdate()
{
    if (m_client.isNull()) {
        m_updateTimer.stop();
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_updateStartMs;
    const quint64 generation = m_generation;
    m_client->logMain(-1)->then(
        this, [this, generation, elapsed](const QbtResult<QJsonArray> &result) {
            if (generation != m_generation || m_updateState != QLatin1String("checking"))
                return;
            const QString words =
                result.isError() ? QString() : updateWords(result.value(), m_updateSinceSecs);
            if (!words.isEmpty()) {
                m_updateState = QStringLiteral("done");
                m_updateText = words;
                m_updateTimer.stop();
                Q_EMIT updateChanged();
                refresh();
                return;
            }
            if (elapsed >= m_watchLengthMs) {
                m_updateState = QStringLiteral("silent");
                m_updateTimer.stop();
                Q_EMIT updateChanged();
            }
        });
}

QString SearchPlugins::updateWords(const QJsonArray &lines, qint64 sinceSecs)
{
    QStringList said;
    qint64 when = 0;
    for (const QJsonValue &value : lines) {
        const QJsonObject line = value.toObject();
        const qint64 stamp = line.value(QStringLiteral("timestamp")).toInteger();
        if (stamp < sinceSecs)
            continue;
        const QString message = line.value(QStringLiteral("message")).toString();
        const bool aboutPlugins =
            message.contains(QStringLiteral("plugin"), Qt::CaseInsensitive) &&
            (message.contains(QStringLiteral("update"), Qt::CaseInsensitive) ||
             message.contains(QStringLiteral("outdated"), Qt::CaseInsensitive) ||
             message.contains(QStringLiteral("up to date"), Qt::CaseInsensitive));
        if (!aboutPlugins)
            continue;
        said.append(message.trimmed());
        when = stamp;
    }
    if (said.isEmpty())
        return {};
    return QStringLiteral("%1 · %2").arg(
        QDateTime::fromSecsSinceEpoch(when).toString(QStringLiteral("HH:mm")),
        said.join(QStringLiteral(" · ")));
}

void SearchPlugins::setNote(const QString &name, const QString &note)
{
    if (note.isEmpty())
        m_notes.remove(name);
    else
        m_notes.insert(name, note);
    for (int i = 0; i < m_plugins.size(); ++i)
        if (m_plugins.at(i).name == name)
            Q_EMIT dataChanged(index(i), index(i), {NoteRole});
}

void SearchPlugins::clear()
{
    ++m_generation;
    m_installTimer.stop();
    m_updateTimer.stop();
    beginResetModel();
    m_plugins.clear();
    m_notes.clear();
    m_loaded = false;
    endResetModel();
    m_installState.clear();
    m_installSource.clear();
    m_installName.clear();
    m_updateState.clear();
    m_updateText.clear();
    Q_EMIT countChanged();
    Q_EMIT installChanged();
    Q_EMIT updateChanged();
}

QVariantList SearchPlugins::enabledPlugins() const
{
    QVariantList list;
    for (int i = 0; i < m_plugins.size(); ++i) {
        const Plugin &plugin = m_plugins.at(i);
        if (!plugin.enabled)
            continue;
        list.append(QVariantMap{
            {QStringLiteral("name"), plugin.name},
            {QStringLiteral("fullName"), plugin.fullName.isEmpty() ? plugin.name : plugin.fullName},
            {QStringLiteral("categories"), data(index(i), CategoriesRole)}});
    }
    return list;
}

QHash<QString, QString> SearchPlugins::names() const
{
    QHash<QString, QString> names;
    for (const Plugin &plugin : m_plugins)
        names.insert(plugin.name, plugin.fullName.isEmpty() ? plugin.name : plugin.fullName);
    return names;
}

} // namespace omaseed::core
