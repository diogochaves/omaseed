# omaseed-core: pure Qt (Core + Network). No QML, no GUI, fully unit-tested.
configure_file(BuildConfig.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/omaseed/core/BuildConfig.hpp" @ONLY)

qt_add_library(omaseed-core STATIC
    ApiError.cpp
    ApiError.hpp
    Bencode.cpp
    Bencode.hpp
    Format.cpp
    Format.hpp
    MagnetLink.cpp
    MagnetLink.hpp
    PathMap.cpp
    PathMap.hpp
    Paths.cpp
    Paths.hpp
    QbtClient.cpp
    QbtClient.hpp
    QbtReply.cpp
    QbtReply.hpp
    SecretStore.cpp
    SecretStore.hpp
    Server.cpp
    Server.hpp
    ServerStore.cpp
    ServerStore.hpp
    TorrentFile.cpp
    TorrentFile.hpp
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
