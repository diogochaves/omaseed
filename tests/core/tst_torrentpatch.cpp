// TorrentPatch and Torrent::apply() (#81, docs/36 §4.1): a patch merges only the fields it
// carries and reports the ones that moved, by field, never by an engine's key.

#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"
#include "core/engines/TorrentPatch.hpp"

#include <QTest>

#include <limits>

using namespace omaseed::core;

class TestTorrentPatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fieldsAreASet();
    void applyMergesOnlyWhatThePatchCarries();
    void applyReportsOnlyRealChanges();
    void ofCopiesOneField();
    void everyFieldFeedsTheTorrentRole();
    void noEstimateSortsLast();
};

void TestTorrentPatch::fieldsAreASet()
{
    TorrentFields fields;
    QVERIFY(fields.isEmpty());
    fields.set(TorrentField::MagnetUri); // the last one: past 32 bits
    fields.set(TorrentField::Name);
    QVERIFY(fields.has(TorrentField::MagnetUri));
    QVERIFY(fields.has(TorrentField::Name));
    QVERIFY(!fields.has(TorrentField::Size));
    QCOMPARE(fields.count(), 2);
    QList<TorrentField> seen;
    fields.forEach([&seen](TorrentField field) { seen.append(field); });
    QCOMPARE(seen, QList<TorrentField>({TorrentField::Name, TorrentField::MagnetUri}));
    fields.clear(TorrentField::Name);
    QCOMPARE(fields, TorrentFields({TorrentField::MagnetUri}));
    QCOMPARE(TorrentFields::all().count(), int(TorrentField::Count));
}

void TestTorrentPatch::applyMergesOnlyWhatThePatchCarries()
{
    Torrent t;
    t.name = QStringLiteral("before");
    t.dlspeed = 10;

    TorrentPatch patch(QStringLiteral("aa"));
    patch.set(&Torrent::dlspeed, TorrentField::DlSpeed, qint64(99));
    patch.values.name = QStringLiteral("not carried"); // no field, so not read
    const TorrentFields changed = t.apply(patch);

    QCOMPARE(changed, TorrentFields({TorrentField::DlSpeed}));
    QCOMPARE(t.dlspeed, qint64(99));
    QCOMPARE(t.name, QStringLiteral("before"));
    QVERIFY(t.hash.isEmpty()); // apply() never moves the identity
}

void TestTorrentPatch::applyReportsOnlyRealChanges()
{
    Torrent t;
    TorrentPatch patch;
    patch.set(&Torrent::eta, TorrentField::Eta, std::optional<qint64>());
    patch.set(&Torrent::ratioLimit, TorrentField::RatioLimit, RatioLimit::global());
    // Both already hold those values: nothing moved.
    QVERIFY(t.apply(patch).isEmpty());

    patch.set(&Torrent::eta, TorrentField::Eta, std::optional<qint64>(60));
    patch.set(&Torrent::ratioLimit, TorrentField::RatioLimit, RatioLimit::unlimited());
    QCOMPARE(t.apply(patch), TorrentFields({TorrentField::Eta, TorrentField::RatioLimit}));
    QCOMPARE(t.eta, std::optional<qint64>(60));

    patch.set(&Torrent::ratio, TorrentField::Ratio, std::numeric_limits<double>::infinity());
    QCOMPARE(t.apply(patch), TorrentFields({TorrentField::Ratio}));
    QVERIFY(t.apply(patch).isEmpty()); // infinity equals itself
}

void TestTorrentPatch::ofCopiesOneField()
{
    Torrent t;
    t.hash = QStringLiteral("aa");
    t.seqDl = true;
    t.name = QStringLiteral("x");
    const TorrentPatch patch = TorrentPatch::of(t, TorrentField::SeqDl);
    QCOMPARE(patch.hash, QStringLiteral("aa"));
    QCOMPARE(patch.fields, TorrentFields({TorrentField::SeqDl}));

    Torrent other;
    QCOMPARE(other.apply(patch), TorrentFields({TorrentField::SeqDl}));
    QVERIFY(other.seqDl);
    QVERIFY(other.name.isEmpty());
}

void TestTorrentPatch::everyFieldFeedsTheTorrentRole()
{
    // Fields with roles of their own name them; the rest only ride on TorrentRole, which
    // every change carries (TorrentModel::applyPartial).
    const QList<TorrentField> roleless{TorrentField::DownloadPath,
                                       TorrentField::SuperSeeding,
                                       TorrentField::AutoTmm,
                                       TorrentField::UploadedSession};
    for (int i = 0; i < int(TorrentField::Count); ++i) {
        const auto field = TorrentField(i);
        QCOMPARE(TorrentModel::rolesForField(field).isEmpty(), roleless.contains(field));
    }
    QVERIFY(
        TorrentModel::rolesForField(TorrentField::Status).contains(TorrentModel::StateTextRole));
}

void TestTorrentPatch::noEstimateSortsLast()
{
    TorrentModel model;
    TorrentPatch none(QStringLiteral("aa"));
    none.set(&Torrent::name, TorrentField::Name, QStringLiteral("a"));
    TorrentPatch soon(QStringLiteral("bb"));
    soon.set(&Torrent::eta, TorrentField::Eta, std::optional<qint64>(8639999));
    model.applyFullUpdate({none, soon});
    const qint64 noneKey = model.data(model.index(0), TorrentModel::EtaRole).toLongLong();
    const qint64 soonKey = model.data(model.index(1), TorrentModel::EtaRole).toLongLong();
    QCOMPARE(noneKey, TorrentModel::kNoEstimateSortKey);
    QVERIFY(soonKey < noneKey);
}

QTEST_GUILESS_MAIN(TestTorrentPatch)
#include "tst_torrentpatch.moc"
