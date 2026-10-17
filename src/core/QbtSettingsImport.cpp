#include "core/QbtSettingsImport.hpp"

#include "core/ServerPreferences.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrlQuery>

#include <memory>

namespace omaseed::core {

namespace {

/// `a == b` the way the server's JSON means it: 10240 and 10240.0 are the same number.
bool sameJson(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isDouble() && b.isDouble())
        return a.toDouble() == b.toDouble();
    return a == b;
}

/// A path from any machine, absolute: `/data`, `C:/data`, `C:\data`, `\\nas\share`.
bool isAbsoluteAnywhere(const QString &path)
{
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:[\\\\/]"));
    return path.startsWith(QLatin1Char('/')) || path.startsWith(QStringLiteral("\\\\")) ||
           drive.match(path).hasMatch();
}

QString normalised(const QString &path)
{
    QString out = path;
    out.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (out.size() > 1 && out.endsWith(QLatin1Char('/')))
        out.chop(1);
    return out;
}

/// Every feed URL and folder path in an rss/items tree. Paths use qBittorrent's `\`.
void walkRss(const QJsonObject &folder,
             const QString &prefix,
             QSet<QString> *urls,
             QSet<QString> *folders,
             QList<QbtSettingsImport::Feed> *feeds = nullptr)
{
    for (auto it = folder.constBegin(); it != folder.constEnd(); ++it) {
        const QString path = prefix.isEmpty() ? it.key() : prefix + QLatin1Char('\\') + it.key();
        const QJsonObject item = it.value().toObject();
        if (item.contains(QLatin1String("url"))) {
            const QString url = item.value(QLatin1String("url")).toString();
            if (urls)
                urls->insert(url);
            if (feeds)
                feeds->append({path, url});
            continue;
        }
        if (folders)
            folders->insert(path);
        walkRss(item, path, urls, folders, feeds);
    }
}

/// A category's own share limits, set to something other than "the global setting".
bool hasOwnLimits(const QJsonObject &category)
{
    for (const char *key : {"ratio_limit", "seeding_time_limit", "inactive_seeding_time_limit"}) {
        const QJsonValue value = category.value(QLatin1String(key));
        if (value.isDouble() && value.toDouble() != -2)
            return true;
    }
    const QJsonValue action = category.value(QLatin1String("share_limit_action"));
    return action.isString() && action.toString() != QLatin1String("Default");
}

bool namesFolder(const QJsonValue &value)
{
    return value.isString() && !value.toString().isEmpty();
}

} // namespace

