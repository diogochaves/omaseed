// App's #168 block (board 46 H3–H4, DECISIONS C24): which display shows the library in the
// rail's scope, remembered per scope (core/LibraryDisplays) in state.json's `displays`, and the
// grouping of `filtered` that display wants: the rows under the five questions, the overview's
// shelves under the categories, the table none. The declarations sit at the end of App.hpp.
#include "ui/app/App.hpp"

#include "core/LibraryDisplays.hpp"
#include "core/StateFile.hpp"
#include "core/TorrentFilterModel.hpp"

#include <QFile>

namespace omaseed::app {

using core::LibraryDisplays;
using core::TorrentFilterModel;

namespace {

const QString kDisplaysKey = QStringLiteral("displays");

} // namespace

void App::setUpLibraryDisplays()
{
    // H4: rows for a new install, the table for one made before this build (#168 assumed 3).
    // Decided from state.json as this run finds it, and written with the first thing this run
    // writes there (writeStateSection()), so it never flips. An App that writes nothing (a
    // demo, a test) leaves no state.json behind (APP-16).
    const QJsonObject saved = stateSection(kDisplaysKey);
    if (saved.isEmpty()) {
        const bool exists = QFile::exists(core::StateFile::path());
        const bool existing = LibraryDisplays::isExistingInstall(
            exists ? core::StateFile::read() : QJsonObject(), exists);
        m_displays = LibraryDisplays(LibraryDisplays::initialLibrary(existing));
        m_displaysUnsaved = true;
    } else {
        m_displays = LibraryDisplays::fromJson(saved, QStringLiteral("table"));
    }
    // The walks pick their display explicitly (scripts/dev): every scope starts on it.
    const QString forced = qEnvironmentVariable("OMASEED_DISPLAY");
    if (LibraryDisplays::isName(forced)) {
        m_displays = LibraryDisplays(forced);
        m_displaysUnsaved = true;
    }

    connect(m_filtered, &TorrentFilterModel::questionChanged, this, &App::updateDisplay);
    connect(m_filtered, &TorrentFilterModel::categoryChanged, this, &App::updateDisplay);
    connect(m_filtered, &TorrentFilterModel::tagChanged, this, &App::updateDisplay);
    connect(m_filtered, &TorrentFilterModel::trackerChanged, this, &App::updateDisplay);
    connect(m_filtered, &TorrentFilterModel::folderChanged, this, &App::updateDisplay);
    connect(m_filtered, &TorrentFilterModel::onlyHashesChanged, this, &App::updateDisplay);
    updateDisplay();
}

void App::updateDisplay()
{
    const QString scope = LibraryDisplays::scopeKey(m_filtered->question(),
                                                    m_filtered->category(),
                                                    m_filtered->tag(),
                                                    m_filtered->tracker(),
                                                    m_filtered->folder());
    // #168 assumed 5: a move's torrents on show are its step column's list.
    const QString display =
        m_filtered->onlyHashes().isEmpty() ? m_displays.displayFor(scope) : QStringLiteral("table");
    m_filtered->setGroupBy(display == QLatin1String("rows")       ? TorrentFilterModel::ByQuestion
                           : display == QLatin1String("overview") ? TorrentFilterModel::ByCategory
                                                                  : TorrentFilterModel::NoGroups);
    if (scope == m_displayScope && display == m_display)
        return;
    m_displayScope = scope;
    m_display = display;
    Q_EMIT displayChanged();
}

void App::setDisplay(const QString &display)
{
    if (!m_filtered->onlyHashes().isEmpty() || !m_displays.set(m_displayScope, display))
        return;
    saveDisplays();
    updateDisplay();
}

QString App::displayFor(const QString &scopeKey) const
{
    return m_displays.displayFor(scopeKey);
}

void App::resetDisplays(bool existingInstall)
{
    m_displays = LibraryDisplays(LibraryDisplays::initialLibrary(existingInstall));
    saveDisplays();
    updateDisplay();
}

void App::saveDisplays()
{
    m_displaysUnsaved = false;
    writeStateSection(kDisplaysKey, m_displays.toJson());
}

void App::saveDisplaysFirst()
{
    if (m_displaysUnsaved && !m_demoMode)
        saveDisplays();
}

} // namespace omaseed::app
