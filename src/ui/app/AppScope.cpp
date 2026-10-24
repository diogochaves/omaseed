// App's #167 block (board 46 `titleBar()`, DECISIONS C24 H5): the title bar's sentence for the
// scope the rail picked. core/ScopeSentence has the words; this picks which sentence and reads
// it again when the library, the rail's filters or a folder's move change. The declarations
// sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/FolderModel.hpp"
#include "core/ScopeSentence.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentFilterModel.hpp"

namespace omaseed::app {

using core::ScopeSentence;
using core::TorrentFilterModel;

void App::setUpScope()
{
    // Every poll (FilterCounts recounts after the model, and at local midnight), every pick in
    // the rail, a category's save path, and a folder's figures or its move.
    connect(m_engine->counts(), &core::FilterCounts::changed, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::questionChanged, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::categoryChanged, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::tagChanged, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::trackerChanged, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::folderChanged, this, &App::updateScopeSentence);
    connect(m_filtered, &TorrentFilterModel::onlyHashesChanged, this, &App::updateScopeSentence);
    connect(
        m_engine->categories(), &QAbstractItemModel::modelReset, this, &App::updateScopeSentence);
    connect(
        m_engine->categories(), &QAbstractItemModel::dataChanged, this, &App::updateScopeSentence);
    connect(m_move, &MoveController::changed, this, &App::updateScopeSentence);
    connect(
        m_move->folders(), &core::FolderModel::revisionChanged, this, &App::updateScopeSentence);
    updateScopeSentence();
}

void App::updateScopeSentence()
{
    const int question = m_filtered->question();
    QString line;
    if (!m_filtered->onlyHashes().isEmpty()) {
        // A move's "show": the window words it, as it knows whether it came from the sheet.
    } else if (!m_filtered->folder().isEmpty()) {
        line = m_move->folderLine(m_filtered->folder());
    } else {
        const core::ScopeFacts facts = m_filtered->scopeFacts();
        const QVariant category = m_filtered->category();
        if (category.isValid()) {
            const QString name = category.toString();
            line = ScopeSentence::narrowed(
                question,
                ScopeSentence::category(
                    facts, name.isEmpty() ? QString() : m_engine->categories()->savePathOf(name)));
        } else if (m_filtered->tag().isValid()) {
            line = ScopeSentence::narrowed(question, ScopeSentence::tag(facts));
        } else if (m_filtered->tracker().isValid()) {
            line = ScopeSentence::narrowed(question, ScopeSentence::tracker(facts));
        } else {
            line = ScopeSentence::question(question, facts);
        }
    }
    if (line == m_scopeSentence)
        return;
    m_scopeSentence = line;
    Q_EMIT scopeSentenceChanged();
}

QString App::lostSentence(const QString &listTime, const QString &headline) const
{
    return ScopeSentence::lost(listTime, headline);
}

QString App::logSentence(const QString &server, int lines, const QString &since) const
{
    return ScopeSentence::log(server, lines, since);
}

QString App::seedSentence(const QString &upSpeed, const QString &today, const QString &scope) const
{
    return ScopeSentence::seed(upSpeed, today, scope);
}

QString App::moveJobSentence(int torrents, int withServer) const
{
    return ScopeSentence::moveJob(torrents, withServer);
}

QString App::moveSheetSentence(int torrents) const
{
    return ScopeSentence::moveSheet(torrents);
}

} // namespace omaseed::app
