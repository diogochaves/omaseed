#include "core/LooseFiles.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <unistd.h>

using omaseed::core::LooseFile;
using omaseed::core::LooseFiles;
using omaseed::core::ServerFolder;

namespace {

const QDateTime kOld = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);
const QDateTime kNow = QDateTime(QDate(2026, 10, 18), QTime(12, 0), QTimeZone::UTC);

void write(const QString &path, qint64 size = 10, const QDateTime &modified = kOld)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(size, 'x'));
    file.close();
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
}

QStringList listed(const LooseFiles::Result &result, LooseFile::Verdict verdict)
{
    QStringList out;
    for (const LooseFile &file : result.files) {
        if (file.verdict == verdict)
            out << file.relative;
    }
    return out;
}

} // namespace

// Board 42 §7 (DECISIONS C15 M9): which files of a folder no torrent owns.
class TestLooseFiles : public QObject
{
    Q_OBJECT

private slots:
    void sortsAFolder()
    {
        QTemporaryDir tmp;
        const QString games = tmp.filePath(QStringLiteral("games"));
        // Two torrents with a root folder, one single-file torrent, one added without a
        // subfolder (its files sit loose in games), and what nobody owns.
        write(games + QStringLiteral("/Open Arena/arena.bin"));
        write(games + QStringLiteral("/Open Arena/extra/readme.txt"));
        write(games + QStringLiteral("/Shareware Shelf/disc1.iso"));
        write(games + QStringLiteral("/single.iso"));
        write(games + QStringLiteral("/manual-1.pdf"));
        write(games + QStringLiteral("/manual-2.pdf"));
        write(games + QStringLiteral("/disk-images/arena-gold.iso"), 4000);
        write(games + QStringLiteral("/readme.txt"), 2);
        write(games + QStringLiteral("/.0123abcd.parts"));
        write(games + QStringLiteral("/half.bin.!qB"));
        write(games + QStringLiteral("/Thumbs.db"));
        write(games + QStringLiteral("/handheld/DESKTOP.INI"));
        write(games + QStringLiteral("/handheld/new-dump.img"), 50, kNow.addSecs(-180));
        write(games + QStringLiteral("/Open Arena Extras/x.bin"));

        const QStringList owned{games + QStringLiteral("/Open Arena"),
                                games + QStringLiteral("/Shareware Shelf/"),
                                games + QStringLiteral("/single.iso"),
                                games + QStringLiteral("/manual-1.pdf"),
                                games + QStringLiteral("/manual-2.pdf")};
        const LooseFiles::Result result = LooseFiles::scan(games, owned, kNow);
        QVERIFY(result.complete);
        QCOMPARE(listed(result, LooseFile::Verdict::Loose),
                 QStringList({QStringLiteral("disk-images/arena-gold.iso"),
                              QStringLiteral("Open Arena Extras/x.bin"),
                              QStringLiteral("readme.txt")}));
        QCOMPARE(result.loose, 3);
        QCOMPARE(result.looseBytes, qint64(4000 + 10 + 2));
        QCOMPARE(listed(result, LooseFile::Verdict::NeverCounts),
                 QStringList({QStringLiteral(".0123abcd.parts"),
                              QStringLiteral("half.bin.!qB"),
                              QStringLiteral("handheld/DESKTOP.INI"),
                              QStringLiteral("Thumbs.db")}));
        QCOMPARE(listed(result, LooseFile::Verdict::Recent),
                 QStringList({QStringLiteral("handheld/new-dump.img")}));
        const LooseFile recent = result.files.at(result.files.indexOf(
            *std::find_if(result.files.cbegin(), result.files.cend(), [](const LooseFile &f) {
                return f.verdict == LooseFile::Verdict::Recent;
            })));
        QCOMPARE(recent.why, QStringLiteral("changed 3 min ago"));
        QCOMPARE(result.unknown, 0);
    }

    void contentPathNotSavePath()
    {
        // qui's April 2026 false positive: a torrent saved in games owns its own folder, not
        // the save path. A folder named like a torrent but not its content is loose.
        QTemporaryDir tmp;
        const QString games = tmp.filePath(QStringLiteral("games"));
        write(games + QStringLiteral("/Pack/a.bin"));
        write(games + QStringLiteral("/Pack (copy)/a.bin"));
        const LooseFiles::Result result =
            LooseFiles::scan(games, {games + QStringLiteral("/Pack")}, kNow);
        QCOMPARE(listed(result, LooseFile::Verdict::Loose),
                 QStringList({QStringLiteral("Pack (copy)/a.bin")}));
    }

    void unreadableIsUnknownNeverLoose()
    {
        if (::geteuid() == 0)
            QSKIP("root reads every file");
        QTemporaryDir tmp;
        const QString games = tmp.filePath(QStringLiteral("games"));
        write(games + QStringLiteral("/launcher/cache/blob"));
        write(games + QStringLiteral("/secret.bin"));
        write(games + QStringLiteral("/fine.bin"));
        QFile::setPermissions(games + QStringLiteral("/secret.bin"), QFileDevice::WriteOwner);
        QFile::setPermissions(games + QStringLiteral("/launcher/cache"), QFileDevice::WriteOwner);

        const LooseFiles::Result result = LooseFiles::scan(games, {}, kNow);
        QCOMPARE(listed(result, LooseFile::Verdict::Unknown),
                 QStringList({QStringLiteral("launcher/cache"), QStringLiteral("secret.bin")}));
        QCOMPARE(listed(result, LooseFile::Verdict::Loose),
                 QStringList({QStringLiteral("fine.bin")}));
        QFile::setPermissions(games + QStringLiteral("/launcher/cache"),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner);
    }

