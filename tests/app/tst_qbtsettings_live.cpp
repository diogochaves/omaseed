// docs/16's qBittorrent settings against a real qBittorrent, which only a fake server had
// checked before (tst_serverpreferences): every key the Settings group writes, the changes
// that cut the app off (Web UI port, login, local auth) and the API key rotation.
//
// None of it runs on the :8081 playground or the :8082 strict twin: each test starts a
// qbittorrent-nox of its own (tests/ThrowawayQbt.hpp) on free ports with a temporary profile,
// so a moved Web UI, a new login or a rotated key dies with it. The whole file skips when
// qbittorrent-nox is not installed.
//
// The keys come from Views/QbtSettingsSections.qml itself, loaded here, so a row added there
// is covered without touching this file. App's half keeps out of the real profile as
// tst_app_servers does: XDG_CONFIG_HOME in a temporary directory and a keychain service of
// its own, emptied before and after.

#include "core/AltSpeed.hpp"
#include "core/CertificatePin.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerPreferences.hpp"
#include "core/SyncEngine.hpp"
#include "ui/app/App.hpp"

#include "ThrowawayQbt.hpp"

#include <QEventLoop>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <memory>

using omaseed::app::App;
using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::ServerPreferences;
using omaseed::core::SyncEngine;

namespace {

const auto kSecretService = QStringLiteral("io.github.diogochaves.omaseed-qbtsettings-test");

void clearTestSecretService()
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (tool.isEmpty())
        return;
    QProcess clear;
    clear.start(tool, {QStringLiteral("clear"), QStringLiteral("server"), kSecretService});
    if (!clear.waitForFinished(10000)) {
        clear.kill();
        clear.waitForFinished(1000);
    }
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &value) {
        result = value;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

/// The schema's sections, as plain variants.
QVariantList loadSections(QString *error)
{
    static QQmlEngine engine;
    QQmlComponent component(
        &engine,
        QUrl::fromLocalFile(QStringLiteral(OMASEED_QML_DIR "/Views/QbtSettingsSections.qml")));
    std::unique_ptr<QObject> schema(component.create());
    if (!schema) {
        *error = component.errorString();
        return {};
    }
    QVariant sections = schema->property("sections");
    if (sections.canConvert<QJSValue>())
        sections = sections.value<QJSValue>().toVariant();
    return sections.toList();
}

/// One change the round trip makes: the keys it writes together (a "time" field writes hours
/// and minutes), and where it came from, for the failure message.
struct Change
{
    QString section;
    QString title;
    QVariantMap values;
    /// Every key of the row: qBittorrent may move them together (a share limit's value turns
    /// its switch), and putting the change back sends their old values.
    QStringList rowKeys;
};

/// The keys a schema row reads and writes.
QStringList keysOfRow(const QVariantMap &row)
{
    QStringList keys;
    if (row.contains(QStringLiteral("toggle")))
        keys.append(row.value(QStringLiteral("toggle")).toString());
    for (const char *kind : {"choice", "pick", "list"}) {
        const QVariantMap control = row.value(QLatin1String(kind)).toMap();
        if (control.contains(QStringLiteral("key")))
            keys.append(control.value(QStringLiteral("key")).toString());
    }
    for (const QVariant &field : row.value(QStringLiteral("fields")).toList()) {
        keys.append(field.toMap().value(QStringLiteral("key")).toString());
        if (field.toMap().contains(QStringLiteral("minKey")))
            keys.append(field.toMap().value(QStringLiteral("minKey")).toString());
    }
    for (const char *extra : {"enable", "disable"})
        keys.append(row.value(QLatin1String(extra)).toMap().keys());
    keys.removeDuplicates();
    return keys;
}

/// What QbtSettingRow::flipChanges writes for the row's switch turned `on`: the key, and the
/// row's `enable` / `disable` keys qBittorrent needs with it.
QVariantMap flipChanges(const QVariantMap &row, const QVariantMap &prefs, bool on)
{
    QVariantMap changes{{row.value(QStringLiteral("toggle")).toString(), on}};
    const QVariantMap extra =
        row.value(on ? QStringLiteral("enable") : QStringLiteral("disable")).toMap();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        QVariant none;
        for (const QVariant &field : row.value(QStringLiteral("fields")).toList()) {
            if (field.toMap().value(QStringLiteral("key")) == it.key())
                none = field.toMap().value(QStringLiteral("none"));
        }
        if (on && none.isValid() && !ServerPreferences::sameValue(prefs.value(it.key()), none))
            continue;
        changes.insert(it.key(),
                       it.value() == QStringLiteral("randomPort")
                           ? QVariant(ThrowawayQbt::freePort())
                           : it.value());
    }
    return changes;
}

