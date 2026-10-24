// core::MadeHere (#146, DECISIONS C27 CR7–CR9, board 49 §4): the made-here record, its JSON
// (no passkey, no announce URL), the 30 days, and the match for the site's copy: the made file
// itself, the same hash, the same files under another hash.

#include "core/MadeHere.hpp"

#include <QJsonDocument>
#include <QTest>

using omaseed::core::MadeHere;
using omaseed::core::TorrentFile;
using omaseed::core::TorrentFileEntry;

namespace {

TorrentFile made()
{
    TorrentFile file;
    file.name = QStringLiteral("field-recordings-2026");
    file.infoHashV1 = QString(40, QLatin1Char('a'));
    file.source = QStringLiteral("TA");
    file.multiFile = true;
    file.files = {{QStringLiteral("01 dawn chorus.flac"), 512},
                  {QStringLiteral("02 river at night.flac"), 498}};
    return file;
}

MadeHere::Record recordOf(const TorrentFile &file, const QDateTime &at)
{
    return MadeHere::Record::of(file,
                                QByteArrayLiteral("d8:announce…e"),
                                QStringLiteral("/mnt/media/share/field-recordings-2026"),
                                QStringLiteral("tracker-a.example"),
                                at);
}

} // namespace

class TestMadeHere : public QObject
{
    Q_OBJECT

private slots:
    void theRecordRoundTripsWithoutAKey();
    void matches();
    void aFolderMadeAgainReplacesItsRecord();
    void keptThirtyDays();
    void forgottenOnceTheSiteCopyIsIn();
};

void TestMadeHere::theRecordRoundTripsWithoutAKey()
{
    const QDateTime at(QDate(2026, 10, 22), QTime(14, 20), QTimeZone::UTC);
    MadeHere here;
    here.add(recordOf(made(), at));
    const QByteArray json = QJsonDocument(here.toJson()).toJson(QJsonDocument::Compact);
    QVERIFY(!json.contains("announce"));
    const MadeHere back = MadeHere::fromJson(QJsonDocument::fromJson(json).array());
    QCOMPARE(back.records().size(), 1);
    const MadeHere::Record record = back.records().first();
    QCOMPARE(record.name, QStringLiteral("field-recordings-2026"));
    QCOMPARE(record.infoHashV1, QString(40, QLatin1Char('a')));
    QCOMPARE(record.files.size(), 2);
    QCOMPARE(record.files.at(1).size, qint64(498));
    QVERIFY(record.multiFile);
    QCOMPARE(record.folder, QStringLiteral("/mnt/media/share/field-recordings-2026"));
    QCOMPARE(record.host, QStringLiteral("tracker-a.example"));
    QCOMPARE(record.source, QStringLiteral("TA"));
    QCOMPARE(record.digest, MadeHere::digestOf(QByteArrayLiteral("d8:announce…e")));
    QCOMPARE(record.madeAt, at);
    QCOMPARE(record.totalSize(), qint64(1010));
}

void TestMadeHere::matches()
{
    MadeHere here;
    here.add(recordOf(made(), QDateTime::currentDateTimeUtc()));
    const QString digest = MadeHere::digestOf(QByteArrayLiteral("d8:announce…e"));

    // The saved file itself.
    QCOMPARE(here.match(made(), digest).kind, MadeHere::Kind::TheMadeFile);
    // The site's copy, only the announce changed: the same hash.
    const MadeHere::Match same = here.match(made(), QStringLiteral("other"));
    QCOMPARE(same.kind, MadeHere::Kind::SameHash);
    QCOMPARE(same.index, 0);
    // The site changed the source: another hash, the same files (in another order).
    TorrentFile site = made();
    site.infoHashV1 = QString(40, QLatin1Char('b'));
    std::reverse(site.files.begin(), site.files.end());
    QCOMPARE(here.match(site, QString()).kind, MadeHere::Kind::SameFiles);
    // One file's size differs: no claim.
    site.files[0].size += 1;
    QCOMPARE(here.match(site, QString()).kind, MadeHere::Kind::None);
    // A v2 hash alone matches too.
    TorrentFile v2 = made();
    v2.infoHashV1.clear();
    v2.infoHashV2 = QString(64, QLatin1Char('c'));
    MadeHere hybrid;
    hybrid.add(recordOf(v2, QDateTime::currentDateTimeUtc()));
    TorrentFile other = v2;
    other.files.append({QStringLiteral("extra.txt"), 1});
    QCOMPARE(hybrid.match(other, QString()).kind, MadeHere::Kind::SameHash);
}

void TestMadeHere::aFolderMadeAgainReplacesItsRecord()
{
    MadeHere here;
    here.add(recordOf(made(), QDateTime::currentDateTimeUtc()));
    TorrentFile again = made();
    again.infoHashV1 = QString(40, QLatin1Char('d'));
    here.add(recordOf(again, QDateTime::currentDateTimeUtc()));
    QCOMPARE(here.records().size(), 1);
    QCOMPARE(here.records().first().infoHashV1, QString(40, QLatin1Char('d')));
}

void TestMadeHere::keptThirtyDays()
{
    const QDateTime now(QDate(2026, 10, 24), QTime(12, 0), QTimeZone::UTC);
    MadeHere here;
    here.add(recordOf(made(), now.addDays(-30)));
    QVERIFY(!here.dropExpired(now));
    QVERIFY(here.dropExpired(now.addDays(1)));
    QVERIFY(here.isEmpty());
}

void TestMadeHere::forgottenOnceTheSiteCopyIsIn()
{
    MadeHere here;
    here.add(recordOf(made(), QDateTime::currentDateTimeUtc()));
    TorrentFile site = made();
    site.infoHashV1 = QString(40, QLatin1Char('b'));
    here.forgetMatching(site, QString());
    QVERIFY(here.isEmpty());
    here.add(recordOf(made(), QDateTime::currentDateTimeUtc()));
    here.forget(0);
    QVERIFY(here.isEmpty());
}

QTEST_APPLESS_MAIN(TestMadeHere)
#include "tst_madehere.moc"