    void linksAreUnknown()
    {
        QTemporaryDir tmp;
        const QString games = tmp.filePath(QStringLiteral("games"));
        write(games + QStringLiteral("/real.bin"));
        QVERIFY(QFile::link(games + QStringLiteral("/real.bin"), games + QStringLiteral("/l.bin")));
        const LooseFiles::Result result = LooseFiles::scan(games, {}, kNow);
        QCOMPARE(listed(result, LooseFile::Verdict::Unknown),
                 QStringList({QStringLiteral("l.bin")}));
        QCOMPARE(listed(result, LooseFile::Verdict::Loose),
                 QStringList({QStringLiteral("real.bin")}));
    }

    void aFolderInsideATorrentIsAllOwned()
    {
        QTemporaryDir tmp;
        const QString pack = tmp.filePath(QStringLiteral("games/Pack"));
        write(pack + QStringLiteral("/sub/a.bin"));
        const LooseFiles::Result result =
            LooseFiles::scan(pack + QStringLiteral("/sub"), {pack}, kNow);
        QVERIFY(result.complete);
        QVERIFY(result.files.isEmpty());
    }

    void aMissingFolderIsIncomplete()
    {
        const LooseFiles::Result result =
            LooseFiles::scan(QStringLiteral("/nonexistent/omaseed/folder"), {}, kNow);
        QVERIFY(!result.complete);
        QVERIFY(result.files.isEmpty());
    }

    void stopEndsTheWalk()
    {
        QTemporaryDir tmp;
        for (int i = 0; i < 200; ++i)
            write(tmp.filePath(QStringLiteral("f/%1.bin").arg(i)), 1);
        const LooseFiles::Result result = LooseFiles::scan(
            tmp.filePath(QStringLiteral("f")), {}, kNow, std::chrono::minutes(15), [] {
                return true;
            });
        QVERIFY(!result.complete);
        QVERIFY(result.files.size() < 200);
    }

    // #102, C16 L13: a server whose disks this machine can't see names what it lists.
    void onServerNamesWhatNoTorrentOwns()
    {
        const QString root = QStringLiteral("/volume1/games");
        const QStringList folders{root, root + QStringLiteral("/handheld")};
        const QStringList owned{root + QStringLiteral("/Arcade Set 01"),
                                root + QStringLiteral("/handheld/Pack 01"),
                                root + QStringLiteral("/handheld/loose-rom.bin")};
        QHash<QString, ServerFolder> listed;

        LooseFiles::OnServer out = LooseFiles::onServer(root, folders, listed, owned);
        QVERIFY(!out.complete);
        QCOMPARE(out.needListings, folders);
        QVERIFY(out.names.isEmpty());

        ServerFolder games;
        games.state = ServerFolder::State::Listed;
        games.names = {QStringLiteral("Arcade Set 01"),
                       QStringLiteral("handheld"),
                       QStringLiteral("manual.pdf"),
                       QStringLiteral("Thumbs.db")};
        ServerFolder handheld;
        handheld.state = ServerFolder::State::Listed;
        handheld.names = {QStringLiteral("Pack 01"),
                          QStringLiteral("loose-rom.bin"),
                          QStringLiteral("launcher-config"),
                          QStringLiteral(".abc.parts")};
        listed.insert(root, games);
        listed.insert(root + QStringLiteral("/handheld"), handheld);
        out = LooseFiles::onServer(root + QStringLiteral("/"), folders, listed, owned);
        QVERIFY(out.complete);
        QVERIFY(out.needListings.isEmpty());
        QCOMPARE(out.names,
                 QStringList(
                     {QStringLiteral("manual.pdf"), QStringLiteral("handheld/launcher-config")}));
        QCOMPARE(out.neverCounts, 2);
    }

    void onServerFailsClosed()
    {
        const QString root = QStringLiteral("/volume1/games");
        QHash<QString, ServerFolder> listed;
        listed.insert(root, ServerFolder()); // no answer
        LooseFiles::OnServer out = LooseFiles::onServer(root, {root}, listed, {});
        QVERIFY(!out.complete);
        QVERIFY(out.needListings.isEmpty());

        ServerFolder missing;
        missing.state = ServerFolder::State::Missing;
        listed.insert(root, missing);
        out = LooseFiles::onServer(root, {root}, listed, {});
        QVERIFY(out.complete);
        QVERIFY(out.names.isEmpty());
    }

    void theNeverCountList()
    {
        QCOMPARE(LooseFiles::neverCountPatterns().size(), 5);
        QVERIFY(LooseFiles::neverCounts(QStringLiteral(".abc.parts")));
        QVERIFY(LooseFiles::neverCounts(QStringLiteral("x.mkv.!qB")));
        QVERIFY(LooseFiles::neverCounts(QStringLiteral("thumbs.db")));
        QVERIFY(LooseFiles::neverCounts(QStringLiteral(".DS_Store")));
        QVERIFY(!LooseFiles::neverCounts(QStringLiteral("parts.txt")));
        QVERIFY(!LooseFiles::neverCounts(QStringLiteral("ds_store")));
    }
};

QTEST_GUILESS_MAIN(TestLooseFiles)

#include "tst_loosefiles.moc"
