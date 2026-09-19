#include "integration/Xdg.hpp"

#include "integration/AppIdentity.hpp"

#include <QClipboard>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QStandardPaths>

namespace omaseed::integration {

namespace {

Xdg *s_defaultInstance = nullptr;

const QLatin1String kFileManagerService("org.freedesktop.FileManager1");
const QLatin1String kFileManagerPath("/org/freedesktop/FileManager1");
const QLatin1String kFileManagerInterface("org.freedesktop.FileManager1");

QString ourDesktopId()
{
    return AppIdentity::desktopFileName() + QStringLiteral(".desktop");
}

bool looksLikeMagnet(const QString &text)
{
    static const QRegularExpression btihHex(QStringLiteral("xt=urn:btih:([0-9A-Fa-f]{40})(?:&|$)"));
    static const QRegularExpression btihB32(QStringLiteral("xt=urn:btih:([A-Za-z2-7]{32})(?:&|$)"));
    static const QRegularExpression btmh(QStringLiteral("xt=urn:btmh:([0-9A-Fa-f]+)(?:&|$)"));

    if (!text.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive))
        return false;

    return btihHex.match(text).hasMatch() || btihB32.match(text).hasMatch() ||
           btmh.match(text).hasMatch();
}

} // namespace

Xdg::Xdg(QDBusConnection connection, QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
{}

void Xdg::setDefaultInstance(Xdg *instance)
{
    s_defaultInstance = instance;
}

Xdg *Xdg::createForEngine(QQmlEngine *engine)
{
    if (s_defaultInstance) {
        QQmlEngine::setObjectOwnership(s_defaultInstance, QQmlEngine::CppOwnership);
        return s_defaultInstance;
    }
    // Nobody wired one up (the QML tests, a tool): give the engine one of its own.
    return new Xdg(QDBusConnection::sessionBus(), engine);
}

Addable Xdg::parseAddable(const QString &textIn)
{
    const QString text = textIn.trimmed();
    if (text.isEmpty())
        return Addable{};

    if (looksLikeMagnet(text))
        return Addable{Addable::Kind::Magnet, text};

    if (text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        return Addable{Addable::Kind::HttpUrl, text};
    }

    if (text.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QString localPath = QUrl(text).toLocalFile();
        if (localPath.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) &&
            QFile::exists(localPath)) {
            return Addable{Addable::Kind::TorrentFile, localPath};
        }
        return Addable{};
    }

    if (text.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) && QFile::exists(text))
        return Addable{Addable::Kind::TorrentFile, text};

    return Addable{};
}

QList<Addable> Xdg::parseAddableList(const QString &text)
{
    QList<Addable> result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const Addable addable = parseAddable(line);
        if (addable.kind != Addable::Kind::None)
            result.append(addable);
    }
    return result;
}

void Xdg::showInFileManager(const QString &path)
{
    const QFileInfo info(path);
    const bool isDir = info.isDir();
    const QString uri = QUrl::fromLocalFile(info.absoluteFilePath()).toString();
    const QString method = isDir ? QStringLiteral("ShowFolders") : QStringLiteral("ShowItems");

    QDBusMessage message = QDBusMessage::createMethodCall(
        kFileManagerService, kFileManagerPath, kFileManagerInterface, method);
    message << QStringList{uri} << QString();

    QDBusPendingCall call = m_connection.asyncCall(message);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            [this, path, isDir](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusPendingReply<> reply = *w;
                if (reply.isError()) {
                    const QString fallbackDir = isDir ? path : QFileInfo(path).absolutePath();
                    QDesktopServices::openUrl(QUrl::fromLocalFile(fallbackDir));
                    Q_EMIT revealFinished(false);
                    return;
                }
                Q_EMIT revealFinished(true);
            });
}

QList<Addable> Xdg::addablesFromDrop(const QList<QUrl> &urls, const QString &text) const
{
    QList<Addable> result;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;
        const QString localPath = url.toLocalFile();
        if (localPath.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) &&
            QFile::exists(localPath)) {
            result.append(Addable{Addable::Kind::TorrentFile, localPath});
        }
    }
    result.append(parseAddableList(text));
    return result;
}

void Xdg::isDefaultMagnetHandler()
{
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("xdg-mime"));
    process->setArguments({QStringLiteral("query"),
                           QStringLiteral("default"),
                           QStringLiteral("x-scheme-handler/magnet")});

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process](int, QProcess::ExitStatus) {
                const QString current =
                    QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                process->deleteLater();
                Q_EMIT isDefaultMagnetHandlerResult(current == ourDesktopId(), current);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        process->deleteLater();
        Q_EMIT isDefaultMagnetHandlerResult(false, QString());
    });

    process->start();
}

void Xdg::registerAsDefaultHandler()
{
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("xdg-mime"));
    process->setArguments({QStringLiteral("default"),
                           ourDesktopId(),
                           QStringLiteral("x-scheme-handler/magnet"),
                           QStringLiteral("application/x-bittorrent")});

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();
                Q_EMIT registrationFinished(status == QProcess::NormalExit && exitCode == 0);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        process->deleteLater();
        Q_EMIT registrationFinished(false);
    });

    process->start();
}

QString Xdg::stateFilePath()
{
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty())
        configHome = QDir::homePath() + QStringLiteral("/.config");
    return configHome + QStringLiteral("/omaseed/state.json");
}

QJsonObject Xdg::readState()
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject{{QStringLiteral("version"), 1}};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return QJsonObject{{QStringLiteral("version"), 1}};

    return doc.object();
}

void Xdg::writeState(const QJsonObject &state)
{
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(state).toJson(QJsonDocument::Indented));
}

bool Xdg::firstRunPromptNeeded() const
{
    if (!QFile::exists(stateFilePath()))
        writeState(QJsonObject{{QStringLiteral("version"), 1}});

    const QJsonObject state = readState();
    return !state.value(QStringLiteral("magnetHandlerAsked")).toBool(false);
}

void Xdg::setFirstRunPromptAsked()
{
    QJsonObject state = readState();
    state.insert(QStringLiteral("magnetHandlerAsked"), true);
    writeState(state);
}

Addable Xdg::detectClipboardMagnet() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return Addable{};

    const QString text = clipboard->text();
    if (text.isEmpty())
        return Addable{};

    const QList<Addable> found = parseAddableList(text);
    if (!found.isEmpty())
        return found.first();

    return parseAddable(text);
}

} // namespace omaseed::integration
