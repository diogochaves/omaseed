#pragma once

// BarMenu — the one menu the tray and the bar widget's ⋯ both show (docs/33 §11, D10, D24;
// docs/DECISIONS.md C13). Built once in the app: the tray builds its QMenu from these rows,
// StatusWriter writes them into status.json (`servers`, `menu`) for the widget, and
// StatusBus's `Invoke`/`SwitchServer` come back here. A row added once shows in both.
//
// Rows, in order:
//   servers         a caption and one radio row per server, then a separator; only with two
//                   servers or more (D19: one server never offers to switch)
//   alt-speed       a check row: the alternative limits, by their word (docs/31: burst, turtle)
//   stop-all        stop every torrent
//   start-all       start every torrent
//   ---
//   add             the app's add sheet
//   settings        the app's settings
//   ---
//   quit            the app's last row (reverses docs/07 §1 "no quit", board 33 §11 change 2)
//
// The tray adds its own "show omaseed" and two figure rows above them; the widget has its head
// and Now for those (board 33 T0).
//
// Each row has two labels: `label`, the tray's words (lower case, as the tray's menu always
// was), and `title`, the shell's card style the widget draws (Title Case, docs/20 N2). A
// server's row carries `detail`, what it shows at its right ("4 torrents", "sign-in
// refused"), and `urgent` when that is a failure.
//
// Nothing here knows the app: App feeds the servers and the alt-speed state, and answers
// `triggered` and `serverChosen`.

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QString>

namespace omaseed::integration {

class BarMenu : public QObject
{
    Q_OBJECT

public:
    enum class Kind { Action, Check, Radio, Caption, Separator };

    struct Row
    {
        QString id;
        QString label; ///< the tray's words
        QString title; ///< the widget's (card style)
        Kind kind = Kind::Action;
        bool checked = false;
        bool enabled = true;
        QString detail; ///< a server row's right-hand words
        bool urgent = false;

        bool operator==(const Row &other) const = default;
    };

    /// One server as status.json's `servers` lists it (board 33 §11 change 5).
    struct Server
    {
        QString id;
        QString name;
        bool current = false;
        /// "ok" · "down" · "refused" · "first-sync" · "unknown". For another server, how its
        /// last try went (D5); for the current one, how it is doing now.
        QString state = QStringLiteral("unknown");
        int count = -1;  ///< the current one's live count, another's last seen; -1 unknown
        qint64 seen = 0; ///< unix seconds it was last seen or tried; 0 never
        QString reason;  ///< why it failed, short ("sign-in refused"); empty when fine

        bool operator==(const Server &other) const = default;
    };

    /// The menu ids `Invoke` takes, besides `server:<id>`.
    static QStringList actionIds();
    /// "action" · "check" · "radio" · "caption" · "separator".
    static QString kindName(Kind kind);
    /// "4 torrents", "1 torrent", "sign-in refused", "down", "first sync", "" — a server
    /// row's right-hand words.
    static QString serverDetail(const Server &server);

    explicit BarMenu(QObject *parent = nullptr);

    // --- what App feeds -----------------------------------------------------------------
    /// Every server, in Settings' order.
    void setServers(const QList<Server> &servers);
    QList<Server> servers() const { return m_servers; }
    /// The alternative limits: on or off, and their words (`label` the tray's, "burst · until
    /// 07:00"; `title` the widget's, "Burst · until 07:00").
    void setAltSpeed(bool on, const QString &label, const QString &title);
    /// Alt-speed, stop all and start all need a server that answers; the rest never do.
    void setServerAnswering(bool answering);

    // --- what the tray and the status file read -------------------------------------------
    QList<Row> rows() const;
    /// status.json's `menu`: `[{id, label, kind, checked, enabled, detail, urgent}]`, `label`
    /// in the card style (the widget is its only reader).
    QJsonArray menuJson() const;
    /// status.json's `servers`: `[{id, name, current, state, count, seen, reason}]`.
    QJsonArray serversJson() const;

    /// A row picked in the tray, or `Invoke` from the widget. `server:<id>` emits
    /// serverChosen; any other known id emits triggered. False for an unknown id, a disabled
    /// row, a caption or a separator, which do nothing.
    Q_INVOKABLE bool invoke(const QString &id);

Q_SIGNALS:
    /// Any row moved: a label, a check, a server.
    void changed();
    /// add · alt-speed · stop-all · start-all · settings · quit
    void triggered(const QString &id);
    /// A server row: the app switches to it (App::switchServer).
    void serverChosen(const QString &serverId);

private:
    QList<Server> m_servers;
    bool m_altOn = false;
    QString m_altLabel;
    QString m_altTitle;
    bool m_answering = false;
};

} // namespace omaseed::integration