/// A value of `current`'s kind that differs from it, for a number field.
QVariant otherNumber(const QVariantMap &field, const QVariant &current)
{
    const double scale = field.value(QStringLiteral("scale"), 1).toDouble();
    const bool decimals = field.value(QStringLiteral("decimals")).toInt() > 0 ||
                          current.typeId() == QMetaType::Double;
    const double step = decimals ? 0.5 : scale;
    const QVariant none = field.value(QStringLiteral("none"));
    double value = current.toDouble();
    const bool hasMin = field.contains(QStringLiteral("min"));
    const bool hasMax = field.contains(QStringLiteral("max"));
    const double min = field.value(QStringLiteral("min")).toDouble();
    const double max = field.value(QStringLiteral("max")).toDouble();
    if (none.isValid() && ServerPreferences::sameValue(none, current))
        value = (hasMin && min > 0 ? min : 1) * scale;
    else if (hasMax && value + step > max * scale)
        value -= step;
    else if (value < 0)
        value = (hasMin ? std::max(min, 1.0) : 1) * scale;
    else
        value += step;
    if (decimals)
        return value;
    return qint64(value);
}

/// Sample entries for the list rows, by key: something each list takes as valid.
QString listSample(const QString &key)
{
    static const QHash<QString, QString> samples{
        {QStringLiteral("banned_IPs"), QStringLiteral("10.11.12.13")},
        {QStringLiteral("bypass_auth_subnet_whitelist"), QStringLiteral("10.0.0.0/8")},
        // The Host header must still pass while the round trip runs: keep 127.0.0.1 in.
        {QStringLiteral("web_ui_domain_list"), QStringLiteral("127.0.0.1;example.org")},
        {QStringLiteral("web_ui_custom_http_headers"), QStringLiteral("X-Omaseed-Test: 1")},
        {QStringLiteral("web_ui_reverse_proxies_list"), QStringLiteral("10.0.0.2")},
        {QStringLiteral("add_trackers"), QStringLiteral("udp://tracker.example.org:6969/announce")},
        {QStringLiteral("add_trackers_url_list"),
         QStringLiteral("https://example.org/trackers.txt")},
        {QStringLiteral("excluded_file_names"), QStringLiteral("*.lnk")},
    };
    return samples.value(key, QStringLiteral("omaseed-test"));
}

/// A text value the server should take for `key`.
QString textSample(const QString &key, const QString &current, const QString &scratch)
{
    if (key.endsWith(QLatin1String("_path")) || key.endsWith(QLatin1String("_dir")) ||
        key.contains(QLatin1String("export_dir")) || current.startsWith(QLatin1Char('/')))
        return scratch + QLatin1Char('/') + key;
    if (key.endsWith(QLatin1String("_ip")) || key.endsWith(QLatin1String("_address")))
        return QStringLiteral("127.0.0.2");
    if (key.contains(QLatin1String("email")) ||
        key.contains(QLatin1String("mail_notification_sender")))
        return QStringLiteral("omaseed@example.org");
    if (key.contains(QLatin1String("smtp")))
        return QStringLiteral("smtp.example.org");
    if (key.contains(QLatin1String("domain")))
        return QStringLiteral("omaseed.example.org");
    return current + QStringLiteral("-omaseed");
}

/// Keys the round trip leaves alone, and why. Each is covered by a test of its own below,
/// or would reach beyond this machine.
const QHash<QString, QString> &skipped()
{
    static const QHash<QString, QString> keys{
        {QStringLiteral("web_ui_address"), QStringLiteral("cut-off: webUiAddress test")},
        {QStringLiteral("web_ui_port"), QStringLiteral("cut-off: webUiPortMovesTheServer")},
        {QStringLiteral("use_https"), QStringLiteral("cut-off: needs a certificate")},
        {QStringLiteral("web_ui_https_cert_path"), QStringLiteral("with use_https")},
        {QStringLiteral("web_ui_https_key_path"), QStringLiteral("with use_https")},
        {QStringLiteral("web_ui_username"), QStringLiteral("cut-off: loginChangeIsFollowed")},
        {QStringLiteral("web_ui_password"), QStringLiteral("cut-off: loginChangeIsFollowed")},
        {QStringLiteral("bypass_local_auth"), QStringLiteral("cut-off: localAuthTurnedOn")},
        {QStringLiteral("web_ui_upnp"), QStringLiteral("would ask the router to forward a port")},
        {QStringLiteral("upnp"), QStringLiteral("would ask the router to forward a port")},
        {QStringLiteral("dyndns_enabled"), QStringLiteral("would call a dynamic DNS service")},
        {QStringLiteral("current_network_interface"), QStringLiteral("peers stay on loopback")},
        {QStringLiteral("current_interface_address"), QStringLiteral("peers stay on loopback")},
        {QStringLiteral("alternative_webui_enabled"), QStringLiteral("no alternative UI to serve")},
    };
    return keys;
}

