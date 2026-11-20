#include "core/TempFolder.hpp"

namespace omaseed::core::TempFolders {

TempFolder fromPreferences(const QVariantMap &prefs, EngineKind engine)
{
    TempFolder folder;
    switch (engine) {
    case EngineKind::Qbittorrent:
    case EngineKind::Transmission:
        folder.on = prefs.value(QStringLiteral("temp_path_enabled")).toBool();
        folder.path = prefs.value(QStringLiteral("temp_path")).toString();
        folder.perTorrent = engine == EngineKind::Qbittorrent;
        break;
    case EngineKind::Deluge:
        // #271 (4): downloads go to download_location and end in move_completed_path.
        folder.on = prefs.value(QStringLiteral("move_completed")).toBool();
        folder.path = prefs.value(QStringLiteral("save_path")).toString();
        folder.perTorrent = true;
        break;
    case EngineKind::Unknown:
        return folder;
    }
    if (folder.path.isEmpty())
        folder.on = false;
    return folder;
}

QString defaultSavePath(const QVariantMap &prefs, EngineKind engine)
{
    if (engine == EngineKind::Deluge && fromPreferences(prefs, engine).on) {
        const QString end = prefs.value(QStringLiteral("move_completed_path")).toString();
        if (!end.isEmpty())
            return end;
    }
    return prefs.value(QStringLiteral("save_path")).toString();
}

bool preallocates(const QVariantMap &prefs, EngineKind engine)
{
    switch (engine) {
    case EngineKind::Qbittorrent:
        return prefs.value(QStringLiteral("preallocate_all")).toBool();
    case EngineKind::Deluge:
        return prefs.value(QStringLiteral("pre_allocate_storage")).toBool();
    case EngineKind::Transmission:
    case EngineKind::Unknown:
        return false;
    }
    return false;
}

} // namespace omaseed::core::TempFolders
