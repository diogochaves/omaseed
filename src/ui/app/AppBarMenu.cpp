// App's #73 block (docs/33 §11, D10, D24, D25; DECISIONS C13): the one menu the tray and the
// bar widget's ⋯ show, and Settings › Appearance's "In the bar". Kept out of App.cpp so it
// reads as one piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/AltSpeed.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"

namespace omaseed::app {

using namespace omaseed::core;
using omaseed::integration::BarMenu;

namespace {

/// What the status file calls a server's state (board 33 §11 change 5), from the list's
/// words (ServerListModel::StateRole).
QString contractState(const QString &listState, const QString &reason)
{
    if (listState == QLatin1String("live") || listState == QLatin1String("seen"))
        return QStringLiteral("ok");
    if (listState == QLatin1String("syncing"))
        return QStringLiteral("first-sync");
    if (listState == QLatin1String("refused") || listState == QLatin1String("certificate"))
        return QStringLiteral("refused");
    if (listState == QLatin1String("down"))
        return QStringLiteral("down");
    if (listState == QLatin1String("failed")) {
        // ErrorWords::shortReason's words: "sign-in refused", "certificate refused" and
        // "refused omaseed" are a refusal; "didn't answer", "not found", "too slow" a loss.
        return reason.contains(QLatin1String("refused")) ? QStringLiteral("refused")
                                                         : QStringLiteral("down");
    }
    return QStringLiteral("unknown");
}

} // namespace

void App::setUpBarMenu()
{
    connect(
        m_barMenu, &BarMenu::serverChosen, this, [this](const QString &id) { switchServer(id); });
    connect(m_barMenu, &BarMenu::triggered, this, [this](const QString &id) {
        if (id == QLatin1String("add")) {
            Q_EMIT raiseRequested();
            Q_EMIT addRequested();
        } else if (id == QLatin1String("alt-speed")) {
            toggleAltSpeed();
        } else if (id == QLatin1String("stop-all")) {
            stopAll();
        } else if (id == QLatin1String("start-all")) {
            startAll();
        } else if (id == QLatin1String("settings")) {
            Q_EMIT openSettings();
        } else if (id == QLatin1String("quit")) {
            Q_EMIT quitRequested();
        }
    });

    // The servers block follows the list: its rows, the last-seen figures, the switch.
    connect(m_serverList, &QAbstractItemModel::dataChanged, this, &App::updateBarMenu);
    connect(m_serverList, &QAbstractItemModel::modelReset, this, &App::updateBarMenu);
    connect(m_serverList, &QAbstractItemModel::rowsInserted, this, &App::updateBarMenu);
    connect(m_serverList, &QAbstractItemModel::rowsRemoved, this, &App::updateBarMenu);
    connect(this, &App::serverChanged, this, &App::updateBarMenu);
    connect(this, &App::serverStateChanged, this, &App::updateBarMenu);

    // D25: the choice reads from the widget being in the bar and the tray icon mode.
    connect(m_barWidget, &omaseed::integration::BarWidget::changed, this, [this]() {
        if (!m_barWidget->busy())
            m_inTheBarWanted.clear();
        Q_EMIT inTheBarChanged();
    });
    connect(this, &App::settingsChanged, this, &App::inTheBarChanged);

    updateBarMenu();
}

void App::updateBarMenu()
{
    QList<BarMenu::Server> servers;
    for (int row = 0; row < m_serverList->count(); ++row) {
        const QModelIndex at = m_serverList->index(row, 0);
        const auto role = [&](ServerListModel::Role r) { return m_serverList->data(at, r); };
        BarMenu::Server server;
        server.id = role(ServerListModel::IdRole).toString();
        server.name = role(ServerListModel::NameRole).toString();
        server.current = role(ServerListModel::IsCurrentRole).toBool();
        const QString listState = role(ServerListModel::StateRole).toString();
        QString reason = role(ServerListModel::LastReasonRole).toString();
        if (server.current) {
            // The current server: how it is doing now, and its live count.
            if (listState == QLatin1String("refused"))
                reason = tr("sign-in refused");
            else if (listState == QLatin1String("certificate"))
                reason = tr("certificate refused");
            else if (listState == QLatin1String("down"))
                reason = ErrorWords::shortReason(m_serverError);
            else
                reason.clear();
            server.count =
                (listState == QLatin1String("live")) ? m_engine->torrents()->count() : -1;
            if (m_demoMode)
                server.count = m_engine->torrents()->count();
        } else {
            server.count = role(ServerListModel::LastCountRole).toInt();
            if (listState != QLatin1String("failed"))
                reason.clear();
        }
        server.state = contractState(listState, reason);
        server.reason = reason;
        const QDateTime seen = role(ServerListModel::LastSeenAtRole).toDateTime();
        server.seen = seen.isValid() ? seen.toSecsSinceEpoch() : 0;
        servers.append(server);
    }
    m_barMenu->setServers(servers);

    const GlobalState *state = m_engine->globalState();
    // docs/31 T1 (D14): the row's words follow the alternative set's word, in the tray's
    // lower case and the widget's card style.
    const QString label = m_altSpeed->trayLabel();
    const QString kind = m_altSpeed->kind();
    const bool named = kind == QLatin1String("burst") || kind == QLatin1String("turtle");
    QString title = tr("Alternative Speed Limits");
    if (named && !label.isEmpty())
        title = label.at(0).toUpper() + label.mid(1);
    m_barMenu->setAltSpeed(state->useAltSpeedLimits(), label, title);
    m_barMenu->setServerAnswering(m_demoMode ||
                                  (m_hasServer && state->connected() && !m_serverLost));
}

// --- D25: "In the bar" ------------------------------------------------------------------------

QString App::inTheBarFor(bool widgetInBar, const QString &trayIconMode)
{
    // DECISIONS C13: the two old settings, read as one. A widget in the bar with the tray icon
    // "never" behaves as "widget" (auto hides the icon while the widget runs).
    if (widgetInBar)
        return trayIconMode == QLatin1String("always") ? QStringLiteral("both")
                                                       : QStringLiteral("widget");
    return trayIconMode == QLatin1String("never") ? QStringLiteral("none") : QStringLiteral("tray");
}

QString App::inTheBar() const
{
    if (!m_inTheBarWanted.isEmpty() && m_barWidget->busy())
        return m_inTheBarWanted;
    return inTheBarFor(m_barWidget->inBar(), trayIconMode());
}

void App::setInTheBar(const QString &choice)
{
    static const QStringList choices{QStringLiteral("widget"),
                                     QStringLiteral("tray"),
                                     QStringLiteral("both"),
                                     QStringLiteral("none")};
    if (!choices.contains(choice) || m_barWidget->busy())
        return;
    const bool wantsWidget = choice == QLatin1String("widget") || choice == QLatin1String("both");
    const bool inBar = m_barWidget->inBar();
    // Without the shell (or the packaged widget) the widget cannot be had: nothing changes.
    if (wantsWidget && !inBar && !m_barWidget->available()) {
        Q_EMIT inTheBarChanged();
        return;
    }
    setTrayIconMode(choice == QLatin1String("both")   ? QStringLiteral("always")
                    : choice == QLatin1String("none") ? QStringLiteral("never")
                                                      : QStringLiteral("auto"));
    if (wantsWidget != inBar && m_barWidget->available()) {
        m_inTheBarWanted = choice;
        if (wantsWidget)
            m_barWidget->install();
        else
            m_barWidget->remove();
    }
    Q_EMIT inTheBarChanged();
}

} // namespace omaseed::app