/// Every change the Settings group can make, one per control, with a value that differs from
/// the server's current one.
QList<Change> changesFor(const QVariantList &sections,
                         const QVariantMap &prefs,
                         const QString &scratch,
                         QStringList *notOnServer)
{
    QList<Change> changes;
    QStringList rowKeys;
    const auto add = [&](const QString &section, const QString &title, const QVariantMap &values) {
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            if (skipped().contains(it.key()))
                return;
            if (!prefs.contains(it.key()) && !it.key().endsWith(QLatin1String("password"))) {
                notOnServer->append(it.key());
                return;
            }
        }
        if (!values.isEmpty())
            changes.append(Change{section, title, values, rowKeys});
    };
    for (const QVariant &sectionValue : sections) {
        const QVariantMap section = sectionValue.toMap();
        const QString name = section.value(QStringLiteral("id")).toString();
        for (const QVariant &rowValue : section.value(QStringLiteral("rows")).toList()) {
            const QVariantMap row = rowValue.toMap();
            const QString title = row.value(QStringLiteral("title")).toString();
            rowKeys = keysOfRow(row);
            if (row.contains(QStringLiteral("toggle"))) {
                const QString key = row.value(QStringLiteral("toggle")).toString();
                add(name, title, flipChanges(row, prefs, !prefs.value(key).toBool()));
            }
            for (const char *kind : {"choice", "pick"}) {
                const QVariantMap choice = row.value(QLatin1String(kind)).toMap();
                if (choice.isEmpty())
                    continue;
                const QString key = choice.value(QStringLiteral("key")).toString();
                if (choice.contains(QStringLiteral("source"))) {
                    add(name, title, {{key, QVariant()}}); // skipped by key
                    continue;
                }
                for (const QVariant &option : choice.value(QStringLiteral("options")).toList()) {
                    const QVariant value = option.toList().value(0);
                    if (!ServerPreferences::sameValue(value, prefs.value(key))) {
                        add(name, title, {{key, value}});
                        break;
                    }
                }
            }
            const QVariantMap list = row.value(QStringLiteral("list")).toMap();
            if (!list.isEmpty()) {
                const QString key = list.value(QStringLiteral("key")).toString();
                if (list.value(QStringLiteral("dirs")).toBool()) {
                    // scan_dirs: {path: 0}, 0 = download into the folder it was found in.
                    add(name, title, {{key, QVariantMap{{scratch + QStringLiteral("/watch"), 0}}}});
                } else {
                    const QString sep = list.value(QStringLiteral("sep")).toString();
                    const QString current = prefs.value(key).toString();
                    const QString sample = listSample(key);
                    add(name, title, {{key, current.isEmpty() ? sample : current + sep + sample}});
                }
            }
            for (const QVariant &fieldValue : row.value(QStringLiteral("fields")).toList()) {
                const QVariantMap field = fieldValue.toMap();
                const QString key = field.value(QStringLiteral("key")).toString();
                const QString kind = field.value(QStringLiteral("kind")).toString();
                const QVariant current = prefs.value(key);
                if (kind == QLatin1String("time")) {
                    const QString minKey = field.value(QStringLiteral("minKey")).toString();
                    add(name,
                        title,
                        {{key, (current.toInt() + 1) % 24},
                         {minKey, (prefs.value(minKey).toInt() + 5) % 60}});
                } else if (kind == QLatin1String("text")) {
                    add(name, title, {{key, textSample(key, current.toString(), scratch)}});
                } else if (kind == QLatin1String("password")) {
                    add(name, title, {{key, QStringLiteral("omaseed-test-secret")}});
                } else {
                    add(name, title, {{key, otherNumber(field, current)}});
                }
            }
        }
    }
    return changes;
}

QString fingerprintOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return omaseed::core::CertificatePin::fingerprint(QSslCertificate(file.readAll(), QSsl::Pem));
}

} // namespace

/// Starts `qbt` (with START's arguments), skips the test without qbittorrent-nox, fails it
/// when the server does not come up.
#define START_OR_SKIP(qbt, ...)                                                                    \
    do {                                                                                           \
        if (!(qbt).start(__VA_ARGS__)) {                                                           \
            if ((qbt).skipReason().isEmpty())                                                      \
                QFAIL(qPrintable((qbt).error()));                                                  \
            QSKIP(qPrintable((qbt).skipReason()));                                                 \
        }                                                                                          \
    } while (false)

class TestQbtSettingsLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void everySettingRoundTrips();
    void unknownAndBadValuesAreKeptNotRefused();
    void aShortPasswordIsRefusedAndTheLoginStays();
    void webUiPortMovesTheSavedServer();
    void loginChangeIsFollowed();
    void httpsWithoutACertificateStaysOnHttp();
    void httpsWithACertificateMovesTheSavedServer();
    void localAuthTurnedOnShutsANoLoginServerOut();
    void rotatedApiKeyReplacesTheSavedOne();
    void resumeDataStorageSurvivesARestart();
    void altSetIsReadAgainWhenItFlips();

private:
    /// App on `qbt`, signed in the way `authMode` says, synced once; the server's id.
    QString connectApp(App &app, ThrowawayQbt &qbt, const QString &authMode, const QString &secret);
    /// A fresh App from what the first one saved: true when it signs in and syncs.
    bool savedServerSyncs();
    /// Logs a client into `qbt` and loads `prefs`; false when that fails.
    bool connectPrefs(ThrowawayQbt &qbt, QbtClient &client, ServerPreferences &prefs);
    /// Applies `values` and waits for the read-back; the keys the server kept, or an error.
    QString applyAndWait(ServerPreferences &prefs, const QVariantMap &values, QStringList *kept);

    QTemporaryDir m_config;
};

