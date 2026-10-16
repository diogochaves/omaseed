// #73 — BarMenu, the one menu of the tray and the bar widget's ⋯ (docs/33 §11, D10, D24;
// DECISIONS C13): its rows in order, the servers block only with two servers or more, the
// status file's two lists, and which ids it answers.

#include "integration/BarMenu.hpp"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using omaseed::integration::BarMenu;

namespace {

QStringList idsOf(const QList<BarMenu::Row> &rows)
{
    QStringList ids;
    for (const BarMenu::Row &row : rows)
        ids << row.id;
    return ids;
}

BarMenu::Server server(const QString &id,
                       const QString &name,
                       bool current,
                       const QString &state,
                       int count,
                       const QString &reason = QString())
{
    BarMenu::Server s;
    s.id = id;
    s.name = name;
    s.current = current;
    s.state = state;
    s.count = count;
    s.reason = reason;
    return s;
}

} // namespace

class TestBarMenu : public QObject
{
    Q_OBJECT

private slots:
    void oneServerHasNoServersBlock();
    void twoServersAddTheBlockFirst();
    void serverDetailSaysWhatTheRowShows();
    void rowsThatNeedAServerFollowIt();
    void invokeAnswersKnownEnabledRows();
    void jsonCarriesTheContractsFields();
    void changedOnlyWhenSomethingMoved();
};

// T0 (D19): with one server, today's rows; quit last, after a separator.
void TestBarMenu::oneServerHasNoServersBlock()
{
    BarMenu menu;
    menu.setServers(
        {server(QStringLiteral("h"), QStringLiteral("home"), true, QStringLiteral("ok"), 3)});
    QCOMPARE(idsOf(menu.rows()),
             QStringList({QStringLiteral("alt-speed"),
                          QStringLiteral("stop-all"),
                          QStringLiteral("start-all"),
                          QStringLiteral("separator:actions"),
                          QStringLiteral("add"),
                          QStringLiteral("settings"),
                          QStringLiteral("separator:quit"),
                          QStringLiteral("quit")}));
    const QList<BarMenu::Row> rows = menu.rows();
    QCOMPARE(rows.first().kind, BarMenu::Kind::Check);
    QCOMPARE(rows.last().label, QStringLiteral("quit"));
    QCOMPARE(rows.last().title, QStringLiteral("Quit omaseed"));
    QCOMPARE(rows.at(5).title, QStringLiteral("omaseed Settings…"));
    QCOMPARE(rows.at(4).label, QStringLiteral("add torrent…"));
    // No server at all: the same.
    menu.setServers({});
    QCOMPARE(menu.rows().size(), 8);
}

// W1, T1: a caption, a radio row per server (the current one checked), a separator.
void TestBarMenu::twoServersAddTheBlockFirst()
{
    BarMenu menu;
    menu.setServers(
        {server(QStringLiteral("a"), QStringLiteral("public"), false, QStringLiteral("ok"), 0),
         server(QStringLiteral("b"), QStringLiteral("private"), true, QStringLiteral("ok"), 4),
         server(QStringLiteral("c"),
                QStringLiteral("seedbox"),
                false,
                QStringLiteral("refused"),
                12,
                QStringLiteral("sign-in refused"))});
    const QList<BarMenu::Row> rows = menu.rows();
    QCOMPARE(rows.size(), 13);
    QCOMPARE(rows.at(0).kind, BarMenu::Kind::Caption);
    QCOMPARE(rows.at(0).label, QStringLiteral("servers"));
    QCOMPARE(rows.at(1).id, QStringLiteral("server:a"));
    QCOMPARE(rows.at(1).kind, BarMenu::Kind::Radio);
    QVERIFY(!rows.at(1).checked);
    QCOMPARE(rows.at(1).detail, QStringLiteral("0 torrents"));
    QVERIFY(rows.at(2).checked);
    QCOMPARE(rows.at(2).label, QStringLiteral("private"));
    QCOMPARE(rows.at(2).title, QStringLiteral("private"));
    QCOMPARE(rows.at(3).detail, QStringLiteral("sign-in refused"));
    QVERIFY(rows.at(3).urgent);
    QVERIFY(!rows.at(2).urgent);
    QCOMPARE(rows.at(4).kind, BarMenu::Kind::Separator);
    QCOMPARE(rows.at(5).id, QStringLiteral("alt-speed"));
    QCOMPARE(rows.last().id, QStringLiteral("quit"));
}

void TestBarMenu::serverDetailSaysWhatTheRowShows()
{
    using S = BarMenu::Server;
    const auto detail = [](const QString &state, int count, const QString &reason = QString()) {
        S s;
        s.state = state;
        s.count = count;
        s.reason = reason;
        return BarMenu::serverDetail(s);
    };
    QCOMPARE(detail(QStringLiteral("ok"), 1), QStringLiteral("1 torrent"));
    QCOMPARE(detail(QStringLiteral("ok"), 142), QStringLiteral("142 torrents"));
    QCOMPARE(detail(QStringLiteral("ok"), -1), QString());
    QCOMPARE(detail(QStringLiteral("unknown"), -1), QString());
    QCOMPARE(detail(QStringLiteral("first-sync"), -1), QStringLiteral("first sync"));
    QCOMPARE(detail(QStringLiteral("down"), 4), QStringLiteral("down"));
    QCOMPARE(detail(QStringLiteral("down"), 4, QStringLiteral("didn't answer")),
             QStringLiteral("didn't answer"));
    QCOMPARE(detail(QStringLiteral("refused"), -1), QStringLiteral("sign-in refused"));
}

