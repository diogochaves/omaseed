#include "core/OmarchyMenuFile.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::OmarchyMenuFile;
using State = OmarchyMenuFile::State;

// docs/DECISIONS.md C36 M13–M15, board 53 §6: every row of the file table, the cases
// tests/dev/test_install_extras.sh had for --menu, and the rest of the file kept byte for byte.
class TestOmarchyMenuFile : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void noMenuWritesNothing();
    void noFileIsCreatedHoldingOnlyTheBlock();
    void stockFileTakesTheBlockAndGivesItBack();
    void addAndRemoveKeepTheRestByteForByte_data();
    void addAndRemoveKeepTheRestByteForByte();
    void missingCommaIsAddedAfterTheLastEntry_data();
    void missingCommaIsAddedAfterTheLastEntry();
    void lastEntryEndingInACommentIsLeftAlone_data();
    void lastEntryEndingInACommentIsLeftAlone();
    void oldRowIsReplacedByTheBlock_data();
    void oldRowIsReplacedByTheBlock();
    void usersRowIsReportedAndKept_data();
    void usersRowIsReportedAndKept();
    void nestedTorrentsKeyIsNotARow();
    void rewriteChangesOnlyBetweenTheMarkers();
    void addOverABlockRewritesIt();
    void removeWithoutABlockChangesNothing();
    void unpairedMarkersAreLeftAlone();
    void malformedBlockIsRefused_data();
    void malformedBlockIsRefused();
    void fileModeAndSymlinkAreKept();
    void handCopyReadsAsOn();

private:
    QString userFile() const { return m_dir.path() + QStringLiteral("/ext/omarchy-menu.jsonc"); }
    QString menuFile() const { return m_dir.path() + QStringLiteral("/omarchy-menu.jsonc"); }
    OmarchyMenuFile file() const { return OmarchyMenuFile(userFile(), menuFile()); }
    void put(const QByteArray &bytes) const;
    QByteArray contents() const;

    QTemporaryDir m_dir;
};

namespace {

const QString kBlock = QStringLiteral(
    "  \"trigger.torrents\": {\"icon\":\"x\",\"label\":\"Torrents\",\"aliases\":[\"torrents\"]},\n"
    "  \"trigger.torrents.open\": {\"label\":\"Open omaseed\",\"action\":\"uwsm-app -- "
    "omaseed\"},\n");

const QString kOtherBlock = QStringLiteral(
    "  \"trigger.torrents\": {\"label\":\"Torrents\"},\n"
    "  \"trigger.torrents.servers.home\": {\"label\":\"home\",\"action\":\"busctl --user x\"},\n");

QByteArray wrapped(const QString &block)
{
    return "  " + OmarchyMenuFile::openMarker() + '\n' + block.toUtf8() + "  " +
           OmarchyMenuFile::closeMarker() + '\n';
}

// JSONC to JSON (comments and trailing commas dropped, strings kept), then its top-level keys
// in file order: the file Omarchy's menu would read.
QStringList keysOf(const QByteArray &text)
{
    QByteArray out;
    for (qsizetype i = 0; i < text.size();) {
        const char c = text.at(i);
        if (c == '"') {
            qsizetype j = i + 1;
            while (text.at(j) != '"')
                j += text.at(j) == '\\' ? 2 : 1;
            out += text.mid(i, j + 1 - i);
            i = j + 1;
        } else if (text.mid(i, 2) == "//") {
            const qsizetype nl = text.indexOf('\n', i);
            i = nl < 0 ? text.size() : nl;
        } else if (text.mid(i, 2) == "/*") {
            i = text.indexOf("*/", i) + 2;
        } else {
            out += c;
            ++i;
        }
    }
    const QString clean = QString::fromUtf8(out).replace(
        QRegularExpression(QStringLiteral(",(\\s*[}\\]])")), QStringLiteral("\\1"));
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(clean.toUtf8(), &error);
    if (!doc.isObject())
        return {QStringLiteral("<invalid: %1>").arg(error.errorString())};
    // QJsonObject sorts its keys; read them in file order instead.
    QStringList keys;
    const QStringList all = doc.object().keys();
    QList<std::pair<qsizetype, QString>> placed;
    for (const QString &k : all)
        placed.append({out.indexOf("\"" + k.toUtf8() + "\""), k});
    std::sort(placed.begin(), placed.end());
    for (const auto &p : placed)
        keys << p.second;
    return keys;
}

} // namespace

void TestOmarchyMenuFile::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.path()).removeRecursively();
    QDir().mkpath(m_dir.path() + QStringLiteral("/ext"));
    QFile menu(menuFile());
    QVERIFY(menu.open(QIODevice::WriteOnly));
    menu.write("{\n  \"trigger\": {\"label\":\"Trigger\"},\n}\n");
}

