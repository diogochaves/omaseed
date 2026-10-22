#pragma once

// QbtFingerprint — does qBittorrent answer at this address? (C25 K5) One unauthenticated
// GET api/v2/app/version: "v5.3.0" when the address lets omaseed in without a sign-in, a 403
// "Forbidden" when it wants one (engine matrix §1.1). Anything else is not a qBittorrent's
// Web UI. The signed-in probe that follows is QbtEngine's, as before E3.

#include "core/engines/EngineFingerprint.hpp"

#include <QUrl>

namespace omaseed::core::QbtFingerprint {

/// `address`/api/v2/app/version.
QUrl versionUrl(const QUrl &address);

struct Reading
{
    Fingerprint fingerprint = Fingerprint::NotItself;
    QString version; ///< "v5.3.0"; empty when it wants a sign-in first
};

Reading read(const HttpAnswer &answer);

} // namespace omaseed::core::QbtFingerprint
