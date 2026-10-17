#pragma once

#include "core/ApiError.hpp"

#include <QObject>
#include <QPointer>

#include <utility>

namespace omaseed::core {

/// The value half of an omaseed API call: either a T or an ApiError, never both.
/// Errors are values here too (docs/ARCHITECTURE.md), so no call throws.
template<class T>
class Result
{
public:
    Result() = default;
    Result(T value) // NOLINT(google-explicit-constructor): reads better at call sites
        : m_value(std::move(value))
    {}
    Result(ApiError error) // NOLINT(google-explicit-constructor)
        : m_error(std::move(error))
    {}

    bool isError() const { return m_error.isError(); }
    bool isValid() const { return !m_error.isError(); }

    /// Only meaningful when isValid(); a failed call keeps a default-constructed value.
    const T &value() const { return m_value; }
    const ApiError &error() const { return m_error; }

private:
    T m_value = T();
    ApiError m_error;
};

/// Non-template base so the reply can carry signals. Engines hand out Reply<T>*
/// pointers that delete themselves right after finished() is emitted, so callers connect
/// immediately (or use Reply::then) and never own them.
class ReplyBase : public QObject
{
    Q_OBJECT

public:
    explicit ReplyBase(QObject *parent = nullptr)
        : QObject(parent)
    {}

    bool isFinished() const { return m_finished; }
    bool isError() const { return m_error.isError(); }
    const ApiError &error() const { return m_error; }

    /// Asks the client to drop this request. The reply still finishes, with
    /// ApiError::Kind::Cancelled.
    void abort();

Q_SIGNALS:
    /// Emitted exactly once; the reply is deleted right after.
    void finished();
    /// Internal: the engine listens for it to cancel the transfer.
    void abortRequested();

protected:
    void markFinished(const ApiError &error);

private:
    bool m_finished = false;
    ApiError m_error;
};

/// Typed reply. `reply->then(context, [](const Result<T> &r) { ... })` is the normal way
/// to read one; the callback runs only while `context` is alive.
template<class T>
class Reply : public ReplyBase
{
public:
    using ReplyBase::ReplyBase;

    const Result<T> &result() const { return m_result; }
    /// Shorthand for result().value().
    const T &value() const { return m_result.value(); }

    template<class Fn>
    Reply<T> *then(QObject *context, Fn &&callback)
    {
        QObject::connect(this,
                         &ReplyBase::finished,
                         context != nullptr ? context : this,
                         [this, callback = std::forward<Fn>(callback)]() { callback(m_result); });
        return this;
    }

    /// Called by the engine when the request settles. Deletes the reply.
    void complete(Result<T> result)
    {
        m_result = std::move(result);
        markFinished(m_result.error());
    }

private:
    Result<T> m_result;
};

/// A reply that settles when `source` does, its value passed through `convert` (an error
/// passes as it is). Aborting it aborts `source`. An engine uses it to hand out omaseed's
/// types for its transport's answers (docs/DECISIONS.md C14): `convert` takes the source's
/// value and returns a `U` or a `Result<U>`.
template<class U, class T, class Fn>
Reply<U> *mapReply(Reply<T> *source, QObject *parent, Fn &&convert)
{
    auto *mapped = new Reply<U>(parent);
    QPointer<Reply<T>> from(source);
    QObject::connect(mapped, &ReplyBase::abortRequested, mapped, [from]() {
        if (!from.isNull())
            from->abort();
    });
    source->then(mapped, [mapped, convert = std::forward<Fn>(convert)](const Result<T> &result) {
        if (result.isError())
            mapped->complete(Result<U>(result.error()));
        else
            mapped->complete(Result<U>(convert(result.value())));
    });
    return mapped;
}

} // namespace omaseed::core