QString QbtSettingsImport::machineGroup(const QString &key)
{
    // docs/24 §6, and a few more for the same reasons (DECISIONS C8).
    static const QHash<QString, QString> named = {
        // the folder may not exist on the new machine
        {QStringLiteral("save_path"), QStringLiteral("folders")},
        {QStringLiteral("temp_path"), QStringLiteral("folders")},
        {QStringLiteral("export_dir"), QStringLiteral("folders")},
        {QStringLiteral("export_dir_fin"), QStringLiteral("folders")},
        {QStringLiteral("scan_dirs"), QStringLiteral("folders")},
        {QStringLiteral("file_log_path"), QStringLiteral("folders")},
        {QStringLiteral("ip_filter_path"), QStringLiteral("folders")},
        // a router forwards one port to one machine; an interface may not be there
        {QStringLiteral("listen_port"), QStringLiteral("network")},
        {QStringLiteral("random_port"), QStringLiteral("network")},
        {QStringLiteral("ssl_listen_port"), QStringLiteral("network")},
        {QStringLiteral("current_network_interface"), QStringLiteral("network")},
        {QStringLiteral("current_interface_address"), QStringLiteral("network")},
        {QStringLiteral("current_interface_name"), QStringLiteral("network")},
        {QStringLiteral("upnp"), QStringLiteral("network")},
        {QStringLiteral("announce_ip"), QStringLiteral("network")},
        {QStringLiteral("announce_port"), QStringLiteral("network")},
        {QStringLiteral("embedded_tracker_port"), QStringLiteral("network")},
        {QStringLiteral("embedded_tracker_port_forwarding"), QStringLiteral("network")},
        // omaseed may lose the very connection it imports through
        {QStringLiteral("web_ui_address"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_port"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_username"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_upnp"), QStringLiteral("webui")},
        {QStringLiteral("use_https"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_domain_list"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_reverse_proxy_enabled"), QStringLiteral("webui")},
        {QStringLiteral("web_ui_reverse_proxies_list"), QStringLiteral("webui")},
        {QStringLiteral("alternative_webui_enabled"), QStringLiteral("webui")},
        // a program from another machine runs nothing, or the wrong thing
        {QStringLiteral("autorun_program"), QStringLiteral("programs")},
        {QStringLiteral("autorun_on_torrent_added_program"), QStringLiteral("programs")},
        {QStringLiteral("python_executable_path"), QStringLiteral("programs")},
        {QStringLiteral("alternative_webui_path"), QStringLiteral("programs")},
    };
    const auto it = named.constFind(key);
    if (it != named.constEnd())
        return it.value();
    if (key.startsWith(QLatin1String("i2p_")))
        return QStringLiteral("network");
    if (key.startsWith(QLatin1String("web_ui_https_")) || key.startsWith(QLatin1String("bypass_")))
        return QStringLiteral("webui");
    return {};
}

int QbtSettingsImport::Diff::machineCount() const
{
    int n = 0;
    for (const PrefChange &change : changes) {
        if (!change.group.isEmpty())
            ++n;
    }
    return n;
}

QbtSettingsImport::Diff QbtSettingsImport::diff(const QbtSettingsSnapshot &file,
                                                const QbtSettingsSnapshot &here)
{
    Diff d;
    QList<PrefChange> safe;
    QList<PrefChange> machine;
    QStringList keys = file.preferences.keys();
    keys.sort();
    for (const QString &key : std::as_const(keys)) {
        if (QbtSettingsExport::isSecretKey(key))
            continue;
        if (!here.preferences.contains(key)) {
            d.skipped.append(key);
            continue;
        }
        const QJsonValue mine = here.preferences.value(key);
        const QJsonValue theirs = file.preferences.value(key);
        if (sameJson(mine, theirs)) {
            ++d.same;
            continue;
        }
        PrefChange change{key, mine, theirs, machineGroup(key)};
        (change.group.isEmpty() ? safe : machine).append(change);
    }
    d.changes = safe + machine;
    for (const QString &key : here.preferences.keys()) {
        if (QbtSettingsExport::isSecretKey(key))
            ++d.secretsHere;
    }

    d.categoriesHere = int(here.categories.size());
    for (auto it = file.categories.constBegin(); it != file.categories.constEnd(); ++it) {
        if (here.categories.contains(it.key()))
            continue;
        d.categories.append(it.key());
        const QJsonObject spec = it.value().toObject();
        if (hasOwnLimits(spec))
            ++d.categoryLimitsLeftOut;
        if (namesFolder(spec.value(QLatin1String("savePath"))) ||
            namesFolder(spec.value(QLatin1String("download_path"))))
            d.hasFolders = true;
    }

    d.tagsHere = int(here.tags.size());
    for (const QString &tag : file.tags) {
        if (!here.tags.contains(tag) && !d.tags.contains(tag))
            d.tags.append(tag);
    }

    QSet<QString> urlsHere;
    QSet<QString> foldersHere;
    walkRss(here.rssItems, {}, &urlsHere, &foldersHere);
    d.feedsHere = int(urlsHere.size());
    QList<Feed> fileFeeds;
    walkRss(file.rssItems, {}, nullptr, nullptr, &fileFeeds);
    for (const Feed &feed : std::as_const(fileFeeds)) {
        if (urlsHere.contains(feed.url))
            continue;
        urlsHere.insert(feed.url); // the same URL twice in the file is one feed
        d.feeds.append(feed);
        const QStringList parts = feed.path.split(QLatin1Char('\\'));
        QString folder;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            folder = folder.isEmpty() ? parts.at(i) : folder + QLatin1Char('\\') + parts.at(i);
            if (!foldersHere.contains(folder) && !d.folders.contains(folder))
                d.folders.append(folder);
        }
    }

    d.rulesHere = int(here.rssRules.size());
    for (auto it = file.rssRules.constBegin(); it != file.rssRules.constEnd(); ++it) {
        if (here.rssRules.contains(it.key()))
            continue;
        d.rules.append(it.key());
        const QJsonObject rule = it.value().toObject();
        const QJsonObject params = rule.value(QLatin1String("torrentParams")).toObject();
        if (namesFolder(rule.value(QLatin1String("savePath"))) ||
            namesFolder(params.value(QLatin1String("save_path"))) ||
            namesFolder(params.value(QLatin1String("download_path"))))
            d.hasFolders = true;
    }
    return d;
}

QString QbtSettingsImport::rebaseFolder(const QString &path,
                                        const QList<QPair<QString, QString>> &roots,
                                        bool keep)
{
    if (keep || path.isEmpty() || !isAbsoluteAnywhere(path))
        return path;
    const QString clean = normalised(path);
    for (const auto &[from, to] : roots) {
        if (from.isEmpty() || to.isEmpty())
            continue;
        const QString root = normalised(from);
        if (root == QLatin1String("/"))
            continue; // everything is under /: that says nothing about where it belongs
        if (clean == root)
            return normalised(to);
        if (clean.startsWith(root + QLatin1Char('/')))
            return normalised(to) + clean.mid(root.size());
    }
    return {};
}

QList<QPair<QString, QString>> QbtSettingsImport::folderRoots(const QbtSettingsSnapshot &file,
                                                              const QbtSettingsSnapshot &here,
                                                              const Plan &plan)
{
    const auto target = [&](const char *key) {
        const QString name = QLatin1String(key);
        const QJsonObject &from = plan.keys.contains(name) ? file.preferences : here.preferences;
        return from.value(name).toString();
    };
    return {{file.preferences.value(QLatin1String("save_path")).toString(), target("save_path")},
            {file.preferences.value(QLatin1String("temp_path")).toString(), target("temp_path")}};
}

QJsonObject QbtSettingsImport::preferenceChanges(const QbtSettingsSnapshot &file,
                                                 const QbtSettingsSnapshot &here,
                                                 const Plan &plan)
{
    QJsonObject out;
    for (const QString &key : plan.keys) {
        if (QbtSettingsExport::isSecretKey(key) || !file.preferences.contains(key) ||
            !here.preferences.contains(key))
            continue;
        const QJsonValue value = file.preferences.value(key);
        if (!sameJson(value, here.preferences.value(key)))
            out.insert(key, value);
    }
    return out;
}

QList<QPair<QString, QString>>
QbtSettingsImport::categoryForm(const QString &name,
                                const QJsonObject &spec,
                                const QList<QPair<QString, QString>> &roots,
                                bool keep)
{
    QList<QPair<QString, QString>> form{
        {QStringLiteral("category"), name},
        {QStringLiteral("savePath"),
         rebaseFolder(spec.value(QLatin1String("savePath")).toString(), roots, keep)}};
    const QJsonValue download = spec.value(QLatin1String("download_path"));
    if (download.isBool() && !download.toBool()) {
        form.append({QStringLiteral("downloadPathEnabled"), QStringLiteral("false")});
    } else if (namesFolder(download)) {
        const QString path = rebaseFolder(download.toString(), roots, keep);
        if (!path.isEmpty()) {
            form.append({QStringLiteral("downloadPathEnabled"), QStringLiteral("true")});
            form.append({QStringLiteral("downloadPath"), path});
        }
    }
    return form;
}

QJsonObject QbtSettingsImport::ruleDefinition(const QJsonObject &rule,
                                              const QList<QPair<QString, QString>> &roots,
                                              bool keep)
{
    QJsonObject out = QbtSettingsExport::withoutSecrets(rule);
    if (out.contains(QLatin1String("savePath")))
        out.insert(QStringLiteral("savePath"),
                   rebaseFolder(out.value(QLatin1String("savePath")).toString(), roots, keep));
    if (out.value(QLatin1String("torrentParams")).isObject()) {
        QJsonObject params = out.value(QLatin1String("torrentParams")).toObject();
        for (const char *key : {"save_path", "download_path"}) {
            const QString name = QLatin1String(key);
            if (params.value(name).isString())
                params.insert(name, rebaseFolder(params.value(name).toString(), roots, keep));
        }
        out.insert(QStringLiteral("torrentParams"), params);
    }
    return out;
}

QbtSettingsImport::QbtSettingsImport(QbtClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{}

void QbtSettingsImport::setPreferences(ServerPreferences *prefs)
{
    m_prefs = prefs;
}

void QbtSettingsImport::apply(const QbtSettingsSnapshot &file,
                              const QbtSettingsSnapshot &here,
                              const Plan &plan)
{
    if (m_busy)
        return;
    m_busy = true;
    m_file = file;
    m_here = here;
    m_plan = plan;
    m_diff = diff(file, here);
    m_outcome = Outcome();
    m_steps.clear();

    const QList<QPair<QString, QString>> roots = folderRoots(file, here, plan);
    const auto post = [this](const QString &path,
                             const QUrlQuery &form,
                             const QString &what,
                             std::function<void()> onOk) {
        if (m_client.isNull()) {
            m_outcome.errors.append(QStringLiteral("%1: no server").arg(what));
            next();
            return;
        }
        m_client->post(path, form)->then(this, [this, what, onOk](const Result<QByteArray> &r) {
            if (r.isError())
                m_outcome.errors.append(QStringLiteral("%1: %2").arg(what, r.error().toString()));
            else if (onOk)
                onOk();
            next();
        });
    };

    if (plan.categories) {
        for (const QString &name : std::as_const(m_diff.categories)) {
            const QJsonObject spec = file.categories.value(name).toObject();
            const QList<QPair<QString, QString>> fields =
                categoryForm(name, spec, roots, plan.folders);
            m_steps.append([this, post, name, fields] {
                Q_EMIT progress(tr("category %1").arg(name));
                QUrlQuery form;
                for (const auto &[key, value] : fields)
                    form.addQueryItem(key, QString::fromUtf8(QUrl::toPercentEncoding(value)));
                post(QStringLiteral("torrents/createCategory"),
                     form,
                     tr("category %1").arg(name),
                     [this] { ++m_outcome.categories; });
            });
        }
    }

    if (plan.tags && !m_diff.tags.isEmpty()) {
        m_steps.append([this] {
            Q_EMIT progress(tr("tags"));
            if (m_client.isNull()) {
                m_outcome.errors.append(tr("tags: no server"));
                return next();
            }
            m_client->torrentsCreateTags(m_diff.tags)
                ->then(this, [this](const Result<QByteArray> &r) {
                    if (r.isError())
                        m_outcome.errors.append(tr("tags: %1").arg(r.error().toString()));
                    else
                        m_outcome.tags = int(m_diff.tags.size());
                    next();
                });
        });
    }

    if (plan.rss) {
        for (const QString &folder : std::as_const(m_diff.folders)) {
            m_steps.append([this, folder] {
                Q_EMIT progress(tr("RSS folder %1").arg(folder));
                QUrlQuery form;
                form.addQueryItem(QStringLiteral("path"),
                                  QString::fromUtf8(QUrl::toPercentEncoding(folder)));
                // A folder that is already there answers 409: that is fine.
                m_client->post(QStringLiteral("rss/addFolder"), form)
                    ->then(this, [this, folder](const Result<QByteArray> &r) {
                        if (r.isError() && r.error().httpStatus() != 409)
                            m_outcome.errors.append(
                                tr("RSS folder %1: %2").arg(folder, r.error().toString()));
                        next();
                    });
            });
        }
        for (const Feed &feed : std::as_const(m_diff.feeds)) {
            m_steps.append([this, post, feed] {
                Q_EMIT progress(tr("RSS feed %1").arg(feed.url));
                QUrlQuery form;
                form.addQueryItem(QStringLiteral("url"),
                                  QString::fromUtf8(QUrl::toPercentEncoding(feed.url)));
                form.addQueryItem(QStringLiteral("path"),
                                  QString::fromUtf8(QUrl::toPercentEncoding(feed.path)));
                post(QStringLiteral("rss/addFeed"), form, tr("RSS feed %1").arg(feed.url), [this] {
                    ++m_outcome.feeds;
                });
            });
        }
        for (const QString &name : std::as_const(m_diff.rules)) {
            const QJsonObject def =
                ruleDefinition(file.rssRules.value(name).toObject(), roots, plan.folders);
            m_steps.append([this, post, name, def] {
                Q_EMIT progress(tr("RSS rule %1").arg(name));
                QUrlQuery form;
                form.addQueryItem(QStringLiteral("ruleName"),
                                  QString::fromUtf8(QUrl::toPercentEncoding(name)));
                form.addQueryItem(QStringLiteral("ruleDef"),
                                  QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(
                                      QJsonDocument(def).toJson(QJsonDocument::Compact)))));
                post(QStringLiteral("rss/setRule"), form, tr("RSS rule %1").arg(name), [this] {
                    ++m_outcome.rules;
                });
            });
        }
    }

    const QJsonObject changes = preferenceChanges(file, here, plan);
    if (!changes.isEmpty()) {
        m_steps.append([this, changes] {
            Q_EMIT progress(tr("preferences"));
            const QStringList sent = changes.keys();
            if (!m_prefs.isNull()) {
                // ServerPreferences shows the change at once and reads it back; App follows a
                // Web UI move on its `posted`. Its answer names the keys it was given.
                auto links = std::make_shared<QList<QMetaObject::Connection>>();
                const auto finish = [this, links](const QStringList &kept, const QString &error) {
                    for (const QMetaObject::Connection &link : std::as_const(*links))
                        disconnect(link);
                    links->clear();
                    if (!error.isEmpty()) {
                        m_outcome.errors.append(tr("preferences: %1").arg(error));
                        m_outcome.preferences = 0;
                    }
                    m_outcome.kept = kept;
                    next();
                };
                const QSet<QString> want(sent.cbegin(), sent.cend());
                links->append(
                    connect(m_prefs,
                            &ServerPreferences::applied,
                            this,
                            [want, finish](const QStringList &keys, const QStringList &kept) {
                                if (QSet<QString>(keys.cbegin(), keys.cend()) == want)
                                    finish(kept, {});
                            }));
                links->append(
                    connect(m_prefs,
                            &ServerPreferences::applyFailed,
                            this,
                            [want, finish](const QStringList &keys, const QString &error) {
                                if (QSet<QString>(keys.cbegin(), keys.cend()) == want)
                                    finish({}, error);
                            }));
                m_outcome.preferences = int(sent.size());
                m_prefs->apply(changes.toVariantMap());
                return;
            }
            if (m_client.isNull()) {
                m_outcome.errors.append(tr("preferences: no server"));
                return next();
            }
            m_client->appSetPreferences(changes)->then(
                this, [this, changes](const Result<QByteArray> &r) {
                    if (r.isError()) {
                        m_outcome.errors.append(tr("preferences: %1").arg(r.error().toString()));
                        return next();
                    }
                    m_outcome.preferences = int(changes.size());
                    // Read back: qBittorrent often ignores a value it does not like.
                    m_client->appPreferences()->then(
                        this, [this, changes](const Result<QJsonObject> &back) {
                            if (back.isValid()) {
                                for (auto it = changes.constBegin(); it != changes.constEnd();
                                     ++it) {
                                    const QJsonValue now = back.value().value(it.key());
                                    if (!now.isUndefined() && !sameJson(now, it.value()))
                                        m_outcome.kept.append(it.key());
                                }
                            }
                            next();
                        });
                });
        });
    }

    next();
}

void QbtSettingsImport::next()
{
    if (m_steps.isEmpty()) {
        done();
        return;
    }
    const std::function<void()> step = m_steps.takeFirst();
    step();
}

void QbtSettingsImport::done()
{
    if (!m_busy)
        return;
    m_busy = false;
    m_outcome.preferences = std::max(0, m_outcome.preferences - int(m_outcome.kept.size()));
    Q_EMIT finished(m_outcome);
}

} // namespace omaseed::core
