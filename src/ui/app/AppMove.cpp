// App's #97 block (board 42, DECISIONS C15): moving a folder. MoveController does the work;
// this gives it what it needs from App and carries its line to the tray, its notice to the
// desktop and its question to the window before a quit. The declarations sit at the end of
// App.hpp.
#include "ui/app/App.hpp"

#include "core/ServerLog.hpp"
#include "core/SyncEngine.hpp"
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
    // C18 D8: the finished moves, per server, in the same section.
    hooks.loadHistory = [this](const QString &serverId) {
        return stateSection(QStringLiteral("moves"))
            .value(serverId)
            .toObject()
            .value(QStringLiteral("history"))
            .toArray();
    };
    hooks.saveHistory = [this](const QString &serverId, const QJsonArray &moves) {
        QJsonObject section = stateSection(QStringLiteral("moves"));
        QJsonObject server = section.value(serverId).toObject();
        server.insert(QStringLiteral("history"), moves);
        section.insert(serverId, server);
        writeStateSection(QStringLiteral("moves"), section);
    };
    m_move->setHooks(std::move(hooks));
    // C18 D7: each finished move is a line in the log, with "move back…" while it is kept.
    const auto feedLog = [this]() {
        QList<core::ServerLog::Line> lines;
        for (const QVariant &value : m_move->moveLines()) {
            const QVariantMap map = value.toMap();
            core::ServerLog::Line line;
            line.at = map.value(QStringLiteral("at")).toLongLong();
            line.level = core::ServerLog::Info;
            line.message = map.value(QStringLiteral("message")).toString();
            line.moveId = map.value(QStringLiteral("id")).toString();
            lines << line;
        }
        m_engine->serverLog()->setMoveLines(std::move(lines));
    };
    connect(m_move, &MoveController::historyChanged, this, feedLog);
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
    // C17 D5: a quit sends what waits; you asked for it, so it isn't dropped.
    sendHeldSelection();
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

void App::quitFromOutside(const QString &why)
{
    qInfo().noquote() << QStringLiteral("quit: %1").arg(why);
    sendHeldSelection();
    if (m_move->active())
        m_move->pauseForQuit();
    Q_EMIT quitRequested();
}

} // namespace omaseed::app
