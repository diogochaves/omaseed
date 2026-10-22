#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;

namespace omaseed::integration {

/// #128: SIGTERM, SIGINT and SIGHUP turned into a Qt signal, so a logout, a shutdown, `kill`
/// or ctrl+c in a terminal quits omaseed the way the tray's "quit" does, writing what it keeps
/// on quit, instead of ending the process where it stands.
///
/// The Qt way (the "Calling Qt Functions From Unix Signal Handlers" pattern): the handler only
/// writes the signal's number to one end of a socket pair, which is async-signal-safe; a
/// QSocketNotifier on the other end hears it in the event loop and emits `received()`. After
/// the first signal the three go back to their defaults, so a second one ends a quit that hangs.
///
/// One per process: a second instance while one is alive installs nothing (`isActive()` false).
class QuitSignals : public QObject
{
    Q_OBJECT

public:
    explicit QuitSignals(QObject *parent = nullptr);
    ~QuitSignals() override;

    /// Whether the handlers are installed (the socket pair could be made).
    bool isActive() const { return m_notifier != nullptr; }

    /// "SIGTERM", "SIGINT", "SIGHUP", or the number.
    static QString nameOf(int signal);

signals:
    /// One of the three arrived, in the event loop.
    void received(int signal);

private:
    void readSignal();

    QSocketNotifier *m_notifier = nullptr;
};

} // namespace omaseed::integration