void TestOmarchyMenuFile::put(const QByteArray &bytes) const
{
    QFile f(userFile());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray TestOmarchyMenuFile::contents() const
{
    QFile f(userFile());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<none>");
}

void TestOmarchyMenuFile::noMenuWritesNothing()
{
    QVERIFY(QFile::remove(menuFile()));
    QCOMPARE(file().state(), State::NoMenu);
    OmarchyMenuFile f = file();
    QString error;
    QVERIFY(!f.add(kBlock, &error));
    QVERIFY(error.contains(QStringLiteral("Omarchy's menu")));
    QVERIFY(!QFileInfo::exists(userFile()));
}

void TestOmarchyMenuFile::noFileIsCreatedHoldingOnlyTheBlock()
{
    QDir(m_dir.path() + QStringLiteral("/ext")).removeRecursively();
    QCOMPARE(file().state(), State::NoFile);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QCOMPARE(contents(), "{\n" + wrapped(kBlock) + "}\n");
    QCOMPARE(file().state(), State::Block);
    QCOMPARE(
        keysOf(contents()),
        QStringList({QStringLiteral("trigger.torrents"), QStringLiteral("trigger.torrents.open")}));
}

void TestOmarchyMenuFile::stockFileTakesTheBlockAndGivesItBack()
{
    // The shape Omarchy installs: comments only, so "{" is the last thing before "}".
    const QByteArray stock = "{\n  // Extend the menu.\n  //\n"
                             "  // \"personal\": {\"icon\":\"\",\"label\":\"Personal\"},\n}\n";
    put(stock);
    QCOMPARE(file().state(), State::NoBlock);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QCOMPARE(contents(), stock.left(stock.lastIndexOf('}')) + wrapped(kBlock) + "}\n");
    QVERIFY(f.remove());
    QCOMPARE(contents(), stock);
}

void TestOmarchyMenuFile::addAndRemoveKeepTheRestByteForByte_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::addColumn<QStringList>("keys");
    QTest::newRow("plain") << QByteArray("{\n  \"a\": {\"label\":\"A}\"},\n}\n")
                           << QStringList{QStringLiteral("a")};
    QTest::newRow("blank tail") << QByteArray("{\n  \"a\": {\"label\":\"A\"},\n}\n\n\n")
                                << QStringList{QStringLiteral("a")};
    QTest::newRow("comments and a tail note")
        << QByteArray("// head\n{\n  /* one */ \"a\": [1, {\"b\": \"//x\"}], // mine\n"
                      "  \"c\": \"d\",\n  // closing remark\n} // end\n")
        << QStringList{QStringLiteral("a"), QStringLiteral("c")};
    QTest::newRow("crlf") << QByteArray("{\r\n  \"a\": 1,\r\n}\r\n")
                          << QStringList{QStringLiteral("a")};
}

void TestOmarchyMenuFile::addAndRemoveKeepTheRestByteForByte()
{
    QFETCH(QByteArray, before);
    QFETCH(QStringList, keys);
    put(before);
    QCOMPARE(file().state(), State::NoBlock);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QCOMPARE(file().state(), State::Block);
    const QStringList withBlock = keys + QStringList{QStringLiteral("trigger.torrents"),
                                                     QStringLiteral("trigger.torrents.open")};
    QCOMPARE(keysOf(contents()), withBlock);
    QVERIFY(f.remove());
    QCOMPARE(contents(), before);
    QCOMPARE(file().state(), State::NoBlock);
}

void TestOmarchyMenuFile::missingCommaIsAddedAfterTheLastEntry_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::addColumn<QByteArray>("afterRemove");
    QTest::newRow("no comma, a remark below")
        << QByteArray("{\n  \"a\": {\"label\":\"A\"},\n  \"b\": {\"label\":\"B\"}\n"
                      "  // a closing remark\n}\n")
        << QByteArray("{\n  \"a\": {\"label\":\"A\"},\n  \"b\": {\"label\":\"B\"},\n"
                      "  // a closing remark\n}\n");
    QTest::newRow("a url in an action")
        << QByteArray("{\n  \"web\": {\"action\":\"xdg-open https://example.org\"}\n}\n")
        << QByteArray("{\n  \"web\": {\"action\":\"xdg-open https://example.org\"},\n}\n");
    QTest::newRow("on one line") << QByteArray("{\"a\": 1}") << QByteArray("{\"a\": 1,\n}");
    QTest::newRow("empty object") << QByteArray("{}\n") << QByteArray("{\n}\n");
}

