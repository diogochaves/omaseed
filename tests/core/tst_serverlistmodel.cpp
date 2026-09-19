// T4.5 — `ServerListModel`, the QML face of `ServerStore` (spec v2 §5 Fig. 16's Servers pane,
// §3 Fig. 9's server sheet). Every role the settings modal draws, the four mutations it can
// make, what a probe writes into a row, and the fact that everything lands in
// `servers.json` under a throwaway XDG_CONFIG_HOME. `ServerStore` itself (the file format,
// the version field) is covered by tests/core/tst_serverstore.cpp.

#include "core/SecretStore.hpp"
#include "core/Server.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerStore.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace omaseed::core;

namespace {

QVariantMap homeFields()
{
    return QVariantMap{
        {QStringLiteral("name"), QStringLiteral("home")},
        {QStringLiteral("url"), QStringLiteral("http://nas.invalid:9090")},
        {QStringLiteral("authMode"), QStringLiteral("apiKey")},
        {QStringLiteral("pathMapRemote"), QStringLiteral("/downloads")},
        {QStringLiteral("pathMapLocal"), QStringLiteral("/data/torrents")},
    };
}

QVariant roleOf(const ServerListModel &model, int row, ServerListModel::Role role)
{
    return model.data(model.index(row, 0), role);
}

} // namespace

class TestServerListModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void pureHelpers();
    void rolesOfOneServer();
    void addUpdateRemove();
    void defaultServer();
    void probeResultsFillTheRow();
    void persistsToJson();
    void secretsFollowTheServer();

private:
    QTemporaryDir m_config;
};

void TestServerListModel::initTestCase()
{
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
}

void TestServerListModel::init()
{
    // Every test starts from an empty servers.json.
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/servers.json"));
}

void TestServerListModel::pureHelpers()
{
    QCOMPARE(ServerListModel::authModeText(AuthMode::ApiKey), QStringLiteral("API key"));
    QCOMPARE(ServerListModel::authModeText(AuthMode::Cookie), QStringLiteral("cookie"));
    QCOMPARE(ServerListModel::authModeText(AuthMode::None), QStringLiteral("none"));

    QCOMPARE(ServerListModel::urlText(QUrl(QStringLiteral("http://nas.invalid:9090"))),
             QStringLiteral("nas.invalid:9090"));
    QCOMPARE(ServerListModel::urlText(QUrl(QStringLiteral("https://sb.example.net"))),
             QStringLiteral("sb.example.net"));
    QCOMPARE(ServerListModel::urlText(QUrl()), QString());

    QCOMPARE(ServerListModel::pathMapText(
                 {PathMapping{QStringLiteral("/downloads"), QStringLiteral("/data/torrents")}}),
             QStringLiteral("/downloads → /data/torrents"));
    QCOMPARE(ServerListModel::pathMapText({}), QString());

    // A host typed without a scheme is still a server; a trailing slash is not part of one.
    const Server bare = ServerListModel::serverFromMap(
        QVariantMap{{QStringLiteral("url"), QStringLiteral("nas.local:9090/")}});
    QCOMPARE(bare.url.toString(), QStringLiteral("http://nas.local:9090"));
    // An unknown auth mode is "none", never a guess.
    const Server odd = ServerListModel::serverFromMap(
        QVariantMap{{QStringLiteral("url"), QStringLiteral("http://a.b")},
                    {QStringLiteral("authMode"), QStringLiteral("magic")}});
    QCOMPARE(odd.authMode, AuthMode::None);
}

