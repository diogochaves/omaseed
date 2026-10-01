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

#include "core/QbtClient.hpp"
#include "core/ServerListModel.hpp"
#include "core/ServerPreferences.hpp"
#include "ui/app/App.hpp"

#include "ThrowawayQbt.hpp"

#include <QEventLoop>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

using omaseed::app::App;
using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::ServerPreferences;

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

private:
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

QTEST_MAIN(TestQbtSettingsLive)
#include "tst_qbtsettings_live.moc"
