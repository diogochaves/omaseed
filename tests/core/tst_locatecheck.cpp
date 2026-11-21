// DECISIONS C50 GT2 (#294): `do locate`'s find, each wanted file's relative path and size in
// a folder, over a table standing for the disk (PLAN Q9).

#include "core/LocateCheck.hpp"

#include <QHash>
#include <QTest>

using omaseed::core::ContentFile;
namespace LocateCheck = omaseed::core::LocateCheck;

namespace {

ContentFile file(const QString &path, qint64 size, double progress = 1.0, int priority = 1)
{
    ContentFile out;
    out.path = path;
    out.size = size;
    out.progress = progress;
    out.priority = priority;
    return out;
}

LocateCheck::Stat disk(const QHash<QString, qint64> &table)
{
    return [table](const QString &path) -> std::optional<qint64> {
        if (!table.contains(path))
            return std::nullopt;
        return table.value(path);
    };
}

const QList<ContentFile> kRecordings{file(QStringLiteral("Rec/01.flac"), 100),
                                     file(QStringLiteral("Rec/02.flac"), 200),
                                     file(QStringLiteral("Rec/sub/03.flac"), 300)};

} // namespace

class TestLocateCheck : public QObject
{
    Q_OBJECT

private slots:
    void allThere()
    {
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b/"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/02.flac"), 200},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 3);
        QCOMPARE(result.files, 3);
        QVERIFY(result.all());
        QCOMPARE(result.bytes, qint64(600));
        QVERIFY(result.firstMissing.isEmpty());
    }

    void noneFound()
    {
        const auto result = LocateCheck::check(kRecordings, QStringLiteral("/data/b"), disk({}));
        QCOMPARE(result.found, 0);
        QCOMPARE(result.files, 3);
        QVERIFY(!result.all());
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/01.flac"));
    }

    void partial()
    {
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 2);
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/02.flac"));
        QCOMPARE(result.bytes, qint64(400));
    }

    void sizeDiffers()
    {
        // A complete file of another size is another file.
        const auto result =
            LocateCheck::check(kRecordings,
                               QStringLiteral("/data/b"),
                               disk({{QStringLiteral("/data/b/Rec/01.flac"), 100},
                                     {QStringLiteral("/data/b/Rec/02.flac"), 199},
                                     {QStringLiteral("/data/b/Rec/sub/03.flac"), 300}}));
        QCOMPARE(result.found, 2);
        QCOMPARE(result.firstMissing, QStringLiteral("Rec/02.flac"));
    }

    void skippedFileAbsent()
    {
        const QList<ContentFile> files{file(QStringLiteral("Rec/01.flac"), 100),
                                       file(QStringLiteral("Rec/extra.nfo"), 10, 0.0, 0)};
        const auto result = LocateCheck::check(
            files, QStringLiteral("/d"), disk({{QStringLiteral("/d/Rec/01.flac"), 100}}));
        QCOMPARE(result.files, 1);
        QCOMPARE(result.found, 1);
        QVERIFY(result.all());
    }

    void incompleteFileSmaller()
    {
        const QList<ContentFile> files{file(QStringLiteral("a.bin"), 100, 0.5),
                                       file(QStringLiteral("b.bin"), 100, 0.5)};
        const auto result = LocateCheck::check(
            files,
            QStringLiteral("/d"),
            disk({{QStringLiteral("/d/a.bin"), 60}, {QStringLiteral("/d/b.bin"), 120}}));
        QCOMPARE(result.found, 1); // larger than the torrent's: not its file
        QCOMPARE(result.firstMissing, QStringLiteral("b.bin"));
    }

    void noWantedFile()
    {
        const auto result = LocateCheck::check({}, QStringLiteral("/d"), disk({}));
        QCOMPARE(result.files, 0);
        QVERIFY(!result.all());
    }

    void shownName()
    {
        QCOMPARE(LocateCheck::shownName(kRecordings, QStringLiteral("Rec/sub/03.flac")),
                 QStringLiteral("sub/03.flac"));
        const QList<ContentFile> loose{file(QStringLiteral("a.bin"), 1),
                                       file(QStringLiteral("Dir/b.bin"), 1)};
        QCOMPARE(LocateCheck::shownName(loose, QStringLiteral("Dir/b.bin")),
                 QStringLiteral("Dir/b.bin"));
    }

    void statLocal()
    {
        QVERIFY(!LocateCheck::statLocal(QStringLiteral("/nonexistent/omaseed/x")).has_value());
        QVERIFY(!LocateCheck::statLocal(QStringLiteral("/")).has_value()); // a folder
    }
};

QTEST_GUILESS_MAIN(TestLocateCheck)
#include "tst_locatecheck.moc"
