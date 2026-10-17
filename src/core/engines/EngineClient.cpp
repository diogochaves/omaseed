#include "core/engines/EngineClient.hpp"

namespace omaseed::core {

EngineClient::~EngineClient() = default;

Reply<QList<LogEntry>> *EngineClient::log(int, bool)
{
    return unsupported<QList<LogEntry>>(QStringLiteral("the engine's log"));
}

Reply<FetchedMetadata> *EngineClient::fetchMetadata(const QString &)
{
    return unsupported<FetchedMetadata>(QStringLiteral("files before the add"));
}

} // namespace omaseed::core
