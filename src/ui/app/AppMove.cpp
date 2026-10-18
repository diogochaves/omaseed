// App's #97 block (board 42, DECISIONS C15): moving a folder. MoveController does the work;
// this gives it what it needs from App and carries its line to the tray, its notice to the
// desktop and its question to the window before a quit. The declarations sit at the end of
// App.hpp.
#include "ui/app/App.hpp"

#include "integration/Notifier.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace omaseed::app {

void App::setUpMove()
{
    m_move = new MoveController(this);
    MoveController::Hooks hooks;
    hooks.engine = m_engine;
    hooks.probe = &m_freeSpace;
    hooks.lowDiskBytes = [this]() { return qint64(lowDiskThresholdGiB()) * 1024 * 1024 * 1024; };
    hooks.serverName = [this]() { return m_hasServer ? m_server.name : QString(); };
    hooks.notify = [this](const QString &summary, const QString &body) {
        if (!m_notifier.isNull() && !m_demoMode)
            m_notifier->notifyMove(m_move->serverId(), summary, body);
    };
    // C16 L4: the last destinations, per server, in state.json's "moves".
    hooks.loadRecents = [this](const QString &serverId) {
        return stateSection(QStringLiteral("moves"))
            .value(serverId)
            .toObject()
            .value(QStringLiteral("recent"))
            .toVariant()
            .toStringList();
    };
    hooks.saveRecents = [this](const QString &serverId, const QStringList &paths) {
        QJsonObject section = stateSection(QStringLiteral("moves"));
        QJsonObject server = section.value(serverId).toObject();
        server.insert(QStringLiteral("recent"), QJsonArray::fromStringList(paths));
        section.insert(serverId, server);
        writeStateSection(QStringLiteral("moves"), section);
    };
    m_move->setHooks(std::move(hooks));
    // C16 L7: a quick move that didn't happen is said in the footer, as a failure the log told
    // (docs/32 N1) was; one that landed is #104's footer note.
    connect(m_move,
            &MoveController::note,
            this,
            [this](const QString &text, const QStringList &hashes, bool failed) {
                if (failed)
                    Q_EMIT loggedFailure(text, hashes);
            });
    connect(this, &App::serverChanged, this, &App::updateMoveServer);
    // J6 (M20): the tray's tooltip carries the job's line.
    connect(m_move, &MoveController::changed, this, [this]() {
        if (!m_tray.isNull())
            m_tray->setJobLine(m_move->trayLine());
    });
    if (QCoreApplication::instance() != nullptr)
        connect(QCoreApplication::instance(),
                &QCoreApplication::aboutToQuit,
                m_move,
                &MoveController::saveNow);
    updateMoveServer();
}

void App::updateMoveServer()
{
    // Demo and first run have no job; another server's job waits in its own file.
    if (m_move == nullptr)
        return;
    m_move->setServer(m_hasServer && !m_demoMode ? m_server.id : QString());
}

bool App::quitAllowed()
{
    if (!m_move->active())
        return true;
    Q_EMIT raiseRequested();
    Q_EMIT moveQuitAsked();
    return false;
}

void App::pauseMoveAndQuit()
{
    m_move->pauseForQuit();
    Q_EMIT quitRequested();
}

} // namespace omaseed::app
