#pragma once

// What a server said about itself when probed: its engine's version and the version of the
// API it speaks (docs/36 §4.3). EngineFeatures are worked out from these.

#include <QString>

namespace omaseed::core {

struct ServerInfo
{
    QString version;    ///< the engine's own, e.g. "v5.2.3"
    QString apiVersion; ///< the API's, e.g. "2.15.1"

    bool operator==(const ServerInfo &other) const = default;
};

} // namespace omaseed::core
