#include "ui/app/StatusService.hpp"

#include "core/ApiError.hpp"
#include "core/CategoryModel.hpp"
#include "core/GlobalState.hpp"
#include "core/MagnetLink.hpp"
#include "core/QbtClient.hpp"
#include "core/StateFile.hpp"
#include "core/TorrentFile.hpp"
#include "integration/Xdg.hpp"
#include "ui/app/App.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#include <memory>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

/// Bigger than any real .torrent; qBittorrent itself refuses past 100 MiB.
constexpr qint64 kMaxTorrentFileBytes = 100 * 1024 * 1024;

const QString kStateSection = QStringLiteral("widgetAdd");

QString compact(const QJsonDocument &document)
{
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QString urlName(const QString &text)
{
    const QUrl url(text);
    const QString path = url.path();
    const QString last = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    return last.isEmpty() ? url.host() : QUrl::fromPercentEncoding(last.toUtf8());
}

QJsonObject entry(const StatusService::Source &source)
{
    return QJsonObject{
        {QStringLiteral("source"), source.source},
        {QStringLiteral("name"), source.name},
        {QStringLiteral("hash"), source.hash},
    };
}

QJsonObject failure(const QString &source, const QString &error)
{
    return QJsonObject{{QStringLiteral("source"), source}, {QStringLiteral("error"), error}};
}

} // namespace