void TestQbtSettingsLive::initTestCase()
{
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    qputenv("OMASEED_SECRET_SERVICE", kSecretService.toUtf8());
    qunsetenv("OMASEED_SERVER_URL");
    clearTestSecretService();
}

void TestQbtSettingsLive::cleanupTestCase()
{
    clearTestSecretService();
}

void TestQbtSettingsLive::init()
{
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/servers.json"));
    QFile::remove(m_config.path() + QStringLiteral("/omaseed/state.json"));
}

bool TestQbtSettingsLive::connectPrefs(ThrowawayQbt &qbt,
                                       QbtClient &client,
                                       ServerPreferences &prefs)
{
    client.setBaseUrl(qbt.url());
    client.setCredentials(qbt.username(), qbt.password());
    prefs.reload();
    return QTest::qWaitFor([&]() { return prefs.isLoaded() || !prefs.loadError().isEmpty(); },
                           15000) &&
           prefs.isLoaded();
}

QString TestQbtSettingsLive::applyAndWait(ServerPreferences &prefs,
                                          const QVariantMap &values,
                                          QStringList *kept)
{
    QSignalSpy applied(&prefs, &ServerPreferences::applied);
    QSignalSpy failed(&prefs, &ServerPreferences::applyFailed);
    prefs.apply(values);
    if (!QTest::qWaitFor([&]() { return applied.count() + failed.count() > 0; }, 15000))
        return QStringLiteral("no answer");
    if (failed.count() > 0)
        return failed.at(0).at(1).toString();
    if (kept)
        *kept = applied.at(0).at(1).toStringList();
    return {};
}

void TestQbtSettingsLive::everySettingRoundTrips()
{
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);

    QString schemaError;
    const QVariantList sections = loadSections(&schemaError);
    QVERIFY2(!sections.isEmpty(), qPrintable(schemaError));

    QbtClient client;
    ServerPreferences prefs(&client);
    QVERIFY2(connectPrefs(qbt, client, prefs), qPrintable(prefs.loadError()));
    const QVariantMap original = prefs.values();

    QStringList notOnServer;
    const QList<Change> changes = changesFor(sections, original, qbt.profilePath(), &notOnServer);
    QVERIFY(changes.size() > 100);
    qInfo("%lld changes over %lld sections", qlonglong(changes.size()), qlonglong(sections.size()));

    QStringList problems;
    for (const Change &change : changes) {
        const QVariantMap before = prefs.values();
        QStringList kept;
        const QString error = applyAndWait(prefs, change.values, &kept);
        const QString where =
            QStringLiteral("%1 › %2 %3")
                .arg(change.section,
                     change.title,
                     QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(change.values))
                                           .toJson(QJsonDocument::Compact)));
        if (!error.isEmpty()) {
            problems.append(QStringLiteral("%1: refused (%2)").arg(where, error));
            continue;
        }
        if (!kept.isEmpty())
            problems.append(
                QStringLiteral("%1: the server kept %2 as %3")
                    .arg(where, kept.join(QLatin1Char(',')), prefs.value(kept.first()).toString()));
        // Nothing but the keys sent may change.
        const QVariantMap after = prefs.values();
        for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
            if (change.values.contains(it.key()) || change.rowKeys.contains(it.key()))
                continue;
            if (!ServerPreferences::sameValue(before.value(it.key()), it.value()))
                problems.append(QStringLiteral("%1: %2 changed too (%3 → %4)")
                                    .arg(where,
                                         it.key(),
                                         before.value(it.key()).toString(),
                                         it.value().toString()));
        }
        // And back, so the next change starts from qBittorrent's defaults.
        QVariantMap restore;
        QStringList touched = change.values.keys() + change.rowKeys;
        touched.removeDuplicates();
        for (const QString &key : std::as_const(touched)) {
            if (original.contains(key) && !skipped().contains(key) &&
                !ServerPreferences::sameValue(original.value(key), after.value(key)))
                restore.insert(key, original.value(key));
        }
        if (!restore.isEmpty()) {
            const QString back = applyAndWait(prefs, restore, nullptr);
            if (!back.isEmpty())
                problems.append(
                    QStringLiteral("%1: putting it back was refused (%2)").arg(where, back));
        }
    }

    // Everything is as it was.
    prefs.reload();
    QTRY_VERIFY_WITH_TIMEOUT(!prefs.isLoading(), 15000);
    for (auto it = original.constBegin(); it != original.constEnd(); ++it) {
        if (!ServerPreferences::sameValue(it.value(), prefs.value(it.key())))
            problems.append(
                QStringLiteral("after the round trip %1 is %2, was %3")
                    .arg(it.key(), prefs.value(it.key()).toString(), it.value().toString()));
    }

    if (!notOnServer.isEmpty())
        problems.append(QStringLiteral("rows for keys qBittorrent does not have: %1")
                            .arg(notOnServer.join(QStringLiteral(", "))));
    for (const QString &problem : std::as_const(problems))
        qWarning().noquote() << problem;
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("%1 problem(s), listed above").arg(problems.size())));
}