void TestOmarchyMenuFile::missingCommaIsAddedAfterTheLastEntry()
{
    QFETCH(QByteArray, before);
    QFETCH(QByteArray, afterRemove);
    put(before);
    QCOMPARE(file().state(), State::NoBlock);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QVERIFY(!keysOf(contents()).first().startsWith(QLatin1Char('<')));
    QVERIFY(keysOf(contents()).contains(QStringLiteral("trigger.torrents")));
    QVERIFY(f.remove());
    QCOMPARE(contents(), afterRemove);
}

void TestOmarchyMenuFile::lastEntryEndingInACommentIsLeftAlone_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::newRow("line comment") << QByteArray("{\n  \"a\": {\"label\":\"A\"} // mine\n}\n");
    QTest::newRow("block comment") << QByteArray("{\n  \"a\": 1 /* mine */\n}\n");
}

void TestOmarchyMenuFile::lastEntryEndingInACommentIsLeftAlone()
{
    QFETCH(QByteArray, before);
    put(before);
    QString detail;
    QCOMPARE(file().state(&detail), State::NoSafePlace);
    QVERIFY2(detail.contains(QStringLiteral("line 2")), qPrintable(detail));
    OmarchyMenuFile f = file();
    QString error;
    QVERIFY(!f.add(kBlock, &error));
    QVERIFY(error.contains(userFile()));
    QCOMPARE(contents(), before);
}

void TestOmarchyMenuFile::oldRowIsReplacedByTheBlock_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::addColumn<QByteArray>("rest");
    const QByteArray row =
        "  \"torrents\": {\"icon\":\"x\",\"label\":\"Torrents\",\"action\":\"uwsm-app -- "
        "omaseed\"},\n";
    // As the old install-omarchy-extras.sh --menu wrote it, marker comment and all.
    QTest::newRow("from --menu")
        << QByteArray(
               "{\n  \"a\": 1,\n  // ---- omaseed T1.8 (scripts/dev/install-omarchy-extras.sh "
               "--menu) ----\n" +
               row + "}\n")
        << QByteArray("{\n  \"a\": 1,\n");
    QTest::newRow("by hand, mid-file")
        << QByteArray("{\n" + row + "  \"a\": 1\n}\n") << QByteArray("{\n  \"a\": 1,\n");
}

void TestOmarchyMenuFile::oldRowIsReplacedByTheBlock()
{
    QFETCH(QByteArray, before);
    QFETCH(QByteArray, rest);
    put(before);
    QCOMPARE(file().state(), State::OldRow);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QCOMPARE(contents(), rest + wrapped(kBlock) + "}\n");
    QCOMPARE(keysOf(contents()),
             QStringList({QStringLiteral("a"),
                          QStringLiteral("trigger.torrents"),
                          QStringLiteral("trigger.torrents.open")}));
}

void TestOmarchyMenuFile::usersRowIsReportedAndKept_data()
{
    QTest::addColumn<QByteArray>("before");
    QTest::newRow("trigger.torrents")
        << QByteArray("{\n  \"a\": 1,\n  \"trigger.torrents\": {\"label\":\"Mine\"},\n}\n");
    QTest::newRow("a torrents row that isn't omaseed's") << QByteArray(
        "{\n  \"a\": 1,\n  \"torrents\": {\"label\":\"Mine\",\"action\":\"qbittorrent\"},\n}\n");
}

void TestOmarchyMenuFile::usersRowIsReportedAndKept()
{
    QFETCH(QByteArray, before);
    put(before);
    QString detail;
    QCOMPARE(file().state(&detail), State::UsersRow);
    QVERIFY2(detail.startsWith(QStringLiteral("line 3 uses ")), qPrintable(detail));
    OmarchyMenuFile f = file();
    QVERIFY(!f.add(kBlock));
    QVERIFY(f.remove());
    QCOMPARE(contents(), before);
}

void TestOmarchyMenuFile::nestedTorrentsKeyIsNotARow()
{
    const QByteArray before = "{\n  \"mine\": {\"torrents\": {\"label\":\"kept\"}},\n"
                              "  \"x\": {\"trigger.torrents\": 1},\n}\n";
    put(before);
    QCOMPARE(file().state(), State::NoBlock);
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QVERIFY(contents().contains("\"label\":\"kept\""));
    QVERIFY(f.remove());
    QCOMPARE(contents(), before);
}

