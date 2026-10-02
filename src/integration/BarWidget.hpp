#pragma once

// BarWidget — puts omaseed's bar widget in the Omarchy bar and takes it out again (#17,
// ruled 2026-10-02: the widget is part of the install). First run offers it, Settings ›
// Appearance switches it, and `omaseed --install-bar-widget` does it from a terminal.
//
// In: link `~/.config/omarchy/plugins/chaves.omaseed` to the packaged widget
// (BarWidgetInstaller::link), `omarchy-shell shell rescanPlugins`, then
// `omarchy plugin enable chaves.omaseed`, tried again while the shell does not know the widget
// yet (the rescan returns before it is done), as `omabox setup` does for its own widget.
// Out: `omarchy plugin disable chaves.omaseed`, then the link goes.
//
// Nothing here runs unasked: enabling a widget changes the user's shell.json, their bar.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

namespace omaseed::integration {

class BarWidget : public QObject
{
    Q_OBJECT

    /// The Omarchy shell's CLI is here and a widget to link was found.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    /// Something is at ~/.config/omarchy/plugins/chaves.omaseed.
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    /// The shell config puts the widget in the bar (or a container widget).
    Q_PROPERTY(bool inBar READ inBar NOTIFY changed)
    /// An install or a removal is running.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What the last install or removal said; empty before one ran.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    /// Whether the last install or removal went through.
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY changed)

public:
    /// `devSourceDir` is the source tree's plugin/ of a development build (empty in a release).
    explicit BarWidget(const QString &devSourceDir = QString(), QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool installed() const { return m_installed; }
    bool inBar() const { return m_inBar; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    bool lastOk() const { return m_lastOk; }

    /// The programs to run: $OMASEED_OMARCHY and $OMASEED_OMARCHY_SHELL when set, "omarchy" and
    /// "omarchy-shell" otherwise. A test seam: the QML tests point both at `true`.
    Q_INVOKABLE void setPrograms(const QString &omarchy, const QString &omarchyShell);
    /// The source tree's plugin/, found last (main.cpp sets it in a development build).
    void setDevSourceDir(const QString &dir);
    /// How long to wait between enable attempts, and how many to make (200 ms, 20).
    void setRetry(int intervalMs, int attempts);

    /// Reads the plugins folder and shell.json again.
    Q_INVOKABLE void refresh();
    /// Links, rescans and enables. Answers with `finished`.
    Q_INVOKABLE void install();
    /// Disables and unlinks. Answers with `finished`.
    Q_INVOKABLE void remove();

Q_SIGNALS:
    void changed();
    void finished(bool ok, const QString &message);

private:
    void run(const QString &program,
             const QStringList &arguments,
             std::function<void(int exitCode, const QString &output)> done);
    void enable(int attempt);
    /// Reads the plugins folder, shell.json and PATH; true when any of the three moved.
    bool readState();
    void finish(bool ok, const QString &message);
    QString source() const;

    QString m_devSourceDir;
    QString m_omarchy;
    QString m_omarchyShell;
    int m_retryIntervalMs = 200;
    int m_retryAttempts = 20;

    bool m_available = false;
    bool m_installed = false;
    bool m_inBar = false;
    bool m_busy = false;
    bool m_lastOk = false;
    QString m_message;
};

} // namespace omaseed::integration
