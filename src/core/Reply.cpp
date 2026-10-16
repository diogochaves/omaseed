#include "core/Reply.hpp"

namespace omaseed::core {

void ReplyBase::abort()
{
    if (m_finished)
        return;
    Q_EMIT abortRequested();
}

void ReplyBase::markFinished(const ApiError &error)
{
    if (m_finished)
        return;
    m_error = error;
    m_finished = true;
    Q_EMIT finished();
    deleteLater();
}

} // namespace omaseed::core
