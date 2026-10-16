#pragma once

// TorrentField — one field of a Torrent, as a patch carries it and as Torrent::apply()
// reports it changed (docs/36 §4.1). TorrentModel maps each to the roles it feeds, so a
// change never names an engine's keys.

#include <QtGlobal>

#include <initializer_list>

namespace omaseed::core {

enum class TorrentField {
    Name,
    Size,
    TotalSize,
    Progress,
    Status,
    DlSpeed,
    UpSpeed,
    Eta,
    NumSeeds,
    NumComplete,
    NumLeechs,
    NumIncomplete,
    Ratio,
    Category,
    Tags,
    SavePath,
    ContentPath,
    DownloadPath,
    AddedOn,
    CompletionOn,
    LastActivity,
    Tracker,
    TrackersCount,
    HasTrackerError,
    QueuePosition,
    SeqDl,
    FirstLastPiecePrio,
    SuperSeeding,
    AutoTmm,
    IsPrivate,
    DlLimit,
    UpLimit,
    RatioLimit,
    SeedingTimeLimit,
    InactiveSeedingTimeLimit,
    ShareLimitAction,
    Availability,
    AmountLeft,
    Completed,
    Downloaded,
    Uploaded,
    UploadedSession,
    TimeActive,
    SeedingTime,
    InfohashV1,
    InfohashV2,
    MagnetUri,
    Count ///< not a field: how many there are
};

/// A set of TorrentFields (more than QFlags' 32 bits hold).
class TorrentFields
{
public:
    constexpr TorrentFields() = default;
    constexpr TorrentFields(std::initializer_list<TorrentField> fields)
    {
        for (const TorrentField field : fields)
            set(field);
    }

    static constexpr TorrentFields all()
    {
        TorrentFields fields;
        fields.m_bits = (quint64(1) << int(TorrentField::Count)) - 1;
        return fields;
    }

    constexpr void set(TorrentField field) { m_bits |= bit(field); }
    constexpr void clear(TorrentField field) { m_bits &= ~bit(field); }
    constexpr bool has(TorrentField field) const { return (m_bits & bit(field)) != 0; }
    constexpr bool isEmpty() const { return m_bits == 0; }
    constexpr int count() const
    {
        int n = 0;
        for (quint64 bits = m_bits; bits != 0; bits &= bits - 1)
            ++n;
        return n;
    }

    constexpr TorrentFields &operator|=(TorrentFields other)
    {
        m_bits |= other.m_bits;
        return *this;
    }
    constexpr bool operator==(const TorrentFields &other) const = default;

    /// Calls `fn(field)` for each field in the set, in TorrentField order.
    template<class Fn>
    void forEach(Fn &&fn) const
    {
        for (int i = 0; i < int(TorrentField::Count); ++i) {
            if ((m_bits & (quint64(1) << i)) != 0)
                fn(TorrentField(i));
        }
    }

private:
    static constexpr quint64 bit(TorrentField field) { return quint64(1) << int(field); }

    quint64 m_bits = 0;
};

static_assert(int(TorrentField::Count) <= 64, "TorrentFields holds 64 fields");

} // namespace omaseed::core
