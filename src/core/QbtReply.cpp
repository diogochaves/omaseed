#include "core/QbtReply.hpp"

namespace omaseed::core {

void QbtReplyBase::abort()
{
    if (m_finished)
        return;
    Q_EMIT abortRequested();
}

void QbtReplyBase::markFinished(const ApiError &error)
{
    if (m_finished)
        return;
    m_error = error;
    m_finished = true;
    Q_EMIT finished();
    deleteLater();
}

} // namespace omaseed::core