StatusService::StatusService(App *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{}

// --- alternative speed limits ---------------------------------------------------------------

bool StatusService::toggleAltSpeed()
{
    const bool now = m_app->globalState()->useAltSpeedLimits();
    if (m_app->isDemoMode() || !m_app->hasServer())
        return now;
    // The tray's request: the server flips whatever it has, and the next poll says what that is.
    m_app->toggleAltSpeed();
    return !now;
}

bool StatusService::setAltSpeed(bool on)
{
    const bool now = m_app->globalState()->useAltSpeedLimits();
    if (m_app->isDemoMode() || !m_app->hasServer())
        return now;
    QPointer<App> app(m_app);
    m_app->client()->transferSetSpeedLimitsMode(on)->then(
        this, [app, on](const QbtResult<QByteArray> &result) {
            if (!result.isError() || app.isNull())
                return;
            // A server without setSpeedLimitsMode: flip it when it is not where it should be.
            if (result.error().kind() == ApiError::Kind::NotFound) {
                if (app->globalState()->useAltSpeedLimits() != on)
                    app->toggleAltSpeed();
                return;
            }
            qWarning().noquote()
                << QStringLiteral("alt-speed set failed: %1").arg(result.error().toString());
        });
    return on;
}

// --- sources --------------------------------------------------------------------------------

StatusService::Source StatusService::parseSource(const QString &text)
{
    Source source;
    source.source = text;
    const integration::Addable addable = integration::Xdg::parseAddable(text);
    source.value = addable.value;

    switch (addable.kind) {
    case integration::Addable::Kind::Magnet: {
        source.kind = QStringLiteral("magnet");
        const MagnetLink link = MagnetLink::parse(addable.value);
        if (!link.isValid()) {
            source.error = tr("this magnet link carries no info hash");
            return source;
        }
        source.valid = true;
        source.infoHashV1 = link.infoHashV1;
        source.infoHashV2 = link.infoHashV2;
        source.name = link.displayName.isEmpty() ? link.infoHashV1 : link.displayName;
        source.size = qMax<qint64>(0, link.exactLength);
        break;
    }
    case integration::Addable::Kind::HttpUrl:
        source.kind = QStringLiteral("url");
        source.valid = true;
        source.name = urlName(addable.value);
        break;
    case integration::Addable::Kind::TorrentFile: {
        source.kind = QStringLiteral("file");
        source.fileName = QFileInfo(addable.value).fileName();
        source.name = source.fileName;
        QFile file(addable.value);
        if (!file.open(QIODevice::ReadOnly)) {
            source.error = tr("cannot read %1").arg(addable.value);
            return source;
        }
        if (file.size() > kMaxTorrentFileBytes) {
            source.error = tr("%1 is too large to be a .torrent").arg(addable.value);
            return source;
        }
        source.bytes = file.readAll();
        QString error;
        const TorrentFile parsed = TorrentFile::parse(source.bytes, &error);
        if (!parsed.isValid()) {
            source.error = error.isEmpty() ? tr("this file is not a .torrent") : error;
            source.bytes.clear();
            return source;
        }
        source.valid = true;
        source.name = parsed.name;
        source.size = parsed.totalSize;
        source.infoHashV1 = parsed.infoHashV1;
        source.infoHashV2 = parsed.infoHashV2;
        break;
    }
    case integration::Addable::Kind::None:
        source.error = tr("not a magnet link, a URL or a .torrent file");
        return source;
    }
    source.hash = TorrentFile::torrentIdFor(source.infoHashV1, source.infoHashV2);
    return source;
}

void StatusService::parseInBackground(const QStringList &sources,
                                      std::function<void(const QList<Source> &)> done)
{
    // Off the GUI thread, like the add sheet's parse (plan §4 T4.2): a .torrent is read and
    // hashed. The worker touches nothing of ours and hops back through the application object.
    QPointer<StatusService> self(this);
    QThreadPool::globalInstance()->start([self, sources, done = std::move(done)]() {
        QList<Source> parsed;
        parsed.reserve(sources.size());
        for (const QString &text : sources)
            parsed.append(parseSource(text));
        QCoreApplication *application = QCoreApplication::instance();
        if (!application)
            return;
        QMetaObject::invokeMethod(
            application,
            [self, parsed, done]() {
                if (!self.isNull())
                    done(parsed);
            },
            Qt::QueuedConnection);
    });
}

bool StatusService::isDuplicate(const Source &source) const
{
    if (!source.valid || (source.infoHashV1.isEmpty() && source.infoHashV2.isEmpty()))
        return false;
    return !m_app->findDuplicate(source.infoHashV1, source.infoHashV2).isEmpty();
}

void StatusService::describeSources(const QStringList &sources, Reply reply)
{
    parseInBackground(sources, [this, reply](const QList<Source> &parsed) {
        QJsonArray list;
        for (const Source &source : parsed) {
            const bool duplicate = isDuplicate(source);
            list.append(QJsonObject{
                {QStringLiteral("source"), source.source},
                {QStringLiteral("kind"), source.kind},
                {QStringLiteral("valid"), source.valid},
                {QStringLiteral("error"), source.error},
                {QStringLiteral("name"), source.name},
                {QStringLiteral("size"), source.size},
                {QStringLiteral("hash"), source.hash},
                {QStringLiteral("duplicate"), duplicate},
                {QStringLiteral("duplicateOn"), duplicate ? m_app->serverName() : QString()},
            });
        }
        reply(compact(QJsonDocument(list)));
    });
}

void StatusService::addSources(const QStringList &sources,
                               const QString &category,
                               bool stopped,
                               Reply reply)
{
    if (!m_app->isDemoMode() && !m_app->hasServer()) {
        QJsonArray failed;
        for (const QString &source : sources)
            failed.append(failure(source, tr("no server is configured")));
        reply(compact(QJsonDocument(QJsonObject{{QStringLiteral("added"), QJsonArray()},
                                                {QStringLiteral("duplicates"), QJsonArray()},
                                                {QStringLiteral("failed"), failed}})));
        return;
    }

    parseInBackground(sources, [this, category, stopped, reply](const QList<Source> &parsed) {
        // What the add sheet sends by default: the category, its save path, and automatic
        // management, so the category decides where the files go.
        QbtClient::AddOptions options;
        if (!category.isEmpty()) {
            options.category = category;
            const QString savePath = m_app->categories()->savePathOf(category);
            if (!savePath.isEmpty())
                options.savepath = savePath;
        }
        options.autoTMM = true;
        if (stopped)
            options.stopped = true;

        struct Run
        {
            QJsonArray added;
            QJsonArray duplicates;
            QJsonArray failed;
            int pending = 0;
            Reply reply;
            void finishOne()
            {
                if (--pending > 0)
                    return;
                reply(compact(QJsonDocument(QJsonObject{{QStringLiteral("added"), added},
                                                        {QStringLiteral("duplicates"), duplicates},
                                                        {QStringLiteral("failed"), failed}})));
            }
        };
        auto run = std::make_shared<Run>();
        run->reply = reply;
        run->pending = 1; // held until every request is out, so a fast answer cannot finish early

        bool sentAny = false;
        for (const Source &source : parsed) {
            if (!source.valid) {
                run->failed.append(failure(source.source, source.error));
                continue;
            }
            if (isDuplicate(source)) {
                run->duplicates.append(entry(source));
                continue;
            }
            sentAny = true;
            if (m_app->isDemoMode()) {
                qInfo().noquote()
                    << QStringLiteral("demo: would add %1 (%2)").arg(source.value, source.kind);
                run->added.append(entry(source));
                continue;
            }
            QbtReply<QByteArray> *request =
                source.kind == QLatin1String("file")
                    ? m_app->client()->torrentsAddFile(source.fileName, source.bytes, options)
                    : m_app->client()->torrentsAddUrls(QStringList{source.value}, options);
            ++run->pending;
            request->then(this, [run, source](const QbtResult<QByteArray> &result) {
                if (result.isError()) {
                    // 5.x answers 409 for a torrent it already has.
                    if (result.error().kind() == ApiError::Kind::Conflict)
                        run->duplicates.append(entry(source));
                    else
                        run->failed.append(failure(source.source, result.error().message()));
                } else if (QString::fromUtf8(result.value())
                               .trimmed()
                               .compare(QStringLiteral("Fails."), Qt::CaseInsensitive) == 0) {
                    // 4.x refuses with a 200 and this body.
                    run->failed.append(
                        failure(source.source, tr("the server refused this torrent")));
                } else {
                    Source added = source;
                    const QJsonArray ids = QJsonDocument::fromJson(result.value())
                                               .object()
                                               .value(QStringLiteral("added_torrent_ids"))
                                               .toArray();
                    if (!ids.isEmpty())
                        added.hash = ids.first().toString();
                    run->added.append(entry(added));
                }
                run->finishOne();
            });
        }

        if (sentAny && !m_app->isDemoMode()) {
            StateFile::writeSection(kStateSection,
                                    QJsonObject{{QStringLiteral("category"), category}});
            Q_EMIT addCategoryRemembered();
        }
        qInfo().noquote() << QStringLiteral("widget add: %1 source(s), category \"%2\"%3")
                                 .arg(parsed.size())
                                 .arg(category, stopped ? QStringLiteral(", stopped") : QString());
        run->finishOne();
    });
}

QString StatusService::rememberedCategory()
{
    return StateFile::readSection(kStateSection).value(QStringLiteral("category")).toString();
}

// --- the window -----------------------------------------------------------------------------

void StatusService::openAddSheet(const QStringList &sources)
{
    Q_EMIT raiseRequested();
    // Nothing ticked: the sheet as `a` opens it (and an open one looks at Downloads again).
    if (sources.isEmpty())
        Q_EMIT m_app->addRequested();
    else
        m_app->openUris(sources);
}

void StatusService::showTorrent(const QString &hash)
{
    if (hash.isEmpty()) {
        Q_EMIT raiseRequested();
        return;
    }
    // MainView moves the cursor there and main.cpp raises the window, as for a notice's "show".
    Q_EMIT m_app->showTorrentRequested(hash);
}

void StatusService::openFolder(const QString &hash)
{
    m_app->openFolder(hash);
}

// --- the shared menu (docs/33 §11, D24) -----------------------------------------------------

bool StatusService::switchServer(const QString &id)
{
    // The widget's popup stays open and refills from the next status write (W1).
    return m_app->switchServer(id);
}

bool StatusService::invoke(const QString &id)
{
    return m_app->barMenu()->invoke(id);
}

} // namespace omaseed::app