void TestServerListModel::rolesOfOneServer()
{
    ServerStore store;
    ServerListModel model(&store);
    const QString id = model.add(homeFields());
    QVERIFY(!id.isEmpty());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount(), 1);

    QCOMPARE(roleOf(model, 0, ServerListModel::IdRole).toString(), id);
    QCOMPARE(roleOf(model, 0, ServerListModel::NameRole).toString(), QStringLiteral("home"));
    QCOMPARE(roleOf(model, 0, ServerListModel::UrlRole).toString(),
             QStringLiteral("http://nas.invalid:9090"));
    QCOMPARE(roleOf(model, 0, ServerListModel::UrlTextRole).toString(),
             QStringLiteral("nas.invalid:9090"));
    QCOMPARE(roleOf(model, 0, ServerListModel::AuthModeRole).toString(), QStringLiteral("apiKey"));
    QCOMPARE(roleOf(model, 0, ServerListModel::AuthModeTextRole).toString(),
             QStringLiteral("API key"));
    QCOMPARE(roleOf(model, 0, ServerListModel::PathMapTextRole).toString(),
             QStringLiteral("/downloads → /data/torrents"));
    // The first server ever added becomes the default on its own.
    QVERIFY(roleOf(model, 0, ServerListModel::IsDefaultRole).toBool());
    // Nothing has been probed yet.
    QCOMPARE(roleOf(model, 0, ServerListModel::TorrentCountRole).toInt(), -1);
    QCOMPARE(roleOf(model, 0, ServerListModel::ReachableTextRole).toString(), QString());
    QVERIFY(!roleOf(model, 0, ServerListModel::ReachableRole).toBool());

    // roleNames() is the contract the QML delegates bind to.
    const QHash<int, QByteArray> names = model.roleNames();
    for (const char *expected : {"id",
                                 "name",
                                 "url",
                                 "urlText",
                                 "authMode",
                                 "authModeText",
                                 "username",
                                 "isDefault",
                                 "pathMapText",
                                 "reachable",
                                 "reachableText",
                                 "torrentCount",
                                 "lastError"})
        QVERIFY2(names.values().contains(QByteArray(expected)), expected);

    // serverMap() round-trips through the edit fields the sheet uses.
    const QVariantMap map = model.serverMap(id);
    QCOMPARE(map.value(QStringLiteral("pathMapRemote")).toString(), QStringLiteral("/downloads"));
    QCOMPARE(map.value(QStringLiteral("pathMapLocal")).toString(),
             QStringLiteral("/data/torrents"));
    QCOMPARE(model.indexOfId(id), 0);
    QCOMPARE(model.idAt(0), id);
    QCOMPARE(model.idAt(7), QString());
    QVERIFY(model.serverMap(QStringLiteral("nope")).isEmpty());
}

void TestServerListModel::addUpdateRemove()
{
    ServerStore store;
    ServerListModel model(&store);
    QSignalSpy countSpy(&model, &ServerListModel::countChanged);

    // No URL is not a server.
    QVERIFY(model.add(QVariantMap{{QStringLiteral("name"), QStringLiteral("nowhere")}}).isEmpty());
    QCOMPARE(model.count(), 0);

    const QString id = model.add(homeFields());
    QVERIFY(!id.isEmpty());
    QVERIFY(countSpy.count() > 0);

    QVariantMap edited = model.serverMap(id);
    edited[QStringLiteral("name")] = QStringLiteral("nas");
    edited[QStringLiteral("url")] = QStringLiteral("https://nas.local:9090");
    edited[QStringLiteral("authMode")] = QStringLiteral("cookie");
    edited[QStringLiteral("username")] = QStringLiteral("diogo");
    QVERIFY(model.update(edited));
    QCOMPARE(roleOf(model, 0, ServerListModel::NameRole).toString(), QStringLiteral("nas"));
    QCOMPARE(roleOf(model, 0, ServerListModel::AuthModeTextRole).toString(),
             QStringLiteral("cookie"));
    QCOMPARE(roleOf(model, 0, ServerListModel::UsernameRole).toString(), QStringLiteral("diogo"));
    QCOMPARE(roleOf(model, 0, ServerListModel::UrlTextRole).toString(),
             QStringLiteral("nas.local:9090"));

    // An id nobody knows is not an edit.
    QVariantMap unknown = edited;
    unknown[QStringLiteral("id")] = QStringLiteral("nope");
    QVERIFY(!model.update(unknown));

    QVERIFY(!model.remove(QStringLiteral("nope")));
    QVERIFY(model.remove(id));
    QCOMPARE(model.count(), 0);
}