QString TestQbtSettingsLive::connectApp(App &app,
                                        ThrowawayQbt &qbt,
                                        const QString &authMode,
                                        const QString &secret)
{
    app.start();
    QSignalSpy synced(app.engine(), &SyncEngine::firstSyncFinished);
    const QString id =
        app.addServer({{QStringLiteral("name"), QStringLiteral("throwaway")},
                       {QStringLiteral("url"), qbt.url().toString()},
                       {QStringLiteral("authMode"), authMode},
                       {QStringLiteral("username"),
                        authMode == QLatin1String("cookie") ? qbt.username() : QString()}},
                      secret);
    if (id.isEmpty() || !synced.wait(20000))
        return {};
    if (!QTest::qWaitFor([&]() { return app.serverPrefs()->isLoaded(); }, 15000))
        return {};
    return id;
}

bool TestQbtSettingsLive::savedServerSyncs()
{
    App fresh;
    QSignalSpy synced(fresh.engine(), &SyncEngine::firstSyncFinished);
    fresh.start();
    return fresh.hasServer() && synced.wait(20000);
}

void TestQbtSettingsLive::unknownAndBadValuesAreKeptNotRefused()
{
    // qBittorrent 5.2.3 answers 200 to a key it does not know and to a value of the wrong
    // type; only the read-back tells. ServerPreferences reports those keys as kept.
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    QbtClient client;
    ServerPreferences prefs(&client);
    QVERIFY2(connectPrefs(qbt, client, prefs), qPrintable(prefs.loadError()));

    QStringList kept;
    QCOMPARE(applyAndWait(prefs, {{QStringLiteral("max_ratio"), QStringLiteral("x")}}, &kept),
             QString());
    QCOMPARE(kept, QStringList{QStringLiteral("max_ratio")});
    QCOMPARE(prefs.value(QStringLiteral("max_ratio")).toInt(), 0); // the server's reading of "x"
    applyAndWait(prefs, {{QStringLiteral("max_ratio"), -1}}, nullptr);

    QCOMPARE(applyAndWait(prefs,
                          {{QStringLiteral("resume_data_storage_type"), QStringLiteral("Nope")}},
                          &kept),
             QString());
    QCOMPARE(kept, QStringList{QStringLiteral("resume_data_storage_type")});
    QCOMPARE(prefs.value(QStringLiteral("resume_data_storage_type")).toString(),
             QStringLiteral("Legacy"));

    // A key the server does not have is neither shown nor refused.
    QCOMPARE(applyAndWait(prefs, {{QStringLiteral("omaseed_no_such_key"), 1}}, &kept), QString());
    QVERIFY(!prefs.has(QStringLiteral("omaseed_no_such_key")));
}

void TestQbtSettingsLive::aShortPasswordIsRefusedAndTheLoginStays()
{
    // The one refusal a 5.2.3 server gives through setPreferences: a Web UI password under six
    // characters is a 400. Nothing is followed and the app stays signed in.
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    App app;
    const QString id = connectApp(app, qbt, QStringLiteral("cookie"), qbt.password());
    QVERIFY(!id.isEmpty());

    QSignalSpy failed(app.serverPrefs(), &ServerPreferences::applyFailed);
    QSignalSpy posted(app.serverPrefs(), &ServerPreferences::posted);
    app.serverPrefs()->apply({{QStringLiteral("web_ui_password"), QStringLiteral("abc")}});
    QVERIFY(failed.wait(15000));
    QCOMPARE(posted.count(), 0);
    QVERIFY(app.serverPrefs()->pendingKeys().isEmpty());
    QCOMPARE(app.serverList()->serverMap(id).value(QStringLiteral("username")).toString(),
             qbt.username());

    QbtClient check;
    check.setBaseUrl(qbt.url());
    check.setCredentials(qbt.username(), qbt.password());
    QVERIFY(!await(this, check.get(QStringLiteral("app/version"))).isError());
}

void TestQbtSettingsLive::webUiPortMovesTheSavedServer()
{
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    App app;
    const QString id = connectApp(app, qbt, QStringLiteral("cookie"), qbt.password());
    QVERIFY(!id.isEmpty());
    const int oldPort = qbt.webPort();
    const int newPort = ThrowawayQbt::freePort();
    QVERIFY(newPort > 0);

    QSignalSpy syncedAgain(app.engine(), &SyncEngine::firstSyncFinished);
    app.serverPrefs()->apply({{QStringLiteral("web_ui_port"), newPort}});
    QVERIFY2(ThrowawayQbt::waitFor(newPort, true), "qBittorrent never listened on the new port");
    QVERIFY2(ThrowawayQbt::waitFor(oldPort, false), "qBittorrent still listens on the old port");

    // The saved server moves with it, and the app syncs from the new place.
    QTRY_COMPARE_WITH_TIMEOUT(
        QUrl(app.serverList()->serverMap(id).value(QStringLiteral("url")).toString()).port(),
        newPort,
        10000);
    QVERIFY2(syncedAgain.wait(20000), "no sync after the move");
    QTRY_COMPARE_WITH_TIMEOUT(
        app.serverPrefs()->value(QStringLiteral("web_ui_port")).toInt(), newPort, 15000);
    QVERIFY(!app.isServerLost());
    QVERIFY(savedServerSyncs());
}

