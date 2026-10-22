// EngineKind (#84, DECISIONS C14 E3, C25 K8 and K9): the names an engine is written and shown
// by, and what omaseed makes of an engine and its version.

#include "core/engines/EngineKind.hpp"

#include <QTest>

using omaseed::core::engineDisplayName;
using omaseed::core::EngineKind;
using omaseed::core::engineKindFromString;
using omaseed::core::engineKindToString;
using omaseed::core::engineSupport;

class TestEngineKind : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void namesRoundTrip();
    void qbittorrentIsDrivenWhateverItsVersion();
    void transmissionNeedsRpcSix();
    void delugeNeedsTwoPointOne();
    void anEngineNotDrivenYetIsNamed();
};

void TestEngineKind::namesRoundTrip()
{
    for (const EngineKind kind : omaseed::core::kKnownEngines) {
        bool ok = false;
        QCOMPARE(engineKindFromString(engineKindToString(kind), &ok), kind);
        QVERIFY(ok);
        QVERIFY(!engineDisplayName(kind).isEmpty());
    }
    QCOMPARE(engineKindToString(EngineKind::Qbittorrent), QStringLiteral("qbittorrent"));
    QCOMPARE(engineDisplayName(EngineKind::Qbittorrent), QStringLiteral("qBittorrent"));
    bool ok = true;
    QCOMPARE(engineKindFromString(QStringLiteral("rtorrent"), &ok), EngineKind::Unknown);
    QVERIFY(!ok);
    QCOMPARE(engineKindFromString(QString(), &ok), EngineKind::Unknown);
    QVERIFY(ok);
}

void TestEngineKind::qbittorrentIsDrivenWhateverItsVersion()
{
    for (const QString &version : {QStringLiteral("v5.3.0"), QStringLiteral("v4.1.0"), QString()}) {
        const auto support = engineSupport(EngineKind::Qbittorrent, version, QString());
        QVERIFY(support.driven);
        QVERIFY(support.usable());
        QVERIFY(!support.versionRefused);
    }
}

void TestEngineKind::transmissionNeedsRpcSix()
{
    // 4.1 sends its RPC version with the 409; 4.0 and older send none.
    auto support = engineSupport(EngineKind::Transmission, QString(), QString());
    QVERIFY(support.versionRefused);
    QCOMPARE(support.refusal,
             QStringLiteral("This Transmission is older than 4.1: omaseed needs 4.1 or later"));
    support =
        engineSupport(EngineKind::Transmission, QStringLiteral("4.0.6"), QStringLiteral("5.4.0"));
    QVERIFY(support.versionRefused);
    QCOMPARE(support.refusal,
             QStringLiteral("Transmission 4.0.6 is too old: omaseed needs 4.1 or later"));
    QVERIFY(!support.advice.isEmpty());
    // New enough: refused only for not being driven yet (K9).
    support =
        engineSupport(EngineKind::Transmission, QStringLiteral("4.1.3"), QStringLiteral("6.0.0"));
    QVERIFY(!support.versionRefused);
    QCOMPARE(support.refusal, QStringLiteral("omaseed can't drive Transmission yet"));
}

void TestEngineKind::delugeNeedsTwoPointOne()
{
    auto support = engineSupport(EngineKind::Deluge, QStringLiteral("2.0.5"), QString());
    QVERIFY(support.versionRefused);
    QCOMPARE(support.refusal,
             QStringLiteral("Deluge 2.0.5 is too old: omaseed needs 2.1 or later"));
    // Unknown until signed in: not refused for its version.
    support = engineSupport(EngineKind::Deluge, QString(), QString());
    QVERIFY(!support.versionRefused);
    support = engineSupport(EngineKind::Deluge, QStringLiteral("2.2.0"), QString());
    QVERIFY(!support.versionRefused);
    QCOMPARE(support.minimum, QStringLiteral("2.1"));
}

void TestEngineKind::anEngineNotDrivenYetIsNamed()
{
    const auto support = engineSupport(EngineKind::Deluge, QStringLiteral("2.2.0"), QString());
    QVERIFY(!support.driven);
    QVERIFY(!support.usable());
    QCOMPARE(support.refusal, QStringLiteral("omaseed can't drive Deluge yet"));
    QVERIFY(support.advice.contains(QStringLiteral("qBittorrent")));
    // Nothing recognised: nothing to refuse here (the test says what it saw instead).
    QVERIFY(engineSupport(EngineKind::Unknown, QString(), QString()).usable());
}

QTEST_APPLESS_MAIN(TestEngineKind)

#include "tst_enginekind.moc"