void TestOmarchyMenuFile::rewriteChangesOnlyBetweenTheMarkers()
{
    // The block in the middle, the user's rows on both sides, an older marker wording.
    const QByteArray head = "{\n  \"a\": 1,\n  // >>> omaseed (an older wording)\n";
    const QByteArray tail = "  // <<< omaseed\n  \"z\": 2\n}\n";
    put(head + kBlock.toUtf8() + tail);
    QCOMPARE(file().state(), State::Block);
    OmarchyMenuFile f = file();
    QVERIFY(f.rewrite(kOtherBlock));
    QCOMPARE(contents(), head + kOtherBlock.toUtf8() + tail);
    QCOMPARE(keysOf(contents()),
             QStringList({QStringLiteral("a"),
                          QStringLiteral("trigger.torrents"),
                          QStringLiteral("trigger.torrents.servers.home"),
                          QStringLiteral("z")}));
    QVERIFY(f.remove());
    QCOMPARE(contents(), QByteArray("{\n  \"a\": 1,\n  \"z\": 2\n}\n"));

    QString error;
    QVERIFY(!f.rewrite(kBlock, &error));
    QVERIFY(error.contains(QStringLiteral("no omaseed block")));
}

void TestOmarchyMenuFile::addOverABlockRewritesIt()
{
    put("{\n  \"a\": 1,\n}\n");
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QVERIFY(f.add(kOtherBlock));
    QCOMPARE(contents(), "{\n  \"a\": 1,\n" + wrapped(kOtherBlock) + "}\n");
}

void TestOmarchyMenuFile::removeWithoutABlockChangesNothing()
{
    const QByteArray before = "{\n  \"a\": 1 // mine\n}\n";
    put(before);
    OmarchyMenuFile f = file();
    QVERIFY(f.remove());
    QCOMPARE(contents(), before);
    QVERIFY(QFile::remove(userFile()));
    QVERIFY(f.remove());
    QVERIFY(!QFileInfo::exists(userFile()));
}

void TestOmarchyMenuFile::unpairedMarkersAreLeftAlone()
{
    const QByteArray before = "{\n  // >>> omaseed\n  \"trigger.torrents\": {},\n}\n";
    put(before);
    QString detail;
    QCOMPARE(file().state(&detail), State::NoSafePlace);
    QVERIFY(detail.contains(QStringLiteral("markers")));
    OmarchyMenuFile f = file();
    QVERIFY(!f.add(kBlock));
    QVERIFY(!f.remove());
    QVERIFY(!f.rewrite(kBlock));
    QCOMPARE(contents(), before);
}

void TestOmarchyMenuFile::malformedBlockIsRefused_data()
{
    QTest::addColumn<QString>("block");
    QTest::newRow("empty") << QString();
    QTest::newRow("no comma") << QStringLiteral("  \"trigger.torrents\": {}\n");
    QTest::newRow("a marker inside") << QStringLiteral("  // <<< omaseed\n  \"t\": {},\n");
    QTest::newRow("unbalanced") << QStringLiteral("  \"trigger.torrents\": {,\n");
}

void TestOmarchyMenuFile::malformedBlockIsRefused()
{
    QFETCH(QString, block);
    const QByteArray before = "{\n  \"a\": 1,\n}\n";
    put(before);
    OmarchyMenuFile f = file();
    QString error;
    QVERIFY(!f.add(block, &error));
    QVERIFY2(error.startsWith(QStringLiteral("not a block")), qPrintable(error));
    QCOMPARE(contents(), before);
}

void TestOmarchyMenuFile::fileModeAndSymlinkAreKept()
{
    // A dotfiles checkout often links the file into place, and the user's mode is theirs.
    const QString real = m_dir.path() + QStringLiteral("/dotfiles-menu.jsonc");
    {
        QFile f(real);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\n  \"a\": 1,\n}\n");
    }
    QVERIFY(QFile::setPermissions(
        real, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther));
    QVERIFY(QFile::link(real, userFile()));
    OmarchyMenuFile f = file();
    QVERIFY(f.add(kBlock));
    QVERIFY(QFileInfo(userFile()).isSymLink());
    QVERIFY(contents().contains("trigger.torrents"));
    QCOMPARE(QFileInfo(real).permissions(),
             QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser |
                 QFile::ReadGroup | QFile::ReadOther);
}

void TestOmarchyMenuFile::handCopyReadsAsOn()
{
    // board 53 §6: data/omarchy-menu.jsonc, copied by hand, reads as the block (on); and it
    // carries the same opening marker the class writes.
    const QString data = QStringLiteral(OMASEED_DATA_DIR "/omarchy-menu.jsonc");
    QCOMPARE(OmarchyMenuFile(data, menuFile()).state(), State::Block);
    QFile f(data);
    QVERIFY(f.open(QIODevice::ReadOnly));
    bool found = false;
    for (const QByteArray &line : f.readAll().split('\n'))
        found = found || line.trimmed() == OmarchyMenuFile::openMarker();
    QVERIFY(found);
}

QTEST_GUILESS_MAIN(TestOmarchyMenuFile)

#include "tst_omarchymenufile.moc"
