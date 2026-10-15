// App's #33 block (docs/24 §4, §5, §7): a qBittorrent's torrents exported to a file and
// imported back, from Settings › qBittorrent › Backup. Kept out of App.cpp so it reads as one
// piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/Version.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QWindow>

namespace omaseed::app {

using namespace omaseed::core;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("App", text);
}

QString count(int n, const char *one, const char *many)
{
    return n == 1 ? tr(one) : tr(many).arg(n);
}

/// `path` with the home folder written as `~`.
QString tilde(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + path.mid(home.size())
                                                    : path;
}

/// A server's name as part of a file name: "home", "seedbox-2".
QString stem(const QString &name)
{
    QString out = name.toLower();
    out.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    out.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return out.isEmpty() ? QStringLiteral("server") : out;
}

/// The portal's chooser, as a dialog of our window (App.cpp's runPicker, see there).
QStringList pick(QFileDialog &dialog, QWindow *window)
{
    if (window) {
        dialog.winId();
        dialog.windowHandle()->setTransientParent(window);
    }
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles() : QStringList();
}

} // namespace

void App::setTorrentsRunning(const QString &what, const QString &progress)
{
    m_torrentsRunning = what;
    m_torrentsProgress = progress;
    Q_EMIT torrentsBackupChanged();
}

QString App::torrentsExportSummary() const
{
    TorrentModel *model = m_engine->torrents();
    const int n = model->count();
    if (n == 0)
        return tr("No torrents to export yet.");
    qint64 bytes = 0;
    int magnets = 0;
    for (int row = 0; row < n; ++row) {
        const Torrent &t = model->torrentAt(row);
        bytes += std::max<qint64>(0, t.size);
        if (t.totalSize < 0)
            ++magnets;
    }
    QString text =
        n == 1 ? tr("1 torrent with its save path, category, tags and limits.")
               : tr("%1 torrents with their save paths, categories, tags and limits.").arg(n);
    text += QLatin1Char(' ') + tr("The %1 of files stay where they are.").arg(Format::size(bytes));
    if (magnets > 0)
        text += QLatin1Char(' ') + count(magnets,
                                         "1 magnet without metadata goes in as a link.",
                                         "%1 magnets without metadata go in as links.");
    return text;
}

