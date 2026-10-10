#include "integration/BarWidget.hpp"

#include "integration/BarWidgetInstaller.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace omaseed::integration {

namespace Installer = BarWidgetInstaller;

BarWidget::BarWidget(const QString &devSourceDir, QObject *parent)
    : QObject(parent)
    , m_devSourceDir(devSourceDir)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_omarchy = env.value(QStringLiteral("OMASEED_OMARCHY"), QStringLiteral("omarchy"));
    m_omarchyShell =
        env.value(QStringLiteral("OMASEED_OMARCHY_SHELL"), QStringLiteral("omarchy-shell"));
    readState();

    m_settle.setSingleShot(true);
    m_settle.setInterval(100);
    connect(&m_settle, &QTimer::timeout, this, [this]() {
        watch();
        refresh();
    });
    connect(
        &m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    watch();
}

void BarWidget::watch()
{
    const QString plugins = QFileInfo(Installer::targetDir()).absolutePath();
    const QString shellJson = Installer::shellConfigPath();
    const QString omarchy = QFileInfo(shellJson).absolutePath();
    QStringList wanted;
    for (const QString &path : {plugins, omarchy, shellJson}) {
        if (QFileInfo::exists(path))
            wanted.append(path);
    }
    // No ~/.config/omarchy yet: its parent, to see it come.
    if (!wanted.contains(omarchy))
        wanted.append(QFileInfo(omarchy).absolutePath());

    const QStringList current = m_watcher.files() + m_watcher.directories();
    QStringList gone;
    for (const QString &path : current) {
        if (!wanted.contains(path))
            gone.append(path);
    }
    if (!gone.isEmpty())
        m_watcher.removePaths(gone);
    QStringList fresh;
    for (const QString &path : wanted) {
        if (!current.contains(path) && QFileInfo::exists(path))
            fresh.append(path);
    }
    if (!fresh.isEmpty())
        m_watcher.addPaths(fresh);
}

void BarWidget::setDevSourceDir(const QString &dir)
{
    m_devSourceDir = dir;
    refresh();
}

void BarWidget::setPrograms(const QString &omarchy, const QString &omarchyShell)
{
    m_omarchy = omarchy;
    m_omarchyShell = omarchyShell;
    refresh();
}

void BarWidget::setRetry(int intervalMs, int attempts)
{
    m_retryIntervalMs = intervalMs;
    m_retryAttempts = attempts;
}

QString BarWidget::source() const
{
    return Installer::findSource(m_devSourceDir);
}

bool BarWidget::readState()
{
    const bool available =
        !QStandardPaths::findExecutable(m_omarchy).isEmpty() && !source().isEmpty();
    const bool installed =
        Installer::presence(Installer::targetDir()) != Installer::Presence::Absent;
    const bool inBar = Installer::isInBar(Installer::shellConfigPath());
    if (available == m_available && installed == m_installed && inBar == m_inBar)
        return false;
    m_available = available;
    m_installed = installed;
    m_inBar = inBar;
    return true;
}

void BarWidget::refresh()
{
    if (readState())
        Q_EMIT changed();
}

void BarWidget::run(const QString &program,
                    const QStringList &arguments,
                    std::function<void(int, const QString &)> done)
{
    auto *process = new QProcess(this);
    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process,
            &QProcess::finished,
            this,
            [process, done](int exitCode, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(process->readAll()).trimmed();
                process->deleteLater();
                done(status == QProcess::NormalExit ? exitCode : -1, output);
            });
    connect(process,
            &QProcess::errorOccurred,
            this,
            [process, program, done](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                process->deleteLater();
                done(-1, QStringLiteral("%1 could not be started").arg(program));
            });
    process->start();
}

void BarWidget::install()
{
    if (m_busy)
        return;
    m_busy = true;
    Q_EMIT changed();

    const Installer::Result linked = Installer::link(source(), Installer::targetDir());
    if (!linked.ok) {
        finish(false, linked.message);
        return;
    }
    readState();
    if (m_inBar) {
        finish(true, QStringLiteral("omaseed is in your bar"));
        return;
    }
    // A shell that is not running answers with an error; enabling then says what is wrong.
    run(m_omarchyShell,
        {QStringLiteral("shell"), QStringLiteral("rescanPlugins")},
        [this](int, const QString &) { enable(1); });
}

void BarWidget::enable(int attempt)
{
    run(m_omarchy,
        {QStringLiteral("plugin"), QStringLiteral("enable"), Installer::pluginId()},
        [this, attempt](int exitCode, const QString &output) {
            if (exitCode == 0) {
                finish(true, output.isEmpty() ? QStringLiteral("omaseed is in your bar") : output);
                return;
            }
            if (output.contains(QLatin1String("is not known")) && attempt < m_retryAttempts) {
                QTimer::singleShot(
                    m_retryIntervalMs, this, [this, attempt]() { enable(attempt + 1); });
                return;
            }
            finish(false,
                   output.isEmpty() ? QStringLiteral("%1 failed").arg(Installer::enableCommand())
                                    : output);
        });
}

void BarWidget::remove()
{
    if (m_busy)
        return;
    m_busy = true;
    Q_EMIT changed();

    const auto unlinkAndFinish = [this]() {
        const Installer::Result removed = Installer::unlink(Installer::targetDir());
        finish(removed.ok,
               removed.ok ? QStringLiteral("omaseed is no longer in your bar") : removed.message);
    };
    readState();
    if (!m_inBar) {
        unlinkAndFinish();
        return;
    }
    run(m_omarchy,
        {QStringLiteral("plugin"), QStringLiteral("disable"), Installer::pluginId()},
        [this, unlinkAndFinish](int exitCode, const QString &output) {
            if (exitCode != 0) {
                finish(false,
                       output.isEmpty() ? QStringLiteral("omarchy plugin disable failed") : output);
                return;
            }
            unlinkAndFinish();
        });
}

void BarWidget::openSettings()
{
    run(m_omarchyShell,
        {Installer::pluginId(), QStringLiteral("settings")},
        [this](int exitCode, const QString &output) {
            if (exitCode == 0)
                return;
            // Said on the row too: alone in the log, "customize" looked like it did nothing.
            const QString why = output.isEmpty() ? QStringLiteral("no answer") : output;
            qWarning().noquote() << QStringLiteral("bar widget settings: %1").arg(why);
            m_lastOk = false;
            m_message = QStringLiteral("the widget did not open its settings (%1); open its "
                                       "popup from the bar instead")
                            .arg(why);
            Q_EMIT changed();
        });
}

void BarWidget::finish(bool ok, const QString &message)
{
    m_busy = false;
    m_lastOk = ok;
    m_message = message;
    readState();
    Q_EMIT changed();
    Q_EMIT finished(ok, message);
}

} // namespace omaseed::integration
