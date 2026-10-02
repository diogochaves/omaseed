// FilesViewState (#9): the Files tab's hide-skipped preference and each torrent's filter, in
// state.json's "files" section, against a throwaway XDG_CONFIG_HOME.

#include "core/FilesViewState.hpp"
#include "core/StateFile.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace FilesViewState = omaseed::core::FilesViewState;
using omaseed::core::StateFile;

class TestFilesViewState : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void nothing_stored_reads_as_defaults();
    void hide_skipped_round_trips_and_keeps_other_sections();
    void a_filter_is_remembered_per_torrent();
    void an_empty_filter_forgets_the_torrent();
    void the_last_filter_comes_first_and_the_list_is_capped();
    void a_write_that_changes_nothing_leaves_the_file_alone();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QByteArray m_previousConfigHome;
};

void TestFilesViewState::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir->path().toUtf8());
}

void TestFilesViewState::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
    m_dir.reset();
}

void TestFilesViewState::nothing_stored_reads_as_defaults()
{
    QVERIFY(!FilesViewState::hideSkipped());
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("abc")), QString());
    QCOMPARE(FilesViewState::filterFor(QString()), QString());
    QVERIFY(!QFileInfo::exists(StateFile::path()));
}

void TestFilesViewState::hide_skipped_round_trips_and_keeps_other_sections()
{
    StateFile::writeSection(QStringLiteral("notifications"),
                            QJsonObject{{QStringLiteral("completed"), false}});
    FilesViewState::setHideSkipped(true);
    QVERIFY(FilesViewState::hideSkipped());
    QCOMPARE(StateFile::readSection(QStringLiteral("files")).value(QStringLiteral("hideSkipped")),
             QJsonValue(true));
    QCOMPARE(
        StateFile::readSection(QStringLiteral("notifications")).value(QStringLiteral("completed")),
        QJsonValue(false));
    FilesViewState::setHideSkipped(false);
    QVERIFY(!FilesViewState::hideSkipped());
}

void TestFilesViewState::a_filter_is_remembered_per_torrent()
{
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QStringLiteral("*.srt"));
    FilesViewState::rememberFilter(QStringLiteral("bbbb"), QStringLiteral("mkv|mp4"));
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("aaaa")), QStringLiteral("*.srt"));
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("bbbb")), QStringLiteral("mkv|mp4"));
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("cccc")), QString());

    // A new filter for the same torrent replaces the old one; a hash-less one is ignored.
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QStringLiteral("subs/"));
    FilesViewState::rememberFilter(QString(), QStringLiteral("lost"));
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("aaaa")), QStringLiteral("subs/"));
    const QJsonArray filters =
        StateFile::readSection(QStringLiteral("files")).value(QStringLiteral("filters")).toArray();
    QCOMPARE(filters.size(), 2);
}

void TestFilesViewState::an_empty_filter_forgets_the_torrent()
{
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QStringLiteral("*.srt"));
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QString());
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("aaaa")), QString());
    QVERIFY(StateFile::readSection(QStringLiteral("files"))
                .value(QStringLiteral("filters"))
                .toArray()
                .isEmpty());
}

void TestFilesViewState::the_last_filter_comes_first_and_the_list_is_capped()
{
    for (int i = 0; i < FilesViewState::kMaxFilters + 5; ++i)
        FilesViewState::rememberFilter(QStringLiteral("hash%1").arg(i),
                                       QStringLiteral("f%1").arg(i));
    const QJsonArray filters =
        StateFile::readSection(QStringLiteral("files")).value(QStringLiteral("filters")).toArray();
    QCOMPARE(filters.size(), FilesViewState::kMaxFilters);
    QCOMPARE(filters.first().toObject().value(QStringLiteral("hash")).toString(),
             QStringLiteral("hash%1").arg(FilesViewState::kMaxFilters + 4));
    // The oldest five fell off the end.
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("hash4")), QString());
    QCOMPARE(FilesViewState::filterFor(QStringLiteral("hash5")), QStringLiteral("f5"));
}

void TestFilesViewState::a_write_that_changes_nothing_leaves_the_file_alone()
{
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QStringLiteral("*.srt"));
    FilesViewState::setHideSkipped(true);
    const QDateTime written = QFileInfo(StateFile::path()).lastModified();
    QTest::qWait(20);
    FilesViewState::rememberFilter(QStringLiteral("aaaa"), QStringLiteral("*.srt"));
    FilesViewState::setHideSkipped(true);
    FilesViewState::rememberFilter(QStringLiteral("zzzz"), QString());
    QCOMPARE(QFileInfo(StateFile::path()).lastModified(), written);
}

QTEST_GUILESS_MAIN(TestFilesViewState)
#include "tst_filesviewstate.moc"
