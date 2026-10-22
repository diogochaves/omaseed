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

Reply<ServerFolder> *EngineClient::folderContent(const QString &)
{
    auto *reply = new Reply<ServerFolder>(this);
    ServerFolder folder;
    folder.state = ServerFolder::State::Unsupported;
    QTimer::singleShot(
        0, reply, [reply, folder]() { reply->complete(Result<ServerFolder>(folder)); });
    return reply;
}

} // namespace omaseed::core
