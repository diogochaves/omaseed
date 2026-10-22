#pragma once

// EngineKind — which torrent engine a server runs (docs/36 §4, DECISIONS C14 E3, C25 K1–K9).
//
// A server stores its engine (servers.json v2, `Server::engine`) and the version it reported
// at the last contact. The first-run detector names the engine an open port or a process
// points to, and a server's test recognises it (EngineRecognizer). This build drives
// qBittorrent only: a Transmission or a Deluge is named and refused (K9) until E4 and E6
// bring their engines, and an engine below the version omaseed needs is refused in words
// (K8: Transmission 4.1, the first with JSON-RPC 2.0, RPC 6.0.0; Deluge 2.1).

#include <QMetaType>
#include <QString>

namespace omaseed::core {

enum class EngineKind {
    Unknown, ///< not known yet (a test still to recognise it), or nothing answered as one
    Qbittorrent,
    Transmission,
    Deluge,
};

/// "qbittorrent", "transmission", "deluge"; "" for Unknown. What servers.json and the status
/// file write.
QString engineKindToString(EngineKind kind);
/// The inverse; Unknown for anything else (`ok` false then, unless `text` is empty).
EngineKind engineKindFromString(const QString &text, bool *ok = nullptr);
/// The engine's own name, as people write it: "qBittorrent", "Transmission", "Deluge".
QString engineDisplayName(EngineKind kind);

/// The engines the first run and the test look for, in the order K2 lists them.
inline constexpr EngineKind kKnownEngines[] = {
    EngineKind::Qbittorrent,
    EngineKind::Transmission,
    EngineKind::Deluge,
};

/// Whether this build can drive `kind` (C25 K9). qBittorrent only, until E4 (Transmission)
/// and E6 (Deluge).
bool isEngineDriven(EngineKind kind);

/// What omaseed makes of an engine and the version it reported (K8). `refused` holds the
/// words for a version below the one omaseed needs; `notDriven` the words for an engine this
/// build doesn't drive yet (K9). A version omaseed can't read refuses nothing: it says so
/// only when the engine needs one to be judged.
struct EngineSupport
{
    bool driven = false;
    bool versionRefused = false;
    QString minimum; ///< "4.1", "2.1"; empty when the engine has no minimum
    /// One line, for the test's red line: "Transmission 3.00 is too old: omaseed needs 4.1
    /// or later", "omaseed can't drive Deluge yet". Empty when the server can be used.
    QString refusal;
    /// What to do about it, for the details under the line.
    QString advice;

    bool usable() const { return refusal.isEmpty(); }
};

/// `version` is the engine's own ("4.1.3", "2.1.1", "v5.2.3"); `apiVersion` its API's where
/// that decides (Transmission's RPC semver, "6.0.0"; empty when its answer had none).
EngineSupport engineSupport(EngineKind kind, const QString &version, const QString &apiVersion);

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::EngineKind)
