#include "core/ServerFolder.hpp"

#include <QJsonValue>

namespace omaseed::core {

ServerFolder ServerFolder::fromReply(const QbtResult<QJsonArray> &result)
{
    ServerFolder folder;
    if (!result.isError()) {
        folder.state = State::Listed;
        for (const QJsonValue &entry : result.value()) {
            QString path = entry.toString();
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
            while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
                path.chop(1);
            const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
            if (!name.isEmpty())
                folder.names.insert(name);
        }
        return folder;
    }
    const ApiError &error = result.error();
    const QString message = error.message();
    if (error.httpStatus() == 404 && message.contains(QLatin1String("Directory does not exist")))
        folder.state = State::Missing;
    else if (error.httpStatus() == 400 && message.contains(QLatin1String("Invalid directory path")))
        folder.state = State::Missing;
    else if (error.httpStatus() == 404 || error.httpStatus() == 405)
        folder.state = State::Unsupported;
    else
        folder.state = State::Failed;
    return folder;
}

} // namespace omaseed::core
