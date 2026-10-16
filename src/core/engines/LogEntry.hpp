#pragma once

// LogEntry — one line of an engine's own log. Only qBittorrent has one (log/main), and the
// log face and the problem log are qBittorrent's surfaces (docs/DECISIONS.md C14 E7, C2,
// C3), so this keeps its shape; it is a typed value all the same, so the models behind
// those surfaces read no JSON.

#include <QString>
#include <QtGlobal>

namespace omaseed::core {

struct LogEntry
{
    int id = -1;          ///< rising; -1 when the line had none
    qint64 timestamp = 0; ///< seconds since the epoch
    int type = 1;         ///< qBittorrent's level: 1 normal, 2 info, 4 warning, 8 critical
    QString message;

    bool operator==(const LogEntry &other) const = default;
};

} // namespace omaseed::core
