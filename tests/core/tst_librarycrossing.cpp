// LibraryCrossing: what a torrents file of one engine becomes on another (docs/57 §5,
// docs/DECISIONS.md C40 X4, X5; #88). Every row of the ruling, both directions, and the same
// engine both sides.

#include "core/LibraryCrossing.hpp"

#include <QTest>

using omaseed::core::EngineKind;
using omaseed::core::LibraryCrossing;
using omaseed::core::TorrentRecord;

using Grouping = LibraryCrossing::Grouping;
using Kind = LibraryCrossing::NameKind;
using Loss = LibraryCrossing::Loss;
using LossKind = LibraryCrossing::LossKind;
using Name = LibraryCrossing::Name;
using Outcome = LibraryCrossing::Outcome;

namespace {

constexpr EngineKind Q = EngineKind::Qbittorrent;
constexpr EngineKind T = EngineKind::Transmission;
constexpr EngineKind D = EngineKind::Deluge;

TorrentRecord record(const QString &category, const QStringList &tags = {})
{
    TorrentRecord r;
    r.category = category;
    r.tags = tags;
    return r;
}

Outcome kept(Kind kind, const QString &name, int torrents)
{
    return Outcome{true, kind, name, torrents};
}

Outcome left(int torrents)
{
    Outcome o;
    o.torrents = torrents;
    return o;
}

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

// A qBittorrent's records with one of everything: 1 seeding-time limit, 2 idle limits,
// actions Remove, RemoveWithContent and EnableSuperSeeding (and Default, Stop, none), 2 on
// automatic management, 1 incomplete folder, 3 with tags.
QList<TorrentRecord> everything()
{
    QList<TorrentRecord> records;
    TorrentRecord r = record(s("Films"), {s("keep")});
    r.seedingTimeLimit = 0;
    r.inactiveSeedingTimeLimit = 30;
    r.shareLimitAction = s("Remove");
    r.autoTmm = true;
    r.downloadPath = s("/data/incomplete");
    records.append(r);
    r = record(QString(), {s("keep"), s("hd")});
    r.inactiveSeedingTimeLimit = 0;
    r.shareLimitAction = s("RemoveWithContent");
    r.autoTmm = true;
    records.append(r);
    r = record(s("Films"), {s("hd")});
    r.seedingTimeLimit = -1; // no limit: nothing to keep
    r.shareLimitAction = s("EnableSuperSeeding");
    records.append(r);
    r = record(s("Films"));
    r.shareLimitAction = s("Stop");
    records.append(r);
    r = record(s("Films"));
    r.shareLimitAction = s("Default");
    records.append(r);
    records.append(record(QString())); // the defaults: -2 limits, no action
    return records;
}

} // namespace

Q_DECLARE_METATYPE(TorrentRecord)
Q_DECLARE_METATYPE(Grouping)

class LibraryCrossingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void delugeSpelling_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("label");
        QTest::newRow("words") << s("Field Recordings") << s("field-recordings");
        QTest::newRow("kept") << s("linux_iso-2") << s("linux_iso-2");
        QTest::newRow("a run is one dash") << s("TV / Shows!") << s("tv-shows");
        QTest::newRow("ends trimmed") << s(" (films) ") << s("films");
        QTest::newRow("its own dashes stay") << s("a--b") << s("a--b");
        QTest::newRow("not ascii") << s("Música") << s("m-sica");
        QTest::newRow("nothing left") << s("¡¿?") << QString();
        QTest::newRow("only dashes") << s("--") << QString();
        QTest::newRow("empty") << QString() << QString();
    }

    void delugeSpelling()
    {
        QFETCH(QString, name);
        QFETCH(QString, label);
        QCOMPARE(LibraryCrossing::delugeLabel(name), label);
    }

    // ---- X4: one record's grouping on the target ---------------------------------------

    void grouping_data()
    {
        QTest::addColumn<EngineKind>("from");
        QTest::addColumn<EngineKind>("to");
        QTest::addColumn<TorrentRecord>("source");
        QTest::addColumn<Grouping>("expected");

        const TorrentRecord qbt = record(s("Films"), {s("keep"), s("hd")});
        const TorrentRecord onlyTags = record(QString(), {s("keep"), s("hd")});
        const TorrentRecord labels = record(QString(), {s("Field Recordings"), s("keep"), s("hd")});
        const TorrentRecord oneLabel = record(s("films"));
        const TorrentRecord none = record(QString());

        QTest::newRow("qbt to transmission: the category, then the tags")
            << Q << T << qbt << Grouping{QString(), {s("Films"), s("keep"), s("hd")}, {}};
        QTest::newRow("qbt to transmission: no category")
            << Q << T << onlyTags << Grouping{QString(), {s("keep"), s("hd")}, {}};
        QTest::newRow("qbt to transmission: a tag named as the category is one label")
            << Q << T << record(s("keep"), {s("keep"), s("hd")})
            << Grouping{QString(), {s("keep"), s("hd")}, {}};
        QTest::newRow("transmission to qbt: the first label is the category")
            << T << Q << labels << Grouping{s("Field Recordings"), {s("keep"), s("hd")}, {}};
        QTest::newRow("transmission to qbt: one label")
            << T << Q << record(QString(), {s("films")}) << Grouping{s("films"), {}, {}};
        QTest::newRow("qbt to deluge: the category in its spelling, tags left")
            << Q << D << record(s("Field Recordings"), {s("keep"), s("hd")})
            << Grouping{s("field-recordings"), {}, {s("keep"), s("hd")}};
        QTest::newRow("qbt to deluge: no category, no label")
            << Q << D << onlyTags << Grouping{QString(), {}, {s("keep"), s("hd")}};
        QTest::newRow("qbt to deluge: a category with nothing to spell is left")
            << Q << D << record(s("¡!")) << Grouping{QString(), {}, {s("¡!")}};
        QTest::newRow("deluge to qbt: the label is the category")
            << D << Q << oneLabel << Grouping{s("films"), {}, {}};
        QTest::newRow("transmission to deluge: the first label, the rest left")
            << T << D << labels << Grouping{s("field-recordings"), {}, {s("keep"), s("hd")}};
        QTest::newRow("deluge to transmission: the one label")
            << D << T << oneLabel << Grouping{QString(), {s("films")}, {}};

        QTest::newRow("qbt to qbt: unchanged")
            << Q << Q << qbt << Grouping{s("Films"), {s("keep"), s("hd")}, {}};
        QTest::newRow("transmission to transmission: unchanged")
            << T << T << labels << Grouping{QString(), labels.tags, {}};
        QTest::newRow("deluge to deluge: unchanged, not spelled again")
            << D << D << record(s("Odd Label")) << Grouping{s("Odd Label"), {}, {}};
        QTest::newRow("a file without an engine is qbt's")
            << EngineKind::Unknown << T << qbt
            << Grouping{QString(), {s("Films"), s("keep"), s("hd")}, {}};

        for (const EngineKind from : {Q, T, D}) {
            for (const EngineKind to : {Q, T, D}) {
                const QByteArray row =
                    "nothing: " + omaseed::core::engineKindToString(from).toUtf8() + " to " +
                    omaseed::core::engineKindToString(to).toUtf8();
                QTest::newRow(row.constData()) << from << to << none << Grouping{};
            }
        }
    }

    void grouping()
    {
        QFETCH(EngineKind, from);
        QFETCH(EngineKind, to);
        QFETCH(TorrentRecord, source);
        QFETCH(Grouping, expected);
        const Grouping got = LibraryCrossing::grouping(from, to, source);
        QCOMPARE(got.category, expected.category);
        QCOMPARE(got.tags, expected.tags);
        QCOMPARE(got.leftBehind, expected.leftBehind);
    }

    // ---- the names block ---------------------------------------------------------------

    void namesQbtToTransmission()
    {
        const QList<TorrentRecord> records{record(s("Films"), {s("keep")}),
                                           record(s("Films"), {s("hd"), s("keep")}),
                                           record(QString(), {s("Films")})};
        const QList<Name> expected{
            {Kind::Category, s("Films"), {kept(Kind::Label, s("Films"), 2)}},
            {Kind::Tag, s("keep"), {kept(Kind::Label, s("keep"), 2)}},
            {Kind::Tag, s("hd"), {kept(Kind::Label, s("hd"), 1)}},
            // A tag of the category's name is its own entry.
            {Kind::Tag, s("Films"), {kept(Kind::Label, s("Films"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(Q, T, records), expected);
    }

    // A label that is first for some torrents and later for others goes both ways.
    void namesTransmissionToQbt()
    {
        const QList<TorrentRecord> records{record(QString(), {s("films"), s("keep")}),
                                           record(QString(), {s("keep"), s("films")}),
                                           record(QString(), {s("films")})};
        const QList<Name> expected{
            {Kind::Label,
             s("films"),
             {kept(Kind::Category, s("films"), 2), kept(Kind::Tag, s("films"), 1)}},
            {Kind::Label,
             s("keep"),
             {kept(Kind::Tag, s("keep"), 1), kept(Kind::Category, s("keep"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(T, Q, records), expected);
    }

    void namesQbtToDeluge()
    {
        const QList<TorrentRecord> records{record(s("Field Recordings"), {s("keep")}),
                                           record(s("field recordings")),
                                           record(QString(), {s("keep"), s("hd")}),
                                           record(s("¿?"))};
        const QList<Name> expected{
            {Kind::Category, s("Field Recordings"), {kept(Kind::Label, s("field-recordings"), 1)}},
            {Kind::Tag, s("keep"), {left(2)}},
            {Kind::Category, s("field recordings"), {kept(Kind::Label, s("field-recordings"), 1)}},
            {Kind::Tag, s("hd"), {left(1)}},
            {Kind::Category, s("¿?"), {left(1)}},
        };
        QCOMPARE(LibraryCrossing::names(Q, D, records), expected);
    }

    void namesDelugeToQbt()
    {
        const QList<TorrentRecord> records{
            record(s("films")), record(s("music")), record(s("films")), record(QString())};
        const QList<Name> expected{
            {Kind::Label, s("films"), {kept(Kind::Category, s("films"), 2)}},
            {Kind::Label, s("music"), {kept(Kind::Category, s("music"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(D, Q, records), expected);
    }

    // First for one torrent (the label), later for another (left behind).
    void namesTransmissionToDeluge()
    {
        const QList<TorrentRecord> records{record(QString(), {s("Films"), s("keep")}),
                                           record(QString(), {s("keep"), s("Films")})};
        const QList<Name> expected{
            {Kind::Label, s("Films"), {kept(Kind::Label, s("films"), 1), left(1)}},
            {Kind::Label, s("keep"), {left(1), kept(Kind::Label, s("keep"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(T, D, records), expected);
    }

    void namesDelugeToTransmission()
    {
        const QList<Name> expected{
            {Kind::Label, s("films"), {kept(Kind::Label, s("films"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(D, T, {record(s("films"))}), expected);
    }

    void namesSameEngine()
    {
        const QList<Name> qbt{
            {Kind::Category, s("Films"), {kept(Kind::Category, s("Films"), 1)}},
            {Kind::Tag, s("keep"), {kept(Kind::Tag, s("keep"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(Q, Q, {record(s("Films"), {s("keep")})}), qbt);
        const QList<Name> transmission{
            {Kind::Label, s("Films"), {kept(Kind::Label, s("Films"), 1)}},
            {Kind::Label, s("keep"), {kept(Kind::Label, s("keep"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(T, T, {record(QString(), {s("Films"), s("keep")})}),
                 transmission);
        const QList<Name> deluge{
            {Kind::Label, s("films"), {kept(Kind::Label, s("films"), 1)}},
        };
        QCOMPARE(LibraryCrossing::names(D, D, {record(s("films"))}), deluge);
        QVERIFY(LibraryCrossing::names(Q, T, {}).isEmpty());
    }

    // ---- X5: what the target can't keep ------------------------------------------------

    void lossesQbtToTransmission()
    {
        const QList<Loss> expected{{LossKind::SeedingTimeLimit, 1},
                                   {LossKind::ShareLimitAction, 3},
                                   {LossKind::AutoManagement, 2},
                                   {LossKind::IncompleteFolder, 1}};
        QCOMPARE(LibraryCrossing::losses(Q, T, everything()), expected);
        QCOMPARE(LibraryCrossing::losses(EngineKind::Unknown, T, everything()), expected);
    }

    void lossesQbtToDeluge()
    {
        const QList<Loss> expected{{LossKind::SeedingTimeLimit, 1},
                                   {LossKind::IdleLimit, 2},
                                   {LossKind::ShareLimitAction, 2},
                                   {LossKind::AutoManagement, 2},
                                   {LossKind::IncompleteFolder, 1},
                                   {LossKind::GroupingNames, 3}};
        QCOMPARE(LibraryCrossing::losses(Q, D, everything()), expected);
    }

    // A Transmission's idle limit and extra labels are Deluge's to lose; one label is kept.
    void lossesTransmissionToDeluge()
    {
        TorrentRecord idle = record(QString(), {s("films")});
        idle.inactiveSeedingTimeLimit = 30;
        idle.shareLimitAction = s("Stop");
        const QList<TorrentRecord> records{idle,
                                           record(QString(), {s("films"), s("keep"), s("hd")}),
                                           record(QString(), {s("music"), s("keep")}),
                                           record(QString())};
        const QList<Loss> expected{{LossKind::IdleLimit, 1}, {LossKind::GroupingNames, 2}};
        QCOMPARE(LibraryCrossing::losses(T, D, records), expected);
    }

    // Deluge removes at its limit, Transmission only stops; a Deluge has nothing else to lose.
    void lossesDelugeToTransmission()
    {
        TorrentRecord remove = record(s("films"));
        remove.shareLimitAction = s("Remove");
        TorrentRecord stop = record(s("films"));
        stop.shareLimitAction = s("Stop");
        const QList<Loss> expected{{LossKind::ShareLimitAction, 1}};
        QCOMPARE(LibraryCrossing::losses(D, T, {remove, stop}), expected);
        QVERIFY(LibraryCrossing::losses(D, T, {stop}).isEmpty());
    }

    // qBittorrent has a place for everything: Deluge's Remove is one of its actions.
    void lossesIntoQbt()
    {
        TorrentRecord remove = record(s("films"));
        remove.shareLimitAction = s("Remove");
        QVERIFY(LibraryCrossing::losses(D, Q, {remove}).isEmpty());
        TorrentRecord idle = record(QString(), {s("films"), s("keep")});
        idle.inactiveSeedingTimeLimit = 30;
        QVERIFY(LibraryCrossing::losses(T, Q, {idle}).isEmpty());
    }

    void lossesSameEngine()
    {
        QVERIFY(LibraryCrossing::losses(Q, Q, everything()).isEmpty());
        QVERIFY(LibraryCrossing::losses(T, T, everything()).isEmpty());
        QVERIFY(LibraryCrossing::losses(D, D, everything()).isEmpty());
        QVERIFY(LibraryCrossing::losses(Q, T, {}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(LibraryCrossingTest)
#include "tst_librarycrossing.moc"
