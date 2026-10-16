#pragma once

// Per-torrent share limits in omaseed's terms (docs/36 §4.1): follow the server's default,
// no limit, or a value. Engines spell these with sentinels (qBittorrent: -2 and -1); the
// translators turn those into a mode at the edge, so nothing behind them compares magic
// numbers.
//
// code() and fromCode() are the Limits sheet's own encoding (-2 the server's default, -1
// none, else the value), which App's torrent rows and the QML speak; they are not an
// engine's wire format, even where they match one.

#include <QtGlobal>

namespace omaseed::core {

enum class LimitMode {
    Global,    ///< the server's default applies
    Unlimited, ///< no limit for this torrent
    Value,     ///< the torrent's own value
};

/// A ratio limit, e.g. 2.0.
struct RatioLimit
{
    LimitMode mode = LimitMode::Global;
    double ratio = 0.0; ///< only with LimitMode::Value

    static RatioLimit global() { return {}; }
    static RatioLimit unlimited() { return {LimitMode::Unlimited, 0.0}; }
    static RatioLimit of(double ratio) { return {LimitMode::Value, ratio}; }

    double code() const
    {
        return mode == LimitMode::Global ? -2.0 : mode == LimitMode::Unlimited ? -1.0 : ratio;
    }
    static RatioLimit fromCode(double code)
    {
        if (code <= -2.0)
            return global();
        if (code < 0.0)
            return unlimited();
        return of(code);
    }

    bool operator==(const RatioLimit &other) const = default;
};

/// A time limit in minutes (seeding time, or time without traffic).
struct MinutesLimit
{
    LimitMode mode = LimitMode::Global;
    qint64 minutes = 0; ///< only with LimitMode::Value

    static MinutesLimit global() { return {}; }
    static MinutesLimit unlimited() { return {LimitMode::Unlimited, 0}; }
    static MinutesLimit of(qint64 minutes) { return {LimitMode::Value, minutes}; }

    qint64 code() const
    {
        return mode == LimitMode::Global ? -2 : mode == LimitMode::Unlimited ? -1 : minutes;
    }
    static MinutesLimit fromCode(qint64 code)
    {
        if (code <= -2)
            return global();
        if (code < 0)
            return unlimited();
        return of(code);
    }

    bool operator==(const MinutesLimit &other) const = default;
};

} // namespace omaseed::core
