// App's #33 block (docs/24 §4, §5, §7): a qBittorrent's torrents exported to a file and
// imported back, from Settings › qBittorrent › Backup. Kept out of App.cpp so it reads as one
// piece; the declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/Format.hpp"
#include "core/LibraryCrossing.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/Version.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
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

/// An engine a file or a server names, one this build doesn't know read as qBittorrent
/// (docs/DECISIONS.md C40 X1).
EngineKind knownEngine(EngineKind kind)
{
    return kind == EngineKind::Unknown ? EngineKind::Qbittorrent : kind;
}

/// The saved server a torrents file was exported from (its manifest's address), when it is
/// one omaseed knows other than `self`.
std::optional<Server>
savedSource(const QList<Server> &servers, const QString &self, const TorrentsManifest &manifest)
{
    const QString key = OmaseedBackup::addressKey(QUrl(manifest.serverUrl));
    for (const Server &server : servers) {
        if (server.id != self && OmaseedBackup::addressKey(server.url) == key)
            return server;
    }
    return std::nullopt;
}

/// "category", "tag" or "label".
QString nameKindText(LibraryCrossing::NameKind kind)
{
    switch (kind) {
    case LibraryCrossing::NameKind::Category:
        return tr("category");
    case LibraryCrossing::NameKind::Tag:
        return tr("tag");
    case LibraryCrossing::NameKind::Label:
        break;
    }
    return tr("label");
}

/// The names block of a crossing import (docs/57 §5, C40 X4): one row per thing a source
/// name becomes on `to`, `{name, kind, to, left}`.
QVariantList crossingNames(EngineKind from, EngineKind to, const QList<TorrentRecord> &records)
{
    QVariantList out;
    for (const LibraryCrossing::Name &name : LibraryCrossing::names(from, to, records)) {
        for (const LibraryCrossing::Outcome &outcome : name.outcomes) {
            QString target = outcome.name;
            // As TorrentsImport::groupingHere sends it: qBittorrent takes no comma in a tag.
            if (to == EngineKind::Qbittorrent && outcome.kind == LibraryCrossing::NameKind::Tag)
                target = target.replace(QLatin1Char(','), QLatin1Char(' ')).simplified();
            out.append(QVariantMap{
                {QStringLiteral("name"), name.name},
                {QStringLiteral("kind"), nameKindText(name.kind)},
                {QStringLiteral("to"),
                 outcome.kept ? QStringLiteral("%1 %2").arg(nameKindText(outcome.kind), target)
                              : tr("left behind")},
                {QStringLiteral("left"), !outcome.kept}});
        }
    }
    return out;
}

