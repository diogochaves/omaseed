#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <variant>

namespace omaseed::core::bencode {

class Value;

/// A bencoded list: `l...e`.
using List = QList<Value>;

/// One `key -> value` entry of a bencoded dict.
using DictEntry = QPair<QByteArray, Value>;

/// A bencoded dict: `d...e`. Kept as an ordered list (not QMap) so decode() can preserve the
/// order found on disk; encode() always re-sorts by byte value before writing, per the spec.
using Dict = QList<DictEntry>;

/// A byte offset and length into the original buffer a Value was decoded from.
using Span = QPair<qsizetype, qsizetype>;

/// Byte spans of the top-level dict's entries, keyed by the entry's key. decode() fills this
/// so callers (TorrentFile) can hash a field such as "info" over the exact original bytes
/// instead of a re-encoding, which is required for the infohash to match other clients.
using SpanMap = QHash<QByteArray, Span>;

/// A decoded bencode value: an integer, a byte string, a list, or a dict. Immutable once built
/// (the parser only ever appends while building one), copies are cheap via Qt's implicit
/// sharing of QByteArray/QList.
class Value
{
public:
    enum class Type { Invalid, Integer, String, List, Dict };

    Value() = default;
    Value(qint64 value)
        : m_data(value)
    {}
    Value(QByteArray value)
        : m_data(std::move(value))
    {}
    Value(List value)
        : m_data(std::move(value))
    {}
    Value(Dict value)
        : m_data(std::move(value))
    {}

    Type type() const;
    bool isValid() const { return type() != Type::Invalid; }
    bool isInteger() const { return type() == Type::Integer; }
    bool isString() const { return type() == Type::String; }
    bool isList() const { return type() == Type::List; }
    bool isDict() const { return type() == Type::Dict; }

    qint64 toInteger(qint64 defaultValue = 0) const;
    QByteArray toString(const QByteArray &defaultValue = QByteArray()) const;
    List toList() const;
    Dict toDict() const;

    /// Looks up `key` when this value is a dict. Returns an invalid Value otherwise, including
    /// when the key is absent, so lookups can be chained without checking isDict() first.
    Value value(const QByteArray &key) const;

    bool operator==(const Value &other) const = default;

private:
    std::variant<std::monostate, qint64, QByteArray, List, Dict> m_data;
};

/// Reports why decode() failed and at what byte offset, so the caller can report a useful
/// message (e.g. "malformed .torrent file at byte 512: integer has leading zero").
struct ParseError
{
    bool hasError = false;
    QString message;
    qsizetype offset = -1;
};

/// Decodes one bencoded value from `data`. Strict: rejects leading zeros in integers and
/// string lengths, negative zero, unterminated values, trailing bytes after the top-level
/// value, and nesting deeper than 64 (lists/dicts containing themselves). On failure returns
/// an invalid Value and, when `error` is non-null, fills it in.
///
/// When `topLevelSpans` is non-null and the decoded value is a dict, it is filled with the
/// exact `[offset, length)` byte range of each of the dict's top-level entries in `data`, so
/// e.g. `topLevelSpans["info"]` gives the original bytes to hash for a torrent's infohash.
Value decode(const QByteArray &data, ParseError *error = nullptr, SpanMap *topLevelSpans = nullptr);

/// Encodes `value` back to bencode. Dict keys are always written in ascending byte order,
/// regardless of the order they were built or decoded in, which is what the spec requires and
/// what makes re-encoding idempotent.
QByteArray encode(const Value &value);

} // namespace omaseed::core::bencode