void TestQbtSettingsLive::loginChangeIsFollowed()
{
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    const QString user = QStringLiteral("omaseed-renamed");
    const QString pass = QStringLiteral("a-new-password-for-the-test");
    {
        App app;
        const QString id = connectApp(app, qbt, QStringLiteral("cookie"), qbt.password());
        QVERIFY(!id.isEmpty());
        QSignalSpy syncedAgain(app.engine(), &SyncEngine::firstSyncFinished);
        app.serverPrefs()->apply(
            {{QStringLiteral("web_ui_username"), user}, {QStringLiteral("web_ui_password"), pass}});
        QTRY_COMPARE_WITH_TIMEOUT(
            app.serverList()->serverMap(id).value(QStringLiteral("username")).toString(),
            user,
            10000);
        QVERIFY2(syncedAgain.wait(20000), "no sync with the new login");
        QVERIFY(!app.isServerLost());
        QTRY_COMPARE_WITH_TIMEOUT(
            app.serverPrefs()->value(QStringLiteral("web_ui_username")).toString(), user, 15000);
    }

    // The old login is refused; the saved one (the keychain's new password) signs in.
    QbtClient old;
    old.setBaseUrl(qbt.url());
    old.setCredentials(qbt.username(), qbt.password());
    QVERIFY(await(this, old.get(QStringLiteral("app/version"))).isError());
    QVERIFY(savedServerSyncs());
}

void TestQbtSettingsLive::httpsWithoutACertificateStaysOnHttp()
{
    // HTTPS on with no certificate and key (a wrong path does the same): qBittorrent answers
    // use_https, logs "HTTPS setup failed, fallback to HTTP" and stays on HTTP. The app must
    // not move to https:// and has to say why. The pane refuses the switch without both
    // paths (tst_QbtSettings), because what follows cannot be undone from the app: see below.
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    App app;
    const QString id = connectApp(app, qbt, QStringLiteral("cookie"), qbt.password());
    QVERIFY(!id.isEmpty());

    QSignalSpy notFollowed(&app, &App::webUiNotFollowed);
    app.serverPrefs()->apply({{QStringLiteral("use_https"), true}});
    QVERIFY2(notFollowed.wait(20000), "the app never said it stayed on HTTP");
    QVERIFY(notFollowed.at(0).at(0).toString().contains(QLatin1String("HTTPS")));
    const QUrl saved(app.serverList()->serverMap(id).value(QStringLiteral("url")).toString());
    QCOMPARE(saved.scheme(), QStringLiteral("http"));
    QCOMPARE(saved.port(), qbt.webPort());

    // Why it shuts the app out: qBittorrent marks the login cookie secure (it believes it
    // serves HTTPS), and no client sends a secure cookie over HTTP, Qt's included.
    QNetworkAccessManager nam;
    QUrl login = qbt.url();
    login.setPath(QStringLiteral("/api/v2/auth/login"));
    QNetworkRequest request(login);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("username"), qbt.username());
    form.addQueryItem(QStringLiteral("password"), qbt.password());
    QNetworkReply *reply = nam.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QVERIFY(finished.wait(10000));
    const auto cookies =
        reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
    QVERIFY(!cookies.isEmpty());
    QVERIFY2(cookies.first().isSecure(),
             "qBittorrent no longer marks it secure: the pane's guard may go");
    QVERIFY(nam.cookieJar()->cookiesForUrl(qbt.url()).isEmpty());
    reply->deleteLater();
}

