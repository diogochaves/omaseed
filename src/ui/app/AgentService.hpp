#pragma once

// AgentService — the app's side of `io.github.diogochaves.omaseed.Agent` (DECISIONS C45 V3):
// it reads App's models into core::AgentAnswers' plain inputs and answers with their JSON.
// integration::AgentBus exports the interface and calls this; nothing here acts.
//
// What it never reads (C45 V7): a server's username, its URL beyond host:port, the secret
// store, a cookie. Every free text goes out through AgentAnswers, which masks it.

#include "core/AgentAnswers.hpp"
#include "integration/AgentBus.hpp"

#include <QObject>
#include <QString>

namespace omaseed::app {

class App;

class AgentService : public QObject, public omaseed::integration::AgentHandler
{
    Q_OBJECT

public:
    explicit AgentService(App *app, QObject *parent = nullptr);

    void status(const QString &options, Reply reply) override;

    /// status's input as the app is now (C45 V8, design 11).
    core::AgentAnswers::StatusInput statusInput() const;
    /// The server the app is on, as every read says it.
    core::AgentAnswers::Server currentServer() const;
    /// `--server NAME` against the saved servers (C45 V6).
    core::AgentAnswers::ServerMatch matchServer(const QString &query) const;

public Q_SLOTS:
    /// The main window shows or hides: status's `window` (main.cpp wires it as it does the
    /// status file's).
    void setWindowShown(bool shown);

private:
    App *m_app;
    bool m_windowShown = false;
};

} // namespace omaseed::app
