#pragma once

// EngineFingerprint — what one answer says about the engine behind an address (C25 K5, the
// engine matrix §1.1's "fingerprint without credentials"). Each engine reads its own answers
// in its directory (engines/qbt/QbtFingerprint, engines/transmission/…, engines/deluge/…);
// EngineRecognizer only sequences them.

#include <QByteArray>
#include <QList>
#include <QString>

namespace omaseed::core {

/// One HTTP answer, as an engine's reader sees it: no transport error got in the way.
struct HttpAnswer
{
    int status = 0;
    QByteArray body;
    QList<std::pair<QByteArray, QByteArray>> headers; ///< raw, as the server sent them

    /// The first header called `name`, any case; empty when there is none.
    QByteArray header(const QByteArray &name) const
    {
        for (const auto &[key, value] : headers) {
            if (key.compare(name, Qt::CaseInsensitive) == 0)
                return value;
        }
        return {};
    }
};

/// What an answer says.
enum class Fingerprint {
    NotItself, ///< it answered, but not as this engine
    Itself,    ///< this engine: carry on with its next step, or stop here
};

} // namespace omaseed::core