void TestQbtSettingsLive::httpsWithACertificateMovesTheSavedServer()
{
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    QString cert;
    QString key;
    QString error;
    if (!ThrowawayQbt::makeCertificate(
            qbt.profilePath(), QStringLiteral("server"), &cert, &key, &error))
        QSKIP(qPrintable(error));
    const QString fingerprint = fingerprintOf(cert);
    QCOMPARE(fingerprint.size(), 64);

    App app;
    const QString id = connectApp(app, qbt, QStringLiteral("cookie"), qbt.password());
    QVERIFY(!id.isEmpty());
    QSignalSpy applied(app.serverPrefs(), &ServerPreferences::applied);
    app.serverPrefs()->apply({{QStringLiteral("web_ui_https_cert_path"), cert},
                              {QStringLiteral("web_ui_https_key_path"), key}});
    QVERIFY(applied.wait(15000));

    QSignalSpy notFollowed(&app, &App::webUiNotFollowed);
    QSignalSpy refused(&app, &App::certificateRefused);
    app.serverPrefs()->apply({{QStringLiteral("use_https"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(
        QUrl(app.serverList()->serverMap(id).value(QStringLiteral("url")).toString()).scheme(),
        QStringLiteral("https"),
        20000);
    QCOMPARE(notFollowed.count(), 0);

    // The certificate is self-signed, so the app is shut out until it is trusted (issue #2),
    // and it says so: lost, for that reason, with the server's id to review it on.
    QVERIFY2(refused.wait(20000), "the app never said the certificate was refused");
    QCOMPARE(refused.at(0).at(0).toString(), id);
    QVERIFY(app.isServerLost());
    QVERIFY(App::isCertificateError(app.globalState()->lastError()));
    QCOMPARE(app.lostReasonText(app.globalState()->lastError()),
             QStringLiteral("untrusted certificate"));

    // The server sheet's test shows the certificate to trust.
    QSignalSpy testRefused(&app, &App::serverCertificateRefused);
    QSignalSpy tested(&app, &App::serverTested);
    app.testServer(app.serverList()->serverMap(id));
    QVERIFY(tested.wait(20000));
    QCOMPARE(testRefused.count(), 1);
    QCOMPARE(testRefused.at(0).at(1).toString(), fingerprint);
    QVERIFY(testRefused.at(0).at(2).toString().isEmpty());
    QCOMPARE(tested.at(0).at(1).toBool(), false);
    QCOMPARE(tested.at(0).at(6).toString(), QStringLiteral("untrusted certificate"));

    // Trusted: saved with the server, and the app is back in, now and on the next start.
    QSignalSpy synced(app.engine(), &SyncEngine::firstSyncFinished);
    QVERIFY(app.trustCertificate(id, fingerprint));
    QCOMPARE(app.serverList()->serverMap(id).value(QStringLiteral("certificateSha256")).toString(),
             fingerprint);
    QVERIFY2(synced.wait(20000), "no sync after trusting the certificate");
    QVERIFY(!app.isServerLost());
    QVERIFY(savedServerSyncs());

    // A different certificate on the same server is refused again, and reads as changed.
    QString otherCert;
    QString otherKey;
    QVERIFY2(ThrowawayQbt::makeCertificate(
                 qbt.profilePath(), QStringLiteral("other"), &otherCert, &otherKey, &error),
             qPrintable(error));
    const QString otherFingerprint = fingerprintOf(otherCert);
    QSignalSpy otherApplied(app.serverPrefs(), &ServerPreferences::applied);
    app.serverPrefs()->apply({{QStringLiteral("web_ui_https_cert_path"), otherCert},
                              {QStringLiteral("web_ui_https_key_path"), otherKey}});
    QVERIFY(otherApplied.wait(15000));
    // qBittorrent serves the new certificate to new connections only; a restart makes the
    // app open one, as it would after the server's own restart.
    refused.clear();
    QVERIFY2(qbt.restart(), qPrintable(qbt.error()));
    QVERIFY2(refused.wait(30000), "the changed certificate was accepted");
    QCOMPARE(app.lostReasonText(app.globalState()->lastError()),
             QStringLiteral("the certificate changed"));
    testRefused.clear();
    tested.clear();
    app.testServer(app.serverList()->serverMap(id));
    QVERIFY(tested.wait(20000));
    QCOMPARE(testRefused.count(), 1);
    QCOMPARE(testRefused.at(0).at(1).toString(), otherFingerprint);
    QCOMPARE(testRefused.at(0).at(2).toString(), fingerprint);
    QCOMPARE(tested.at(0).at(6).toString(), QStringLiteral("the certificate changed"));
}

void TestQbtSettingsLive::localAuthTurnedOnShutsANoLoginServerOut()
{
    // A server the app reaches with no login (local auth bypassed) asking for one: there is
    // nothing to follow. qBittorrent hands a bypassed client a session cookie anyway, and
    // that session outlives the change, so the running app goes on until it times out (an
    // hour by default) or the app starts again; from then on it is shut out, and says so.
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt, false);
    {
        App app;
        const QString id = connectApp(app, qbt, QStringLiteral("none"), QString());
        QVERIFY(!id.isEmpty());
        QCOMPARE(app.serverPrefs()->value(QStringLiteral("bypass_local_auth")).toBool(), true);

        QSignalSpy applied(app.serverPrefs(), &ServerPreferences::applied);
        app.serverPrefs()->apply({{QStringLiteral("bypass_local_auth"), false}});
        QVERIFY(applied.wait(15000));
        QCOMPARE(app.serverPrefs()->value(QStringLiteral("bypass_local_auth")).toBool(), false);
        QCOMPARE(
            QUrl(app.serverList()->serverMap(id).value(QStringLiteral("url")).toString()).port(),
            qbt.webPort());
    }

    QbtClient anonymous;
    anonymous.setBaseUrl(qbt.url());
    QVERIFY(await(this, anonymous.get(QStringLiteral("app/version"))).isError());
    {
        App restarted;
        QSignalSpy authRequired(restarted.engine(), &SyncEngine::authRequired);
        restarted.start();
        QVERIFY2(authRequired.wait(20000), "a restarted app never noticed it was shut out");
        QVERIFY(restarted.isServerLost());
    }

    // A login still gets in, so the setting can be put back.
    QbtClient admin;
    admin.setBaseUrl(qbt.url());
    admin.setCredentials(qbt.username(), qbt.password());
    QVERIFY(!await(this, admin.get(QStringLiteral("app/version"))).isError());
}

void TestQbtSettingsLive::rotatedApiKeyReplacesTheSavedOne()
{
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    QbtClient admin;
    admin.setBaseUrl(qbt.url());
    admin.setCredentials(qbt.username(), qbt.password());
    const auto first = await(this, admin.post(QStringLiteral("app/rotateAPIKey")));
    QVERIFY2(!first.isError(), qPrintable(first.isError() ? first.error().toString() : QString()));
    const QString firstKey =
        QJsonDocument::fromJson(first.value()).object().value(QLatin1String("apiKey")).toString();
    if (firstKey.isEmpty())
        QSKIP("this qBittorrent has no API keys (before 5.2)");

    QString newKey;
    {
        App app;
        const QString id = connectApp(app, qbt, QStringLiteral("apiKey"), firstKey);
        QVERIFY(!id.isEmpty());
        QSignalSpy rotated(app.serverPrefs(), &ServerPreferences::apiKeyRotated);
        QSignalSpy synced(app.engine(), &SyncEngine::synced);
        app.serverPrefs()->rotateApiKey();
        QVERIFY(rotated.wait(15000));
        newKey = rotated.at(0).at(0).toString();
        QVERIFY(!newKey.isEmpty() && newKey != firstKey);
        // The app keeps syncing on the new key.
        synced.clear();
        QVERIFY(synced.wait(20000));
        QTRY_VERIFY_WITH_TIMEOUT(synced.count() >= 2, 20000);
        QVERIFY(!app.isServerLost());
    }

    QbtClient old;
    old.setBaseUrl(qbt.url());
    old.setApiKey(firstKey);
    QVERIFY(await(this, old.get(QStringLiteral("app/version"))).isError());
    QbtClient current;
    current.setBaseUrl(qbt.url());
    current.setApiKey(newKey);
    QVERIFY(!await(this, current.get(QStringLiteral("app/version"))).isError());
    QVERIFY(savedServerSyncs());
}

void TestQbtSettingsLive::resumeDataStorageSurvivesARestart()
{
    // Advanced's "Resume data storage" takes effect at the next start. Switching it must not
    // lose the torrents qBittorrent knew (a real library has thousands).
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    QbtClient client;
    ServerPreferences prefs(&client);
    QVERIFY2(connectPrefs(qbt, client, prefs), qPrintable(prefs.loadError()));

    QFile torrent(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(torrent.open(QIODevice::ReadOnly));
    QbtClient::AddOptions options;
    options.stopped = true;
    QVERIFY(!await(this,
                   client.torrentsAddFile(
                       QStringLiteral("single_file_v1.torrent"), torrent.readAll(), options))
                 .isError());
    const auto count = [&]() {
        const auto info = await(this, client.get(QStringLiteral("torrents/info")));
        return info.isError() ? -1 : int(QJsonDocument::fromJson(info.value()).array().size());
    };
    QTRY_COMPARE_WITH_TIMEOUT(count(), 1, 10000);

    for (const QString &type : {QStringLiteral("SQLite"), QStringLiteral("Legacy")}) {
        QStringList kept;
        QCOMPARE(applyAndWait(prefs, {{QStringLiteral("resume_data_storage_type"), type}}, &kept),
                 QString());
        QVERIFY(kept.isEmpty());
        QVERIFY2(qbt.restart(), qPrintable(qbt.error()));
        client.setCredentials(qbt.username(), qbt.password()); // a new process, a new session
        QTRY_COMPARE_WITH_TIMEOUT(count(), 1, 15000);
        prefs.reload();
        QTRY_COMPARE_WITH_TIMEOUT(
            prefs.value(QStringLiteral("resume_data_storage_type")).toString(), type, 15000);
    }
}

void TestQbtSettingsLive::altSetIsReadAgainWhenItFlips()
{
    // docs/31 D13: the other set lives only in app/preferences. A set changed in qBittorrent's
    // Web UI (here another client) shows the next time the alternative limits flip.
    ThrowawayQbt qbt;
    START_OR_SKIP(qbt);
    App app;
    QVERIFY(!connectApp(app, qbt, QStringLiteral("cookie"), qbt.password()).isEmpty());
    // qBittorrent's defaults: alternative 10 KiB/s both ways, no normal limits.
    QCOMPARE(app.altSpeed()->kind(), QStringLiteral("turtle"));

    QbtClient webUi;
    webUi.setBaseUrl(qbt.url());
    webUi.setCredentials(qbt.username(), qbt.password());
    QVERIFY(!await(this,
                   webUi.appSetPreferences(QJsonObject{{QStringLiteral("up_limit"), 1228800},
                                                       {QStringLiteral("alt_dl_limit"), 0},
                                                       {QStringLiteral("alt_up_limit"), 0}}))
                 .isError());
    QCOMPARE(app.altSpeed()->kind(), QStringLiteral("turtle")); // not read on every poll

    QVERIFY(!await(this, webUi.transferToggleSpeedLimitsMode()).isError());
    QTRY_VERIFY_WITH_TIMEOUT(app.altSpeed()->isOn(), 20000);
    QTRY_COMPARE_WITH_TIMEOUT(app.altSpeed()->kind(), QStringLiteral("burst"), 15000);
    QCOMPARE(app.altSpeed()->chipTooltip(), QStringLiteral("burst on · A"));
}

QTEST_MAIN(TestQbtSettingsLive)
#include "tst_qbtsettings_live.moc"