QString App::pickTorrentsExportPath() const
{
    const QString suggested = QDir::homePath() + QStringLiteral("/torrents-") +
                              stem(m_server.name) + QLatin1Char('-') +
                              QDate::currentDate().toString(Qt::ISODate) + TorrentsBackup::kSuffix;
    QFileDialog dialog(nullptr,
                       tr("Export qBittorrent's torrents"),
                       suggested,
                       tr("omaseed torrents (*.omaseed-torrents)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    QString path = pick(dialog, m_dialogParent).value(0);
    if (!path.isEmpty() && !path.endsWith(TorrentsBackup::kSuffix))
        path += TorrentsBackup::kSuffix;
    return path;
}

void App::exportTorrents(const QString &path)
{
    if (path.isEmpty())
        return;
    if (!m_torrentsRunning.isEmpty()) {
        Q_EMIT torrentsExportFinished(tr("Not exported: another export or import is running."),
                                      false);
        return;
    }
    if (m_demoMode || !m_hasServer || !m_engine->globalState()->connected()) {
        Q_EMIT torrentsExportFinished(tr("Not exported: %1 is not connected.").arg(m_server.name),
                                      false);
        return;
    }
    auto *exporter = new TorrentsExport(m_client, this);
    m_torrentsExport = exporter;
    setTorrentsRunning(QStringLiteral("export"), tr("exporting…"));
    connect(exporter, &TorrentsExport::progress, this, [this](int done, int total) {
        setTorrentsRunning(QStringLiteral("export"),
                           tr("exporting %1 of %2…").arg(done).arg(total));
    });
    connect(
        exporter,
        &TorrentsExport::finished,
        this,
        [this, exporter](const TorrentsExport::Outcome &outcome) {
            exporter->deleteLater();
            setTorrentsRunning(QString());
            if (outcome.cancelled) {
                Q_EMIT torrentsExportFinished(tr("Export stopped; no file was written."), false);
                return;
            }
            if (!outcome.ok) {
                Q_EMIT torrentsExportFinished(tr("Not exported: %1").arg(outcome.error), false);
                return;
            }
            QString text = outcome.torrents == 1
                               ? tr("Exported 1 torrent to %1.").arg(tilde(outcome.path))
                               : tr("Exported %1 torrents to %2.")
                                     .arg(outcome.torrents)
                                     .arg(tilde(outcome.path));
            if (outcome.magnets > 0)
                text += QLatin1Char(' ') + count(outcome.magnets,
                                                 "1 magnet went in as a link.",
                                                 "%1 magnets went in as links.");
            if (outcome.gone > 0)
                text += QLatin1Char(' ') + count(outcome.gone,
                                                 "1 torrent deleted meanwhile was left out.",
                                                 "%1 torrents deleted meanwhile were left out.");
            qInfo().noquote() << QStringLiteral("torrents: %1").arg(text);
            Q_EMIT torrentsExportFinished(text, true);
        });
    exporter->start(path, m_server.name, m_server.url.toString(), omaseed::core::version());
}

QString App::pickTorrentsImportFile() const
{
    QFileDialog dialog(nullptr,
                       tr("Import qBittorrent's torrents"),
                       QDir::homePath(),
                       tr("omaseed torrents (*.omaseed-torrents);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    return pick(dialog, m_dialogParent).value(0);
}

QString App::readTorrentsImport(const QString &path)
{
    m_torrentsFile.reset();
    if (path.isEmpty())
        return tr("No file was chosen.");
    if (!m_torrentsRunning.isEmpty())
        return tr("Another export or import is running.");
    if (m_demoMode || !m_hasServer || !m_engine->globalState()->connected())
        return tr("%1 is not connected.").arg(m_server.name);
    QString error;
    std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(path, &error);
    if (!contents)
        return tr("Not imported: %1.").arg(error);
    m_torrentsFile = std::move(contents);

    // What the server has now: every hash a torrent there can be known by, its categories
    // and tags. Read afresh, not from the last poll, so the plan is the server's own.
    struct Read
    {
        TorrentsImport::Here here;
        int waiting = 3;
        QString error;
    };
    auto read = std::make_shared<Read>();
    const int which = ++m_torrentsRead;
    setTorrentsRunning(QStringLiteral("read"), tr("reading %1…").arg(m_server.name));
    const auto settle = [this, read, which] {
        if (--read->waiting > 0 || which != m_torrentsRead)
            return;
        setTorrentsRunning(QString());
        if (!read->error.isEmpty()) {
            m_torrentsFile.reset();
            Q_EMIT torrentsImportRead(
                {{QStringLiteral("ok"), false},
                 {QStringLiteral("error"),
                  tr("Could not read %1's torrents: %2").arg(m_server.name, read->error)}});
            return;
        }
        m_torrentsPlan = TorrentsImport::plan(
            *m_torrentsFile,
            read->here,
            TorrentsImport::Journal::load(TorrentsImport::Journal::pathFor(m_server.id)));
        Q_EMIT torrentsImportRead(torrentsImportInfo());
    };
    m_client->torrentsInfo()->then(this, [read, settle](const QbtResult<QJsonArray> &r) {
        if (r.isError())
            read->error = r.error().toString();
        for (const QJsonValue &v : r.value()) {
            const QJsonObject t = v.toObject();
            for (const QString &key : {QStringLiteral("hash"),
                                       QStringLiteral("infohash_v1"),
                                       QStringLiteral("infohash_v2")}) {
                const QString id = t.value(key).toString().toLower();
                if (!id.isEmpty())
                    read->here.ids.insert(id);
            }
        }
        settle();
    });
    m_client->torrentsCategories()->then(this, [read, settle](const QbtResult<QJsonObject> &r) {
        if (r.isError())
            read->error = r.error().toString();
        read->here.categories = r.value().keys();
        settle();
    });
    m_client->torrentsTags()->then(this, [read, settle](const QbtResult<QJsonArray> &r) {
        if (r.isError())
            read->error = r.error().toString();
        for (const QJsonValue &tag : r.value())
            read->here.tags.append(tag.toString());
        settle();
    });
    return QString();
}

QVariantMap App::torrentsImportInfo()
{
    const TorrentsBackup::Contents &file = *m_torrentsFile;
    const TorrentsImport::Plan &plan = m_torrentsPlan;
    const QString name = m_server.name.isEmpty() ? tr("this server") : m_server.name;

    QVariantList rows;
    const auto row = [&rows](const QString &key,
                             const QString &label,
                             const QString &verb,
                             int n,
                             const QString &size,
                             bool dim) {
        rows.append(QVariantMap{{QStringLiteral("key"), key},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("verb"), verb},
                                {QStringLiteral("count"), n},
                                {QStringLiteral("sizeText"), n > 0 ? size : QStringLiteral("—")},
                                {QStringLiteral("dim"), dim}});
    };
    row(QStringLiteral("new"),
        tr("new to %1").arg(name),
        tr("added"),
        int(plan.add.size()),
        Format::size(plan.addBytes),
        false);
    row(QStringLiteral("present"),
        tr("already in %1 (same hash)").arg(name),
        tr("skipped"),
        int(plan.skipped.size()),
        Format::size(plan.skippedBytes),
        true);
    row(QStringLiteral("magnets"),
        tr("magnets without metadata"),
        tr("added as links"),
        int(plan.magnets.size()),
        QStringLiteral("—"),
        plan.magnets.isEmpty());
    if (!plan.finish.isEmpty()) {
        qint64 bytes = 0;
        for (const int i : plan.finish)
            bytes += file.records.at(i).size;
        row(QStringLiteral("finish"),
            tr("added last time, not finished"),
            tr("finished"),
            int(plan.finish.size()),
            Format::size(bytes),
            false);
    }

    // D7 a: the rows start from the path maps omaseed has (DECISIONS C9).
    std::optional<PathMap> source;
    QString sourceName;
    const QString key = OmaseedBackup::addressKey(QUrl(file.manifest.serverUrl));
    for (const Server &server : m_servers->servers()) {
        if (server.id != m_server.id && OmaseedBackup::addressKey(server.url) == key) {
            PathMap map;
            for (const PathMapping &m : server.pathMap)
                map.addMapping(m.remote, m.local);
            source = map;
            sourceName = server.name;
            break;
        }
    }
    const PathMap target = pathMapEnabled() ? serverPathMap() : PathMap();
    QVariantList paths;
    for (const TorrentsImport::PathRow &r :
         TorrentsImport::prefill(file, source, sourceName, target, name))
        paths.append(QVariantMap{{QStringLiteral("from"), r.from},
                                 {QStringLiteral("to"), r.to},
                                 {QStringLiteral("source"), r.source}});
    if (paths.isEmpty())
        paths.append(QVariantMap{{QStringLiteral("from"), file.manifest.savePath},
                                 {QStringLiteral("to"), QString()},
                                 {QStringLiteral("source"), QString()}});

    QString created;
    if (!plan.categories.isEmpty() && !plan.tags.isEmpty())
        created = tr("%1 and %2 they use are created first")
                      .arg(count(int(plan.categories.size()), "1 category", "%1 categories"),
                           count(int(plan.tags.size()), "1 tag", "%1 tags"));
    else if (!plan.categories.isEmpty())
        created = count(int(plan.categories.size()),
                        "1 category they use is created first",
                        "%1 categories they use are created first");
    else if (!plan.tags.isEmpty())
        created = count(int(plan.tags.size()),
                        "1 tag they use is created first",
                        "%1 tags they use are created first");

    QString version = file.manifest.version.isEmpty() ? QStringLiteral("?") : file.manifest.version;
    if (version.startsWith(QLatin1Char('v')))
        version.remove(0, 1);
    const int n = int(file.records.size());
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("title"),
             n == 1 ? tr("Import 1 torrent into %1").arg(name)
                    : tr("Import %1 torrents into %2").arg(n).arg(name)},
            {QStringLiteral("sub"),
             tr("From %1 (qBittorrent %2).").arg(QFileInfo(file.path).fileName(), version)},
            {QStringLiteral("plan"), rows},
            {QStringLiteral("paths"), paths},
            {QStringLiteral("created"), created},
            {QStringLiteral("importCount"), plan.count()},
            {QStringLiteral("check"), true}};
}

QList<TorrentsImport::PathRow> App::torrentsRows(const QVariantList &rows) const
{
    QList<TorrentsImport::PathRow> out;
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        out.append({m.value(QStringLiteral("from")).toString().trimmed(),
                    m.value(QStringLiteral("to")).toString().trimmed(),
                    m.value(QStringLiteral("source")).toString()});
    }
    return out;
}

