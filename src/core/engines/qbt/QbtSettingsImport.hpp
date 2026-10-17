#pragma once

// QbtSettingsImport — a settings export laid over a qBittorrent (docs/24 §5 "Import settings",
// §6, §7; #32).
//
// `diff(file, here)` is what the sheet shows, and nothing changes until `apply`:
//   * Preferences: only the keys whose values differ. A key the target lacks (another
//     qBittorrent version) is skipped and counted; a key the file lacks stays as it is; a
//     secret is never compared or sent. Machine-specific keys (§6: folders, network, Web UI,
//     programs; `machineGroup`) are marked so the sheet starts them unticked.
//   * Categories, tags, RSS feeds and rules are only added, never changed or deleted
//     (docs/24 §5): a category, tag or rule by name, a feed by URL, so nothing hangs on the ids
//     either machine gave them (#34's lesson). A category that is already there keeps its own
//     folder and limits.
//   * A category's or an RSS rule's folder is machine-specific too (§6). Unless the sheet's
//     "folders" row is ticked, a folder under the file's default save path (or its incomplete
//     folder) moves under the target's, and any other absolute folder is left out, so the
//     target falls back to its own default (DECISIONS C8).
//   * Categories' share limits: qBittorrent 5.2's Web API reads them but takes none
//     (torrents/createCategory and editCategory ignore them), so they are left out and
//     counted.
//
// `apply(plan)` runs in this order: categories, tags, RSS folders, feeds and rules, then the
// preferences last, because a Web UI change among them can move the very connection the rest
// goes through. Each step's failure is kept and the rest still runs: what was added stays, and
// running it again skips it (§7). The preferences go through ServerPreferences when one is
// given, so the Settings panes show them at once and App follows a Web UI move.

#include "core/engines/qbt/QbtSettingsExport.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace omaseed::core {

class QbtClient;
class QbtEngine;
class ServerPreferences;

class QbtSettingsImport : public QObject
{
    Q_OBJECT

public:
    /// The sheet's pseudo-key for "category and RSS rule folders".
    static inline const QString kFoldersKey = QStringLiteral("@folders");

    /// docs/24 §6: "folders", "network", "webui" or "programs"; empty for a key safe to copy.
    static QString machineGroup(const QString &key);
    static bool isMachineSpecific(const QString &key) { return !machineGroup(key).isEmpty(); }

    struct PrefChange
    {
        QString key;
        QJsonValue here;
        QJsonValue file;
        QString group; ///< machineGroup(key)
    };
    struct Feed
    {
        QString path; ///< "folder\\name", qBittorrent's separator
        QString url;
    };
    struct Diff
    {
        QList<PrefChange> changes; ///< safe keys first, then machine-specific; each by key
        int same = 0;
        QStringList skipped;    ///< keys the target does not have
        int secretsHere = 0;    ///< secret keys the target has: they stay as they are
        QStringList categories; ///< names to add
        int categoriesHere = 0;
        int categoryLimitsLeftOut = 0; ///< new categories whose share limits cannot be set
        QStringList tags;              ///< to add
        int tagsHere = 0;
        QStringList folders; ///< RSS folders to create, parents first
        QList<Feed> feeds;   ///< to add
        int feedsHere = 0;
        QStringList rules; ///< names to add
        int rulesHere = 0;
        /// A new category or rule names a folder (the sheet's "folders" row).
        bool hasFolders = false;

        int machineCount() const;
    };
    static Diff diff(const QbtSettingsSnapshot &file, const QbtSettingsSnapshot &here);

    /// `path` from the file moved onto the target (DECISIONS C8): kept when `keep`, or when
    /// it is empty or relative; under one of `roots`' first members, the same place under its
    /// second; otherwise empty (the target's own default).
    static QString
    rebaseFolder(const QString &path, const QList<QPair<QString, QString>> &roots, bool keep);

    struct Plan
    {
        QStringList keys; ///< preferences to write
        bool categories = true;
        bool tags = true;
        bool rss = true;
        bool folders = false; ///< keep the file's category and rule folders as they are
    };
    struct Outcome
    {
        int preferences = 0; ///< keys written
        QStringList kept;    ///< keys the server left as they were
        int categories = 0;  ///< added
        int tags = 0;        ///< added
        int feeds = 0;       ///< added
        int rules = 0;       ///< added
        QStringList errors;  ///< one line per step that failed
        bool ok() const { return errors.isEmpty(); }
    };

    /// Talks to `engine`'s transport (docs/36 §4.4: a qBittorrent surface); null for none.
    explicit QbtSettingsImport(QbtEngine *engine, QObject *parent = nullptr);
    /// Preferences go through `prefs` (App's) instead of a POST of their own.
    void setPreferences(ServerPreferences *prefs);

    /// Applies `plan`'s part of `file` to the server `here` was read from. One at a time.
    void apply(const QbtSettingsSnapshot &file, const QbtSettingsSnapshot &here, const Plan &plan);
    bool isBusy() const { return m_busy; }

    /// What the apply sends, for the tests: the preferences, each new category's form, each
    /// new rule's definition, after the folders are moved.
    static QJsonObject preferenceChanges(const QbtSettingsSnapshot &file,
                                         const QbtSettingsSnapshot &here,
                                         const Plan &plan);
    static QList<QPair<QString, QString>>
    folderRoots(const QbtSettingsSnapshot &file, const QbtSettingsSnapshot &here, const Plan &plan);
    /// torrents/createCategory's form fields for category `name`: category, savePath, and
    /// downloadPathEnabled / downloadPath when the file says.
    static QList<QPair<QString, QString>> categoryForm(const QString &name,
                                                       const QJsonObject &spec,
                                                       const QList<QPair<QString, QString>> &roots,
                                                       bool keep);
    static QJsonObject
    ruleDefinition(const QJsonObject &rule, const QList<QPair<QString, QString>> &roots, bool keep);

Q_SIGNALS:
    void progress(const QString &what);
    void finished(const omaseed::core::QbtSettingsImport::Outcome &outcome);

private:
    void next();
    void done();

    QPointer<QbtClient> m_client;
    QPointer<ServerPreferences> m_prefs;
    QbtSettingsSnapshot m_file;
    QbtSettingsSnapshot m_here;
    Plan m_plan;
    Diff m_diff;
    Outcome m_outcome;
    QList<std::function<void()>> m_steps;
    bool m_busy = false;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::QbtSettingsImport::Outcome)
