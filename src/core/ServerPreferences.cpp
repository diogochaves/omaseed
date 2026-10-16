#include "core/ServerPreferences.hpp"

#include "core/QbtClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>

namespace omaseed::core {

ServerPreferences::ServerPreferences(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{}

bool ServerPreferences::sameValue(const QVariant &a, const QVariant &b)
{
    const QJsonValue left = QJsonValue::fromVariant(a);
    const QJsonValue right = QJsonValue::fromVariant(b);
    if (left.isDouble() && right.isDouble())
        return left.toDouble() == right.toDouble();
    return left == right;
}

void ServerPreferences::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT stateChanged();
}

void ServerPreferences::reset()
{
    ++m_generation;
    m_reads = 0;
    m_offline = false;
    const bool had = !m_values.isEmpty();
    m_values.clear();
    const bool hadPending = !m_pending.isEmpty();
    m_pending.clear();
    m_interfaces.clear();
    m_addresses.clear();
    m_loadError.clear();
    m_loaded = false;
    m_loading = false;
    Q_EMIT stateChanged();
    if (had)
        Q_EMIT valuesChanged();
    if (hadPending)
        Q_EMIT pendingChanged();
    Q_EMIT interfacesChanged();
}

void ServerPreferences::setOffline(const QVariantMap &values)
{
    reset();
    m_offline = true;
    m_values = values;
    m_loaded = true;
    Q_EMIT stateChanged();
    Q_EMIT valuesChanged();
}

void ServerPreferences::reload()
{
    if (m_offline)
        return;
    readBack({});
}

void ServerPreferences::readBack(const QVariantMap &sent)
{
    if (m_client == nullptr)
        return;
    const quint64 generation = m_generation;
    ++m_reads;
    setLoading(true);
    m_client->appPreferences()->then(
        this, [this, generation, sent](const Result<QJsonObject> &result) {
            if (generation != m_generation)
                return; // another server took over meanwhile
            if (--m_reads == 0)
                setLoading(false);
            if (result.isError()) {
                m_loadError = result.error().toString();
                Q_EMIT stateChanged();
                if (!sent.isEmpty())
                    Q_EMIT applied(sent.keys(), {});
                return;
            }
            QVariantMap fresh = result.value().toVariantMap();
            // A change still on its way keeps showing what was asked for.
            for (auto it = m_pending.constBegin(); it != m_pending.constEnd(); ++it) {
                if (m_values.contains(it.key()))
                    fresh.insert(it.key(), m_values.value(it.key()));
            }
            const bool changed = fresh != m_values;
            m_values = fresh;
            const bool firstLoad = !m_loaded;
            m_loaded = true;
            const bool hadError = !m_loadError.isEmpty();
            m_loadError.clear();
            if (firstLoad || hadError)
                Q_EMIT stateChanged();
            if (changed)
                Q_EMIT valuesChanged();
            if (sent.isEmpty())
                return;
            QStringList kept;
            for (auto it = sent.constBegin(); it != sent.constEnd(); ++it) {
                // Write-only keys (a password) never come back, so they cannot be checked.
                if (m_values.contains(it.key()) && !sameValue(m_values.value(it.key()), it.value()))
                    kept.append(it.key());
            }
            Q_EMIT applied(sent.keys(), kept);
        });
}

void ServerPreferences::apply(const QVariantMap &changes)
{
    if (changes.isEmpty())
        return;
    if (m_offline) {
        QVariantMap before;
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
            before.insert(it.key(), m_values.value(it.key()));
            if (m_values.contains(it.key()))
                m_values.insert(it.key(), it.value());
        }
        Q_EMIT valuesChanged();
        Q_EMIT posted(changes, before);
        Q_EMIT applied(changes.keys(), {});
        return;
    }
    if (m_client == nullptr)
        return;
    const quint64 generation = m_generation;

    // Show the change at once, remembering what each key held for a refusal.
    QVariantMap before;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        before.insert(it.key(),
                      m_pending.contains(it.key()) ? m_pending.value(it.key())
                                                   : m_values.value(it.key()));
        m_pending.insert(it.key(), before.value(it.key()));
        if (m_values.contains(it.key()))
            m_values.insert(it.key(), it.value());
    }
    Q_EMIT valuesChanged();
    Q_EMIT pendingChanged();

    m_client->appSetPreferences(QJsonObject::fromVariantMap(changes))
        ->then(this, [this, generation, changes, before](const Result<QByteArray> &result) {
            if (generation != m_generation)
                return;
            for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
                m_pending.remove(it.key());
            Q_EMIT pendingChanged();
            if (result.isError()) {
                for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
                    if (m_values.contains(it.key()))
                        m_values.insert(it.key(), it.value());
                }
                Q_EMIT valuesChanged();
                Q_EMIT applyFailed(changes.keys(), result.error().toString());
                return;
            }
            Q_EMIT posted(changes, before);
            readBack(changes);
        });
}

void ServerPreferences::loadInterfaces(const QString &iface)
{
    if (m_offline) {
        m_interfaces = {QVariantMap{{QStringLiteral("name"), QStringLiteral("eth0")},
                                    {QStringLiteral("value"), QStringLiteral("eth0")}},
                        QVariantMap{{QStringLiteral("name"), QStringLiteral("wg0")},
                                    {QStringLiteral("value"), QStringLiteral("wg0")}}};
        m_addresses = {QStringLiteral("192.168.1.20")};
        Q_UNUSED(iface)
        Q_EMIT interfacesChanged();
        return;
    }
    if (m_client == nullptr)
        return;
    const quint64 generation = m_generation;
    m_client->get(QStringLiteral("app/networkInterfaceList"))
        ->then(this, [this, generation](const Result<QByteArray> &result) {
            if (generation != m_generation || result.isError())
                return;
            QVariantList list;
            const QJsonArray array = QJsonDocument::fromJson(result.value()).array();
            for (const QJsonValue &entry : array) {
                const QJsonObject object = entry.toObject();
                list.append(QVariantMap{
                    {QStringLiteral("name"), object.value(QLatin1String("name")).toString()},
                    {QStringLiteral("value"), object.value(QLatin1String("value")).toString()},
                });
            }
            m_interfaces = list;
            Q_EMIT interfacesChanged();
        });
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("iface"), iface);
    m_client->get(QStringLiteral("app/networkInterfaceAddressList"), query)
        ->then(this, [this, generation](const Result<QByteArray> &result) {
            if (generation != m_generation || result.isError())
                return;
            QStringList addresses;
            const QJsonArray array = QJsonDocument::fromJson(result.value()).array();
            for (const QJsonValue &entry : array)
                addresses.append(entry.toString());
            m_addresses = addresses;
            Q_EMIT interfacesChanged();
        });
}

void ServerPreferences::rotateApiKey()
{
    if (m_client == nullptr || m_offline)
        return;
    const quint64 generation = m_generation;
    m_client->post(QStringLiteral("app/rotateAPIKey"))
        ->then(this, [this, generation](const Result<QByteArray> &result) {
            if (generation != m_generation)
                return;
            if (result.isError()) {
                Q_EMIT apiKeyRotateFailed(result.error().toString());
                return;
            }
            const QString key = QJsonDocument::fromJson(result.value())
                                    .object()
                                    .value(QLatin1String("apiKey"))
                                    .toString();
            if (key.isEmpty()) {
                Q_EMIT apiKeyRotateFailed(tr("the server sent no key"));
                return;
            }
            Q_EMIT apiKeyRotated(key);
            reload();
        });
}

} // namespace omaseed::core
