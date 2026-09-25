# omaseed-core: pure Qt (Core + Network). No QML, no GUI, fully unit-tested.
configure_file(BuildConfig.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/omaseed/core/BuildConfig.hpp" @ONLY)

qt_add_library(omaseed-core STATIC
    AddFilesModel.cpp
    AddFilesModel.hpp
    ApiError.cpp
    ApiError.hpp
    Bencode.cpp
    Bencode.hpp
    CategoryModel.cpp
    CategoryModel.hpp
    FilterCounts.cpp
    FilterCounts.hpp
    Format.cpp
    Format.hpp
    GlobalState.cpp
    GlobalState.hpp
    HomeGuard.hpp
    MagnetLink.cpp
    MagnetLink.hpp
    PathMap.cpp
    PathMap.hpp
    PeerInput.cpp
    PeerInput.hpp
    Paths.cpp
    Paths.hpp
    PieceStates.cpp
    PieceStates.hpp
    QbtClient.cpp
    QbtClient.hpp
    QbtReply.cpp
    QbtReply.hpp
    SecretStore.cpp
    SecretStore.hpp
    Server.cpp
    Server.hpp
    ServerDetector.cpp
    ServerDetector.hpp
    ServerListModel.cpp
    ServerListModel.hpp
    ServerPreferences.cpp
    ServerPreferences.hpp
    ServerStore.cpp
    ServerStore.hpp
    StateFile.cpp
    StateFile.hpp
    SyncEngine.cpp
    SyncEngine.hpp
    TagModel.cpp
    TagModel.hpp
    Torrent.cpp
    Torrent.hpp
    TorrentDetails.cpp
    TorrentDetails.hpp
    TorrentFile.cpp
    TorrentFile.hpp
    TorrentFilesModel.cpp
    TorrentFilesModel.hpp
    TorrentFilterModel.cpp
    TorrentFilterModel.hpp
    TorrentModel.cpp
    TorrentModel.hpp
    TorrentPeersModel.cpp
    TorrentPeersModel.hpp
    TorrentTrackersModel.cpp
    TorrentTrackersModel.hpp
    TrackerModel.cpp
    TrackerModel.hpp
    Version.cpp
    Version.hpp
)
add_library(omaseed::core ALIAS omaseed-core)

target_include_directories(omaseed-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(omaseed-core
    PUBLIC Qt6::Core Qt6::Network
    PRIVATE omaseed::warnings Qt6Keychain::Qt6Keychain
)
