#include "integration/Xdg.hpp"

#include "core/MagnetLink.hpp"
#include "core/StateFile.hpp"
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

#include <algorithm>

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
    // `xt` must be a parameter of its own (after `?` or `&`), not the tail of another one
    // such as `foo_xt=`; the hash ends at `&` or the end of the text.
    static const QRegularExpression btihHex(
        QStringLiteral("[?&]xt=urn:btih:([0-9A-Fa-f]{40})(?:&|$)"));
    static const QRegularExpression btihB32(
        QStringLiteral("[?&]xt=urn:btih:([A-Za-z2-7]{32})(?:&|$)"));
    static const QRegularExpression btmh(QStringLiteral("[?&]xt=urn:btmh:([0-9A-Fa-f]+)(?:&|$)"));

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

Addable Xdg::parseAddable(const QString &textIn, BareHash bareHash)
{
    const QString text = textIn.trimmed();
    if (text.isEmpty())
        return Addable{};

    if (looksLikeMagnet(text))
        return Addable{Addable::Kind::Magnet, text};

    // #147: the whole text, never a hash found inside it, and only when given on purpose.
    if (bareHash == BareHash::Take && core::MagnetLink::fromInfoHash(text).isValid())
        return Addable{Addable::Kind::InfoHash, text};

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

QList<Addable> Xdg::parseAddableList(const QString &text, BareHash bareHash)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QList<Addable> result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const Addable whole = parseAddable(line, bareHash);
        // A path with spaces in it is one file; any other line holding whitespace may be
        // several links pasted side by side, each parsed on its own.
        if (whole.kind == Addable::Kind::TorrentFile || !line.contains(whitespace)) {
            if (whole.kind != Addable::Kind::None)
                result.append(whole);
            continue;
        }
        QList<Addable> tokens;
        bool prose = false;
        for (const QString &token : line.split(whitespace, Qt::SkipEmptyParts)) {
            const Addable addable = parseAddable(token, bareHash);
            if (addable.kind != Addable::Kind::None)
                tokens.append(addable);
            else
                prose = true;
        }
        // #147: a magnet or a link says what it is wherever it stands; a run of hex in a
        // sentence may be anything (a commit, a checksum), so it is left to the sentence.
        if (prose)
            tokens.removeIf(
                [](const Addable &addable) { return addable.kind == Addable::Kind::InfoHash; });
        if (!tokens.isEmpty())
            result.append(tokens);
        else if (whole.kind != Addable::Kind::None)
            result.append(whole);
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
    // File managers put the same file:// URLs in the drop's text too (Nautilus does): one
    // file dropped was listed twice. The text adds only what the URLs did not.
    for (const Addable &addable : parseAddableList(text, BareHash::Take)) {
        const bool known = std::any_of(result.cbegin(), result.cend(), [&](const Addable &seen) {
            return seen.kind == addable.kind && seen.value == addable.value;
        });
        if (!known)
            result.append(addable);
    }
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

// state.json has one implementation, core::StateFile (an atomic rewrite); these delegate.
QString Xdg::stateFilePath()
{
    return core::StateFile::path();
}

QJsonObject Xdg::readState()
{
    return core::StateFile::read();
}

void Xdg::writeState(const QJsonObject &state)
{
    core::StateFile::write(state);
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

    return probeClipboardText(clipboard->text());
}

Addable Xdg::probeClipboardText(const QString &text) const
{
    if (text.isEmpty())
        return Addable{};

    const QList<Addable> found = parseAddableList(text, BareHash::Ignore);
    if (!found.isEmpty())
        return found.first();

    return parseAddable(text, BareHash::Ignore);
}

} // namespace omaseed::integration
