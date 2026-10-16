#pragma once

// TorrentPatch — some fields of one torrent, typed, with the set of the fields it carries
// (docs/36 §4.1). A full sync gives one per torrent with every field an engine knows; a
// partial one only what moved. Torrent::apply() merges it. A field the engine does not have
// stays absent, so the torrent keeps its default rather than a made-up zero.
//
// The values live in an ordinary Torrent; `fields` says which of them mean something.
// SyncEngine's pending values (M12) are patches too.

#include "core/Torrent.hpp"

namespace omaseed::core {

struct TorrentPatch
{
    QString hash;
    TorrentFields fields;
    Torrent values; ///< only the members named in `fields` are read

    TorrentPatch() = default;
    explicit TorrentPatch(QString id)
        : hash(std::move(id))
    {}

    bool has(TorrentField field) const { return fields.has(field); }
    bool isEmpty() const { return fields.isEmpty(); }

    /// Sets one field: `patch.set(&Torrent::seqDl, TorrentField::SeqDl, true)`.
    template<class T, class V>
    TorrentPatch &set(T Torrent::*member, TorrentField field, V &&value)
    {
        values.*member = std::forward<V>(value);
        fields.set(field);
        return *this;
    }

    /// The patch that gives `torrent`'s value of `field`, and nothing else.
    static TorrentPatch of(const Torrent &torrent, TorrentField field);
};

} // namespace omaseed::core