/// The "can't be kept" lines of a crossing import (C40 X5): one counted line per thing `to`
/// has no counterpart for. Empty when it keeps everything.
QStringList crossingLosses(EngineKind from, EngineKind to, const QList<TorrentRecord> &records)
{
    const QString engine = engineDisplayName(to);
    // `one` names the engine as %1; `many` the count as %1 and the engine as %2.
    const auto line = [&engine](int n, const char *one, const char *many) {
        return n == 1 ? tr(one).arg(engine) : tr(many).arg(n).arg(engine);
    };
    QStringList out;
    for (const LibraryCrossing::Loss &loss : LibraryCrossing::losses(from, to, records)) {
        const int n = loss.torrents;
        switch (loss.kind) {
        case LibraryCrossing::LossKind::SeedingTimeLimit:
            out.append(line(n,
                            "1 has a seeding-time limit: %1 has none",
                            "%1 have a seeding-time limit: %2 has none"));
            break;
        case LibraryCrossing::LossKind::IdleLimit:
            out.append(
                line(n, "1 has an idle limit: %1 has none", "%1 have an idle limit: %2 has none"));
            break;
        case LibraryCrossing::LossKind::ShareLimitAction:
            out.append(to == EngineKind::Deluge
                           ? line(n,
                                  "1 does something other than stop at its limit: %1 stops or "
                                  "removes",
                                  "%1 do something other than stop at their limit: %2 stops or "
                                  "removes")
                           : line(n,
                                  "1 does something other than stop at its limit: %1 only stops",
                                  "%1 do something other than stop at their limit: %2 only "
                                  "stops"));
            break;
        case LibraryCrossing::LossKind::AutoManagement:
            out.append(count(n,
                             "1 is managed automatically by its category's folder: it keeps the "
                             "folder it is in",
                             "%1 are managed automatically by their category's folder: they keep "
                             "the folder they are in"));
            break;
        case LibraryCrossing::LossKind::IncompleteFolder:
            out.append(line(n,
                            "1 has an incomplete folder of its own: %1 has none",
                            "%1 have an incomplete folder of their own: %2 has none"));
            break;
        case LibraryCrossing::LossKind::GroupingNames:
            out.append(line(n,
                            "1 has tags or labels %1 can't keep: it keeps one label",
                            "%1 have tags or labels %2 can't keep: it keeps one label"));
            break;
        }
    }
    return out;
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

/// "torrents-public-2026-10-16.omaseed-torrents": what the export proposes for `server`.
QString exportName(const QString &server)
{
    return QStringLiteral("torrents-") + stem(server) + QLatin1Char('-') +
           QDate::currentDate().toString(Qt::ISODate) + TorrentsBackup::kSuffix;
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
    // docs/34 D2 a: the file it would be, with the server in its name.
    text += QLatin1Char(' ') + tr("Saved as %1.").arg(exportName(m_server.name));
    return text;
}

QString App::pickTorrentsExportPath() const
{
    const QString suggested = QDir::homePath() + QLatin1Char('/') + exportName(m_server.name);
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
    // C9's file holds qBittorrent's rows: a qBittorrent surface (docs/36 §4.4).
    if (!m_features.torrentsBackup || qbtEngine() == nullptr) {
        Q_EMIT torrentsExportFinished(
            tr("Not exported: %1 cannot export its torrents.").arg(m_server.name), false);
        return;
    }
    auto *exporter = new TorrentsExport(qbtEngine(), this);
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
        [this, exporter, name = m_server.name](const TorrentsExport::Outcome &outcome) {
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
            // docs/34 D2 a: the result names the server.
            QString text = outcome.torrents == 1
                               ? tr("Exported %1's 1 torrent to %2.").arg(name, tilde(outcome.path))
                               : tr("Exported %1's %2 torrents to %3.")
                                     .arg(name)
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
    m_client->torrents()->then(this, [read, settle](const Result<QList<TorrentPatch>> &r) {
        if (r.isError())
            read->error = r.error().toString();
        for (const TorrentPatch &t : r.value()) {
            for (const QString &id : {t.hash, t.values.infohashV1, t.values.infohashV2}) {
                if (!id.isEmpty())
                    read->here.ids.insert(id.toLower());
            }
        }
        settle();
    });
    m_client->categories()->then(this, [read, settle](const Result<QList<CategoryPatch>> &r) {
        if (r.isError())
            read->error = r.error().toString();
        for (const CategoryPatch &category : r.value())
            read->here.categories.append(category.name);
        settle();
    });
    m_client->tags()->then(this, [read, settle](const Result<QStringList> &r) {
        if (r.isError())
            read->error = r.error().toString();
        read->here.tags.append(r.value());
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
    if (const auto saved = savedSource(m_servers->servers(), m_server.id, file.manifest)) {
        PathMap map;
        for (const PathMapping &m : saved->pathMap)
            map.addMapping(m.remote, m.local);
        source = map;
        sourceName = saved->name;
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
    // docs/34 D2 a: whose torrents these are, from the file's manifest, and when.
    const QString fileName = QFileInfo(file.path).fileName();
    const QString from = file.manifest.serverName;
    // docs/57 §5 (DECISIONS C40 X4, X5): a file another engine wrote says so after its name,
    // and the sheet gains what its names become here and what this server can't keep.
    const EngineKind fileEngine = knownEngine(file.manifest.engine);
    const EngineKind hereEngine = knownEngine(m_server.engine);
    const bool crossing = fileEngine != hereEngine;
    const QString engineName = engineDisplayName(fileEngine);
    const QString when = QLocale::c().toString(file.manifest.created.toLocalTime(),
                                               QStringLiteral("d MMM yyyy HH:mm"));
    const bool plain = from.isEmpty() || !file.manifest.created.isValid();
    QString fromText;
    if (crossing) {
        const QString engineText = file.manifest.version.isEmpty()
                                       ? engineName
                                       : QStringLiteral("%1 %2").arg(engineName, version);
        fromText =
            plain ? tr("From %1 · %2").arg(fileName, engineText)
                  : tr("From %1's export of %2, %3 · %4").arg(from, when, fileName, engineText);
    } else {
        fromText = plain ? tr("From %1 (qBittorrent %2).").arg(fileName, version)
                         : tr("From %1's export of %2 (qBittorrent %3), %4.")
                               .arg(from, when, version, fileName);
    }
    QVariantList names;
    QStringList losses;
    if (crossing) {
        names = crossingNames(fileEngine, hereEngine, file.records);
        losses = crossingLosses(fileEngine, hereEngine, file.records);
    }
    const QString hereName = engineDisplayName(hereEngine);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("engine"), engineName},
            {QStringLiteral("crossing"), crossing},
            {QStringLiteral("namesTitle"),
             hereEngine == EngineKind::Qbittorrent ? tr("categories and tags") : tr("labels")},
            {QStringLiteral("names"), names},
            {QStringLiteral("lostTitle"), tr("can't be kept on %1").arg(hereName)},
            {QStringLiteral("losses"), losses},
            {QStringLiteral("kept"),
             crossing && losses.isEmpty()
                 ? tr("Everything in the file has a place on %1.").arg(hereName)
                 : QString()},
            {QStringLiteral("title"),
             n == 1 ? tr("Import 1 torrent into %1").arg(name)
                    : tr("Import %1 torrents into %2").arg(n).arg(name)},
            {QStringLiteral("sub"), fromText},
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
    if (!m_features.torrentsBackup || qbtEngine() == nullptr) {
        Q_EMIT torrentsImportFinished(tr("%1 cannot import torrents.").arg(m_server.name), false);
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
    auto *import = new TorrentsImport(qbtEngine(), this);
    m_torrentsImport = import;
    const QString name = m_server.name;
    const int total = m_torrentsPlan.count();
    // docs/DECISIONS.md C40 X8: the server the file came from, when omaseed has it saved. The
    // import never talks to it; the result only says the torrents are still there.
    const auto saved = savedSource(m_servers->servers(), m_server.id, m_torrentsFile->manifest);
    const QString old = saved ? saved->name : QString();
    setTorrentsRunning(QStringLiteral("import"), tr("importing into %1…").arg(name));
    connect(
        import, &TorrentsImport::progress, this, [this](const QString &phase, int done, int of) {
            setTorrentsRunning(QStringLiteral("import"),
                               phase == QLatin1String("checking")
                                   ? tr("checking files: %1 of %2…").arg(done).arg(of)
                                   : tr("adding %1 of %2…").arg(done).arg(of));
        });
    connect(
        import,
        &TorrentsImport::finished,
        this,
        [this, import, name, total, old](const TorrentsImport::Outcome &o) {
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
                        tr("Kept stopped, its files not renamed: the rename would replace a file "
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
                text +=
                    QLatin1Char(' ') +
                    (o.cancelled ? tr("Stopped after %1 of %2.") : tr("It stopped after %1 of %2."))
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
            if (!old.isEmpty() && !parts.isEmpty())
                text += QLatin1Char(' ') + tr("They are still on %1. Unfinished ones stay stopped "
                                              "here until you stop or remove them there.")
                                               .arg(old);
            qInfo().noquote() << QStringLiteral("torrents: %1").arg(text);
            m_torrentsFile.reset();
            Q_EMIT torrentsImportFinished(text,
                                          o.complete && o.errors.isEmpty() && o.refused.isEmpty());
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
