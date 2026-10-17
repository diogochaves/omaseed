#pragma once

// QbtSettingsFetch — another server's qBittorrent settings, read through a client of its own
// (docs/34 D6 a, #70).
//
// "Copy settings from another server" never moves the app: omaseed stays connected to the
// server it is on (C8, one connection) and only reads the other one. `fetch()` makes a
// short-lived QbtEngine for it, as the server sheet's test does (App::runServerTest): the
// server's address, pinned certificate and the login the caller read from the keyring, no
// retries and an 8 s timeout. It probes, makes the five reads a settings export makes
// (QbtSettingsExport::collect), signs out when it signed in with a password, and drops the
// client. Nothing is ever written to that server.
//
// When the server can't be reached, `newestSaved()` is the fallback: the settings omaseed last
// kept of it, in its automatic copies or read earlier this session, whichever is newest.

#include "core/OmaseedBackup.hpp"
#include "core/Server.hpp"
#include "core/engines/qbt/QbtSettingsExport.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <chrono>
#include <optional>

namespace omaseed::core {

class QbtEngine;

class QbtSettingsFetch : public QObject
{
    Q_OBJECT

public:
    explicit QbtSettingsFetch(QObject *parent = nullptr);
    ~QbtSettingsFetch() override;

    /// How long each request may take (8 s, as the server sheet's test).
    void setTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }

    /// Reads `server`'s settings. `secret` is its API key or password, as its auth mode wants
    /// (empty for none). `fetched` or `failed` ends it, once; a second call while one runs is
    /// ignored.
    void fetch(const Server &server, const QString &secret, const QString &appVersion);
    bool isBusy() const { return m_busy; }

    /// The settings of `server` held in `contents` (an omaseed backup): by this machine's id
    /// for it, then by its address. Never "the only one": that one may be another server's.
    static std::optional<QbtSettingsSnapshot> fromBackup(const OmaseedBackup::Contents &contents,
                                                         const Server &server);
    /// The newest settings omaseed keeps of `server`: `session` (server id -> a settings
    /// export's bytes, what this run read) against the newest of `copies` (automatic copies,
    /// newest first) that holds it, by the snapshot's own date.
    static std::optional<QbtSettingsSnapshot> newestSaved(const Server &server,
                                                          const QHash<QString, QByteArray> &session,
                                                          const QStringList &copies);

    /// Why a read failed, in the sheet's words: "connection refused", "no answer in time",
    /// "wrong username or password"…
    static QString reasonText(const ApiError &error, AuthMode mode);

Q_SIGNALS:
    void fetched(const omaseed::core::QbtSettingsSnapshot &snapshot);
    void failed(const QString &reason);

private:
    void finish(const std::optional<QbtSettingsSnapshot> &snapshot, const QString &reason);

    QPointer<QbtEngine> m_client;
    QPointer<QbtSettingsExport> m_export;
    std::chrono::milliseconds m_timeout{8000};
    AuthMode m_mode = AuthMode::None;
    bool m_busy = false;
};

} // namespace omaseed::core