QString App::torrentsImportPathsNote(const QVariantList &rows) const
{
    if (!m_torrentsFile)
        return QString();
    const QList<TorrentsImport::PathRow> list = torrentsRows(rows);
    QStringList froms;
    for (const TorrentsImport::PathRow &r : list) {
        if (!r.from.isEmpty() && !r.to.isEmpty())
            froms.append(r.from);
    }
    if (froms.isEmpty())
        return tr("save paths are kept as they are in the file");
    const int n = TorrentsImport::uncovered(*m_torrentsFile, m_torrentsPlan, list);
    if (n == 0)
        return QString();
    if (froms.size() == 1)
        return n == 1 ? tr("1 save path doesn't start with %1 and is kept as it is")
                            .arg(froms.constFirst())
                      : tr("%1 save paths don't start with %2 and are kept as they are")
                            .arg(n)
                            .arg(froms.constFirst());
    return count(n,
                 "1 save path no row covers is kept as it is",
                 "%1 save paths no row covers are kept as they are");
}

void App::applyTorrentsImport(const QVariantMap &choice)
{
    if (!m_torrentsFile) {
        Q_EMIT torrentsImportFinished(tr("Choose a file to import first."), false);
        return;
    }
    if (!m_torrentsRunning.isEmpty()) {
        Q_EMIT torrentsImportFinished(tr("Another export or import is running."), false);
        return;
    }
    TorrentsImport::Options options;
    options.rows = torrentsRows(choice.value(QStringLiteral("rows")).toList());
    options.check = choice.value(QStringLiteral("check"), true).toBool();
    options.journalPath = TorrentsImport::Journal::pathFor(m_server.id);
    // #64, #67: the rename guard reads the server's folders where this machine sees them.
    options.localPathFor = [this](const QString &serverPath) {
        return m_freeSpace.localPathFor(serverPath);
    };
    auto *import = new TorrentsImport(m_client, this);
    m_torrentsImport = import;
    const QString name = m_server.name;
    const int total = m_torrentsPlan.count();
    setTorrentsRunning(QStringLiteral("import"), tr("importing into %1…").arg(name));
    connect(
        import, &TorrentsImport::progress, this, [this](const QString &phase, int done, int of) {
            setTorrentsRunning(QStringLiteral("import"),
                               phase == QLatin1String("checking")
                                   ? tr("checking files: %1 of %2…").arg(done).arg(of)
                                   : tr("adding %1 of %2…").arg(done).arg(of));
        });
    connect(import,
            &TorrentsImport::finished,
            this,
            [this, import, name, total](const TorrentsImport::Outcome &o) {
                import->deleteLater();
                setTorrentsRunning(QString());
                QStringList parts;
                const int torrents = o.added + o.finished;
                if (torrents > 0)
                    parts.append(count(torrents, "1 torrent", "%1 torrents"));
                if (o.magnets > 0)
                    parts.append(count(o.magnets, "1 link", "%1 links"));
                QString text =
                    parts.isEmpty()
                        ? tr("Nothing was imported into %1.").arg(name)
                        : tr("Imported into %1: %2.").arg(name, parts.join(QStringLiteral(", ")));
                if (o.started > 0 || o.leftStopped > 0) {
                    QStringList state;
                    if (o.started > 0)
                        state.append(tr("%1 started").arg(o.started));
                    if (o.leftStopped > 0)
                        state.append(tr("%1 left stopped").arg(o.leftStopped));
                    text += QLatin1Char(' ') +
                            tr("After the check: %1.").arg(state.join(QStringLiteral(", ")));
                }
                if (!o.refused.isEmpty())
                    text += QLatin1Char(' ') +
                            tr("Kept stopped, its files not renamed: a file of the new name is "
                               "already on disk (%1).")
                                .arg(o.refused.join(QStringLiteral(", ")));
                if (o.keptManual > 0)
                    text += QLatin1Char(' ') +
                            count(o.keptManual,
                                  "1 torrent kept its folder by hand: automatic management "
                                  "would move it here.",
                                  "%1 torrents kept their folders by hand: automatic "
                                  "management would move them here.");
                if (o.cancelled || !o.complete) {
                    const int done = o.added + o.magnets + o.finished;
                    text += QLatin1Char(' ') +
                            (o.cancelled ? tr("Stopped after %1 of %2.")
                                         : tr("It stopped after %1 of %2."))
                                .arg(done)
                                .arg(total) +
                            QLatin1Char(' ') +
                            tr("Import the same file again to finish: what is here is kept.");
                }
                if (!o.errors.isEmpty())
                    text += QLatin1Char(' ') + (o.errors.size() == 1
                                                    ? tr("Not done: %1.").arg(o.errors.constFirst())
                                                    : tr("Not done: %1, and %2 more.")
                                                          .arg(o.errors.constFirst())
                                                          .arg(o.errors.size() - 1));
                qInfo().noquote() << QStringLiteral("torrents: %1").arg(text);
                m_torrentsFile.reset();
                Q_EMIT torrentsImportFinished(text, o.complete && o.errors.isEmpty());
            });
    import->start(*m_torrentsFile, m_torrentsPlan, options);
}

void App::cancelTorrentsBackup()
{
    if (m_torrentsExport)
        m_torrentsExport->cancel();
    else if (m_torrentsImport)
        m_torrentsImport->cancel();
}

} // namespace omaseed::app