// docs/20 §3 States: actions disabled rather than hidden while the server does not answer.
void TestBarMenu::rowsThatNeedAServerFollowIt()
{
    BarMenu menu;
    const auto enabled = [&menu](const QString &id) {
        for (const BarMenu::Row &row : menu.rows())
            if (row.id == id)
                return row.enabled;
        return false;
    };
    QVERIFY(!enabled(QStringLiteral("alt-speed")));
    QVERIFY(!enabled(QStringLiteral("stop-all")));
    QVERIFY(!enabled(QStringLiteral("start-all")));
    QVERIFY(enabled(QStringLiteral("add")));
    QVERIFY(enabled(QStringLiteral("settings")));
    QVERIFY(enabled(QStringLiteral("quit")));
    menu.setServerAnswering(true);
    QVERIFY(enabled(QStringLiteral("alt-speed")));
    QVERIFY(enabled(QStringLiteral("stop-all")));
    QVERIFY(enabled(QStringLiteral("start-all")));

    menu.setAltSpeed(
        true, QStringLiteral("burst · until 07:00"), QStringLiteral("Burst · until 07:00"));
    const BarMenu::Row alt = menu.rows().first();
    QVERIFY(alt.checked);
    QCOMPARE(alt.label, QStringLiteral("burst · until 07:00"));
    QCOMPARE(alt.title, QStringLiteral("Burst · until 07:00"));
}

void TestBarMenu::invokeAnswersKnownEnabledRows()
{
    BarMenu menu;
    menu.setServers(
        {server(QStringLiteral("a"), QStringLiteral("public"), true, QStringLiteral("ok"), 0),
         server(QStringLiteral("b"),
                QStringLiteral("private"),
                false,
                QStringLiteral("unknown"),
                -1)});
    QSignalSpy triggered(&menu, &BarMenu::triggered);
    QSignalSpy chosen(&menu, &BarMenu::serverChosen);

    QVERIFY(!menu.invoke(QStringLiteral("stop-all"))); // no server answering yet
    menu.setServerAnswering(true);
    for (const QString &id : BarMenu::actionIds())
        QVERIFY2(menu.invoke(id), qPrintable(id));
    QCOMPARE(triggered.size(), BarMenu::actionIds().size());
    QCOMPARE(triggered.last().at(0).toString(), QStringLiteral("quit"));

    QVERIFY(menu.invoke(QStringLiteral("server:b")));
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.first().at(0).toString(), QStringLiteral("b"));
    QVERIFY(!menu.invoke(QStringLiteral("server:nowhere")));
    QVERIFY(!menu.invoke(QStringLiteral("servers")));
    QVERIFY(!menu.invoke(QStringLiteral("separator:quit")));
    QVERIFY(!menu.invoke(QStringLiteral("pause-all")));
    QVERIFY(!menu.invoke(QString()));
    QCOMPARE(triggered.size(), BarMenu::actionIds().size());
    QCOMPARE(chosen.size(), 1);
}

// Board 33 §11 change 5: servers {id, name, current, state, count, seen, reason}; menu {id,
// label, kind, checked, enabled}, plus detail and urgent (DECISIONS C13, assumed).
void TestBarMenu::jsonCarriesTheContractsFields()
{
    BarMenu menu;
    BarMenu::Server a =
        server(QStringLiteral("a"), QStringLiteral("public"), true, QStringLiteral("ok"), 2);
    a.seen = 1789346626;
    menu.setServers({a,
                     server(QStringLiteral("b"),
                            QStringLiteral("private"),
                            false,
                            QStringLiteral("down"),
                            4,
                            QStringLiteral("didn't answer"))});
    const QJsonArray servers = menu.serversJson();
    QCOMPARE(servers.size(), 2);
    const QJsonObject first = servers.at(0).toObject();
    QCOMPARE(first.keys(),
             QStringList({QStringLiteral("count"),
                          QStringLiteral("current"),
                          QStringLiteral("id"),
                          QStringLiteral("name"),
                          QStringLiteral("reason"),
                          QStringLiteral("seen"),
                          QStringLiteral("state")}));
    QCOMPARE(first.value(QStringLiteral("seen")).toInteger(), 1789346626);
    QCOMPARE(servers.at(1).toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("down"));

    const QJsonArray rows = menu.menuJson();
    QCOMPARE(rows.size(), menu.rows().size());
    const QJsonObject caption = rows.at(0).toObject();
    QCOMPARE(caption.keys(),
             QStringList({QStringLiteral("checked"),
                          QStringLiteral("detail"),
                          QStringLiteral("enabled"),
                          QStringLiteral("id"),
                          QStringLiteral("kind"),
                          QStringLiteral("label"),
                          QStringLiteral("urgent")}));
    QCOMPARE(caption.value(QStringLiteral("kind")).toString(), QStringLiteral("caption"));
    QCOMPARE(rows.at(3).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("separator"));
    QCOMPARE(rows.at(4).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("Alternative Speed Limits"));
    QCOMPARE(rows.at(5).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("Stop All"));
    QCOMPARE(rows.at(2).toObject().value(QStringLiteral("urgent")).toBool(), true);
}

void TestBarMenu::changedOnlyWhenSomethingMoved()
{
    BarMenu menu;
    QSignalSpy changed(&menu, &BarMenu::changed);
    const QList<BarMenu::Server> one{
        server(QStringLiteral("a"), QStringLiteral("public"), true, QStringLiteral("ok"), 0)};
    menu.setServers(one);
    menu.setServers(one);
    QCOMPARE(changed.size(), 1);
    menu.setServerAnswering(false); // already
    menu.setAltSpeed(false,
                     QStringLiteral("alternative speed limits"),
                     QStringLiteral("Alternative Speed Limits"));
    QCOMPARE(changed.size(), 1);
    menu.setServerAnswering(true);
    QCOMPARE(changed.size(), 2);
}

QTEST_GUILESS_MAIN(TestBarMenu)
#include "tst_barmenu.moc"