void TestServerListModel::defaultServer()
{
    ServerStore store;
    ServerListModel model(&store);
    const QString first = model.add(homeFields());
    QVariantMap second = homeFields();
    second[QStringLiteral("name")] = QStringLiteral("seedbox");
    second[QStringLiteral("url")] = QStringLiteral("https://sb.example.net");
    const QString other = model.add(second);
    QCOMPARE(model.count(), 2);

    QCOMPARE(model.defaultId(), first);
    QVERIFY(roleOf(model, 0, ServerListModel::IsDefaultRole).toBool());
    QVERIFY(!roleOf(model, 1, ServerListModel::IsDefaultRole).toBool());

    QVERIFY(model.setDefault(other));
    QCOMPARE(model.defaultId(), other);
    QVERIFY(!roleOf(model, 0, ServerListModel::IsDefaultRole).toBool());
    QVERIFY(roleOf(model, 1, ServerListModel::IsDefaultRole).toBool());

    // Removing the default promotes the first one left (ServerStore's own rule), so the app
    // is never left with servers and no server.
    QVERIFY(model.remove(other));
    QCOMPARE(model.defaultId(), first);
    QVERIFY(model.remove(first));
    QCOMPARE(model.defaultId(), QString());
}

void TestServerListModel::probeResultsFillTheRow()
{
    ServerStore store;
    ServerListModel model(&store);
    const QString id = model.add(homeFields());
    QSignalSpy changed(&model, &ServerListModel::dataChanged);

    model.setProbeResult(id, true, 142, 3, QString());
    QCOMPARE(changed.count(), 1);
    QVERIFY(roleOf(model, 0, ServerListModel::ReachableRole).toBool());
    QCOMPARE(roleOf(model, 0, ServerListModel::ReachableTextRole).toString(),
             QStringLiteral("yes · 3 ms"));
    QCOMPARE(roleOf(model, 0, ServerListModel::TorrentCountRole).toInt(), 142);

    // A failure keeps the last count (it is still what the server had) and says why.
    model.setProbeResult(id, false, -1, 12, QStringLiteral("connection refused"));
    QVERIFY(!roleOf(model, 0, ServerListModel::ReachableRole).toBool());
    QCOMPARE(roleOf(model, 0, ServerListModel::ReachableTextRole).toString(),
             QStringLiteral("no · connection refused"));
    QCOMPARE(roleOf(model, 0, ServerListModel::LastErrorRole).toString(),
             QStringLiteral("connection refused"));
    QCOMPARE(roleOf(model, 0, ServerListModel::TorrentCountRole).toInt(), 142);

    // A probe for a server that is gone is not a crash and not a row.
    model.setProbeResult(QStringLiteral("nope"), true, 1, 1, QString());
}

void TestServerListModel::persistsToJson()
{
    QString id;
    {
        ServerStore store;
        ServerListModel model(&store);
        id = model.add(homeFields());
        QVERIFY(!id.isEmpty());
    }

    QVERIFY(QFile::exists(m_config.path() + QStringLiteral("/omaseed/servers.json")));

    ServerStore reopened;
    QVERIFY(reopened.load());
    ServerListModel model(&reopened);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.idAt(0), id);
    QCOMPARE(roleOf(model, 0, ServerListModel::NameRole).toString(), QStringLiteral("home"));
    QCOMPARE(roleOf(model, 0, ServerListModel::PathMapTextRole).toString(),
             QStringLiteral("/downloads → /data/torrents"));
    QVERIFY(roleOf(model, 0, ServerListModel::IsDefaultRole).toBool());
}

void TestServerListModel::secretsFollowTheServer()
{
    ServerStore store;
    SecretStore secrets(QStringLiteral("io.github.diogochaves.omaseed-t45-test"));
    // The file backend keeps this test off the session's keyring entirely.
    secrets.setForcedBackend(SecretStore::Backend::File);
    ServerListModel model(&store, &secrets);

    const QString id = model.add(homeFields(), QStringLiteral("qbt_secret_value"));
    QVERIFY(!id.isEmpty());
    const QString secretFile =
        m_config.path() + QStringLiteral("/omaseed/secrets/") + id + QStringLiteral(".json");
    QTRY_VERIFY(QFile::exists(secretFile));

    // servers.json must never carry the secret.
    QFile file(m_config.path() + QStringLiteral("/omaseed/servers.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains("qbt_secret_value"));
    file.close();

    QVERIFY(model.remove(id));
    QTRY_VERIFY(!QFile::exists(secretFile));
}

QTEST_MAIN(TestServerListModel)

#include "tst_serverlistmodel.moc"
