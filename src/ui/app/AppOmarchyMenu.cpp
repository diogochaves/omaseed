// App's side of Settings › Appearance › In the Omarchy menu (#216; board 53 §6, DECISIONS C36
// M11–M15). OmarchyMenuFile reads and writes the file; this watches it, says what it holds in
// the switch's terms, and keeps the block's Servers › in step with the server list.

#include "ui/app/App.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>

namespace omaseed::app {

using omaseed::core::OmarchyMenuBlock;
using omaseed::core::OmarchyMenuFile;
using omaseed::core::ServerListModel;

namespace {

Q_LOGGING_CATEGORY(lcMenu, "omaseed.omarchymenu")

QString stateName(OmarchyMenuFile::State state)
{
    switch (state) {
    case OmarchyMenuFile::State::NoBlock:
        return QStringLiteral("noBlock");
    case OmarchyMenuFile::State::Block:
        return QStringLiteral("block");
    case OmarchyMenuFile::State::OldRow:
        return QStringLiteral("oldRow");
    case OmarchyMenuFile::State::UsersRow:
        return QStringLiteral("usersRow");
    case OmarchyMenuFile::State::NoSafePlace:
        return QStringLiteral("noSafePlace");
    case OmarchyMenuFile::State::NoFile:
        return QStringLiteral("noFile");
    case OmarchyMenuFile::State::NoMenu:
        break;
    }
    return QStringLiteral("noMenu");
}

} // namespace

void App::setUpOmarchyMenu()
{
    m_omarchyMenuWatcher = new QFileSystemWatcher(this);
    m_omarchyMenuRead = new QTimer(this);
    m_omarchyMenuRead->setSingleShot(true);
    m_omarchyMenuRead->setInterval(50);
    connect(m_omarchyMenuRead, &QTimer::timeout, this, &App::readOmarchyMenu);
    connect(m_omarchyMenuWatcher,
            &QFileSystemWatcher::fileChanged,
            m_omarchyMenuRead,
            qOverload<>(&QTimer::start));
    connect(m_omarchyMenuWatcher,
            &QFileSystemWatcher::directoryChanged,
            m_omarchyMenuRead,
            qOverload<>(&QTimer::start));

    // M11: a server added, removed or renamed changes Servers ›. dataChanged also carries the
    // last-seen figures; followOmarchyMenuServers writes only when the block's text moves.
    connect(m_serverList, &QAbstractItemModel::dataChanged, this, &App::followOmarchyMenuServers);
    connect(m_serverList, &QAbstractItemModel::modelReset, this, &App::followOmarchyMenuServers);
    connect(m_serverList, &QAbstractItemModel::rowsInserted, this, &App::followOmarchyMenuServers);
    connect(m_serverList, &QAbstractItemModel::rowsRemoved, this, &App::followOmarchyMenuServers);
    connect(m_serverList, &QAbstractItemModel::rowsMoved, this, &App::followOmarchyMenuServers);

    readOmarchyMenu();
}

void App::setOmarchyMenuPaths(const QString &userFile, const QString &omarchyMenu)
{
    m_omarchyMenu = OmarchyMenuFile(userFile, omarchyMenu);
    m_omarchyMenuError.clear();
    readOmarchyMenu();
}

void App::readOmarchyMenu()
{
    // Watch the file, its folder and the nearest folder above that exists, so a file made,
    // replaced (QSaveFile renames over it, which drops the watch) or removed is seen.
    const QStringList watched = m_omarchyMenuWatcher->files() + m_omarchyMenuWatcher->directories();
    if (!watched.isEmpty())
        m_omarchyMenuWatcher->removePaths(watched);
    const QFileInfo file(m_omarchyMenu.userFile());
    QStringList paths;
    if (file.exists())
        paths << file.absoluteFilePath();
    QDir dir = file.absoluteDir();
    while (!dir.exists() && !dir.isRoot())
        dir.setPath(QFileInfo(dir.absolutePath()).absolutePath());
    if (dir.exists())
        paths << dir.absolutePath();
    if (!paths.isEmpty())
        m_omarchyMenuWatcher->addPaths(paths);

    m_omarchyMenuBlockSeen.clear();
    QString detail;
    const OmarchyMenuFile::State state = m_omarchyMenu.state(&detail);
    const bool changed = state != m_omarchyMenuState || detail != m_omarchyMenuDetail;
    m_omarchyMenuState = state;
    m_omarchyMenuDetail = detail;
    if (changed) {
        // A failed write is about the file as it was; the file has moved since.
        m_omarchyMenuError.clear();
        Q_EMIT omarchyMenuChanged();
    }
    followOmarchyMenuServers();
}

bool App::inOmarchyMenu() const
{
    return m_omarchyMenuState == OmarchyMenuFile::State::Block;
}

void App::setInOmarchyMenu(bool on)
{
    if (on == inOmarchyMenu() || !omarchyMenuOffered())
        return;
    QString error;
    const bool ok = on ? m_omarchyMenu.add(OmarchyMenuBlock::text(omarchyMenuServers()), &error)
                       : m_omarchyMenu.remove(&error);
    if (!ok)
        qCWarning(lcMenu) << "omaseed: the Omarchy menu's file:" << error;
    readOmarchyMenu();
    if (!ok && m_omarchyMenuError != error) {
        m_omarchyMenuError = error;
        Q_EMIT omarchyMenuChanged();
    } else if (ok && !m_omarchyMenuError.isEmpty()) {
        m_omarchyMenuError.clear();
        Q_EMIT omarchyMenuChanged();
    }
}

QString App::omarchyMenuState() const
{
    return stateName(m_omarchyMenuState);
}

bool App::omarchyMenuOffered() const
{
    switch (m_omarchyMenuState) {
    case OmarchyMenuFile::State::UsersRow:
    case OmarchyMenuFile::State::NoSafePlace:
    case OmarchyMenuFile::State::NoMenu:
        return false;
    default:
        return true;
    }
}

QString App::omarchyMenuReason() const
{
    if (!m_omarchyMenuError.isEmpty())
        return m_omarchyMenuError;
    if (m_omarchyMenuState == OmarchyMenuFile::State::UsersRow ||
        m_omarchyMenuState == OmarchyMenuFile::State::NoSafePlace)
        return m_omarchyMenuDetail;
    return {};
}

QString App::omarchyMenuFile() const
{
    const QString path = m_omarchyMenu.userFile();
    const QString home = QDir::homePath();
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

QList<OmarchyMenuBlock::Server> App::omarchyMenuServers() const
{
    QList<OmarchyMenuBlock::Server> servers;
    for (int row = 0; row < m_serverList->count(); ++row) {
        const QModelIndex at = m_serverList->index(row, 0);
        servers.append({m_serverList->data(at, ServerListModel::IdRole).toString(),
                        m_serverList->data(at, ServerListModel::NameRole).toString()});
    }
    return servers;
}

void App::followOmarchyMenuServers()
{
    // The demo's servers are invented: they never reach the menu on their own.
    if (m_demoMode || m_omarchyMenuState != OmarchyMenuFile::State::Block)
        return;
    QByteArray block = OmarchyMenuBlock::text(omarchyMenuServers()).toUtf8();
    if (!block.endsWith('\n'))
        block.append('\n');
    // The server list's signals come with every last-seen figure: the file is read only when
    // the block's text has moved since it was last found there.
    if (block == m_omarchyMenuBlockSeen)
        return;
    m_omarchyMenuBlockSeen = block;
    QFile file(m_omarchyMenu.userFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    // Already there: the block's text, then the closing marker's line (however indented, so
    // a hand copy's marker never makes a write that is seen, read and written again).
    const QByteArray text = file.readAll();
    for (qsizetype at = text.indexOf(block); at >= 0; at = text.indexOf(block, at + 1)) {
        qsizetype end = at + block.size();
        while (end < text.size() && (text.at(end) == ' ' || text.at(end) == '\t'))
            ++end;
        if (text.mid(end).startsWith(OmarchyMenuFile::closeMarker()))
            return;
    }
    file.close();
    QString error;
    if (!m_omarchyMenu.rewrite(QString::fromUtf8(block), &error))
        qCWarning(lcMenu) << "omaseed: the Omarchy menu's file:" << error;
    // The watcher reads the file again.
}

} // namespace omaseed::app
