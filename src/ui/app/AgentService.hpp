#pragma once

// AgentService — the app's side of `io.github.diogochaves.omaseed.Agent` (DECISIONS C45 V3):
// it reads App's models into core::AgentAnswers' plain inputs and answers with their JSON.
// integration::AgentBus exports the interface and calls this; `do` acts through AgentDo. While the
// Agents row's Reads is off (C50 GT8), every read answers not-allowed; `skin use` stays.
//
// What it never reads (C45 V7): a server's username, its URL beyond host:port, the secret
// store, a cookie. Every free text goes out through AgentAnswers, which masks it.

#include "core/AgentAnswers.hpp"
#include "integration/AgentBus.hpp"

#include <QObject>
#include <QString>

namespace omaseed::app {

class App;
class AgentDo;

class AgentService : public QObject, public omaseed::integration::AgentHandler
{
    Q_OBJECT

public:
    explicit AgentService(App *app, QObject *parent = nullptr);

    void status(const QString &options, Reply reply) override;
    /// C45 V9: on qBittorrent, after one read of the log (at most kErrorsWait); a server
    /// that isn't the current one, an unknown one, or one still loading is a failure.
    void errors(const QString &options, Reply reply) override;
    /// C45 V10: one read of qBittorrent's log, the whole of it at that level; another
    /// server, another engine, a server down or a `--torrent` that names none is a failure.
    void log(const QString &options, Reply reply) override;
    /// C45 V11: the server omaseed is on, or any saved one by `--server` (its history and
    /// journal as kept, no move back).
    void moves(const QString &options, Reply reply) override;
    /// C37 U6: a built-in or a listed user skin that reads is drawn (App::setSkinChoice,
    /// which saves it); a bad name, an unknown skin or one that doesn't read changes nothing.
    void useSkin(const QString &options, Reply reply) override;
    /// C45 V15, C50 GT3, GT5: `do`.
    void doAction(const QString &options, uint callerPid, Reply reply) override;

    /// How long errors waits for the log before it answers with what is held (design 12).
    static constexpr int kErrorsWaitMs = 5000;

    /// status's input as the app is now (C45 V8, design 11).
    core::AgentAnswers::StatusInput statusInput() const;
    /// errors' input as the app is now (C45 V9, design 12): the rail's needs-you.
    core::AgentAnswers::ErrorsInput errorsInput() const;
    /// The server the app is on, as every read says it.
    core::AgentAnswers::Server currentServer() const;
    /// `--server NAME` against the saved servers (C45 V6).
    core::AgentAnswers::ServerMatch matchServer(const QString &query) const;

public Q_SLOTS:
    /// The main window shows or hides: status's `window` (main.cpp wires it as it does the
    /// status file's).
    void setWindowShown(bool shown);

private:
    friend class AgentDo; // `do` checks the server and the torrents as the reads do

    /// A failure the app makes for `command` (C45 V4).
    static QString failure(const QString &command, const QString &code, const QString &message);
    /// C50 GT8's not-allowed for `command` while Reads is off in the Agents row; "" while on.
    QString readsOff(const QString &command) const;
    /// `--server QUERY`'s failure for `command`, "" when it names a saved server; that one's
    /// id in `id`.
    QString serverFailure(const QString &command, const QString &query, QString *id) const;
    /// C45 V6's not-current failure for `command`, naming the saved server `id`.
    QString notCurrent(const QString &command, const QString &current, const QString &id) const;
    /// Design 12: the server omaseed is on is still on its first sync (nothing to read yet).
    bool firstSync(const core::AgentAnswers::Server &current) const;
    QString firstSyncFailure(const QString &command, const QString &name) const;
    /// The library as log lines name it (C3 D13).
    QList<core::AgentAnswers::LogTorrent> logLibrary() const;

    App *m_app;
    AgentDo *m_do = nullptr;
    bool m_windowShown = false;
};

} // namespace omaseed::app
