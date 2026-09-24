#include "core/Bencode.hpp"

#include <QSet>

#include <algorithm>

namespace omaseed::core::bencode {

Value::Type Value::type() const
{
    return std::visit(
        [](auto &&arg) -> Type {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return Type::Invalid;
            else if constexpr (std::is_same_v<T, qint64>)
                return Type::Integer;
            else if constexpr (std::is_same_v<T, QByteArray>)
                return Type::String;
            else if constexpr (std::is_same_v<T, List>)
                return Type::List;
            else
                return Type::Dict;
        },
        m_data);
}

qint64 Value::toInteger(qint64 defaultValue) const
{
    if (const auto *value = std::get_if<qint64>(&m_data))
        return *value;
    return defaultValue;
}

QByteArray Value::toString(const QByteArray &defaultValue) const
{
    if (const auto *value = std::get_if<QByteArray>(&m_data))
        return *value;
    return defaultValue;
}

List Value::toList() const
{
    if (const auto *value = std::get_if<List>(&m_data))
        return *value;
    return {};
}

Dict Value::toDict() const
{
    if (const auto *value = std::get_if<Dict>(&m_data))
        return *value;
    return {};
}

Value Value::value(const QByteArray &key) const
{
    if (const auto *dict = std::get_if<Dict>(&m_data)) {
        for (const auto &entry : *dict) {
            if (entry.first == key)
                return entry.second;
        }
    }
    return {};
}

namespace {

constexpr int kMaxDepth = 64;

/// A single-pass recursive-descent bencode parser. Tracks the current byte offset itself
/// (rather than consuming the QByteArray) so it can report error offsets and record the
/// top-level entry spans decode() promises.
class Parser
{
public:
    explicit Parser(const QByteArray &data)
        : m_data(data)
    {}

    Value parse(SpanMap *topLevelSpans)
    {
        const Value root = parseValue(0);
        if (m_failed)
            return {};
        if (m_pos != m_data.size()) {
            fail(QStringLiteral("trailing data after top-level value"));
            return {};
        }
        if (topLevelSpans) {
            for (const auto &entry : m_topLevelSpans)
                topLevelSpans->insert(entry.first, entry.second);
        }
        return root;
    }

    bool failed() const { return m_failed; }
    QString errorMessage() const { return m_errorMessage; }
    qsizetype errorOffset() const { return m_errorOffset; }

private:
    Value parseValue(int depth)
    {
        if (m_failed)
            return {};
        if (depth > kMaxDepth) {
            fail(QStringLiteral("nesting too deep"));
            return {};
        }
        if (m_pos >= m_data.size()) {
            fail(QStringLiteral("unexpected end of data"));
            return {};
        }
        const char c = m_data.at(m_pos);
        if (c == 'i')
            return parseInteger();
        if (c == 'l')
            return parseList(depth);
        if (c == 'd')
            return parseDict(depth);
        if (c >= '0' && c <= '9')
            return parseString();
        fail(QStringLiteral("unexpected byte '%1'").arg(QChar::fromLatin1(c)));
        return {};
    }

    Value parseInteger()
    {
        m_pos++; // skip 'i'
        const qsizetype numStart = m_pos;
        bool negative = false;
        if (m_pos < m_data.size() && m_data.at(m_pos) == '-') {
            negative = true;
            m_pos++;
        }
        const qsizetype digitsStart = m_pos;
        while (m_pos < m_data.size() && isDigit(m_data.at(m_pos)))
            m_pos++;
        const qsizetype digitsLen = m_pos - digitsStart;
        if (digitsLen == 0) {
            fail(QStringLiteral("integer missing digits"));
            return {};
        }
        if (digitsLen > 1 && m_data.at(digitsStart) == '0') {
            fail(QStringLiteral("integer has a leading zero"));
            return {};
        }
        if (negative && digitsLen == 1 && m_data.at(digitsStart) == '0') {
            fail(QStringLiteral("negative zero is not allowed"));
            return {};
        }
        if (m_pos >= m_data.size() || m_data.at(m_pos) != 'e') {
            fail(QStringLiteral("integer not terminated"));
            return {};
        }
        const QByteArray numText = m_data.mid(numStart, m_pos - numStart);
        m_pos++; // skip 'e'
        bool ok = false;
        const qint64 value = numText.toLongLong(&ok);
        if (!ok) {
            fail(QStringLiteral("integer overflows 64 bits"));
            return {};
        }
        return Value(value);
    }

    Value parseString()
    {
        const qsizetype lenStart = m_pos;
        while (m_pos < m_data.size() && isDigit(m_data.at(m_pos)))
            m_pos++;
        const qsizetype lenDigits = m_pos - lenStart;
        if (lenDigits == 0) {
            fail(QStringLiteral("string missing a length"));
            return {};
        }
        if (lenDigits > 1 && m_data.at(lenStart) == '0') {
            fail(QStringLiteral("string length has a leading zero"));
            return {};
        }
        if (m_pos >= m_data.size() || m_data.at(m_pos) != ':') {
            fail(QStringLiteral("string missing ':'"));
            return {};
        }
        const QByteArray lenText = m_data.mid(lenStart, lenDigits);
        m_pos++; // skip ':'
        bool ok = false;
        const qint64 len = lenText.toLongLong(&ok);
        if (!ok || len < 0) {
            fail(QStringLiteral("invalid string length"));
            return {};
        }
        // Compare against the bytes left rather than computing m_pos + len: a 19-digit
        // length would overflow the addition and slip past the check.
        if (len > m_data.size() - m_pos) {
            fail(QStringLiteral("string runs past the end of data"));
            return {};
        }
        const QByteArray bytes = m_data.mid(m_pos, len);
        m_pos += len;
        return Value(bytes);
    }

    Value parseList(int depth)
    {
        m_pos++; // skip 'l'
        List items;
        while (true) {
            if (m_pos >= m_data.size()) {
                fail(QStringLiteral("list not terminated"));
                return {};
            }
            if (m_data.at(m_pos) == 'e') {
                m_pos++;
                break;
            }
            items.append(parseValue(depth + 1));
            if (m_failed)
                return {};
        }
        return Value(items);
    }

    Value parseDict(int depth)
    {
        m_pos++; // skip 'd'
        Dict entries;
        QSet<QByteArray> seenKeys;
        const bool topLevel = (depth == 0);
        while (true) {
            if (m_pos >= m_data.size()) {
                fail(QStringLiteral("dict not terminated"));
                return {};
            }
            if (m_data.at(m_pos) == 'e') {
                m_pos++;
                break;
            }
            if (!isDigit(m_data.at(m_pos))) {
                fail(QStringLiteral("dict key must be a string"));
                return {};
            }
            const qsizetype keyStart = m_pos;
            const Value key = parseString();
            if (m_failed)
                return {};
            // A repeated key makes the dict ambiguous: value() would return the first entry
            // while the top-level span (used to hash "info") would point at the last.
            if (seenKeys.contains(key.toString())) {
                m_pos = keyStart;
                fail(QStringLiteral("duplicate dict key"));
                return {};
            }
            seenKeys.insert(key.toString());
            const qsizetype valueStart = m_pos;
            const Value value = parseValue(depth + 1);
            if (m_failed)
                return {};
            const qsizetype valueEnd = m_pos;
            const QByteArray keyBytes = key.toString();
            entries.append({keyBytes, value});
            if (topLevel)
                m_topLevelSpans.append({keyBytes, {valueStart, valueEnd - valueStart}});
        }
        return Value(entries);
    }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    void fail(const QString &message)
    {
        if (m_failed)
            return;
        m_failed = true;
        m_errorMessage = message;
        m_errorOffset = m_pos;
    }

    const QByteArray &m_data;
    qsizetype m_pos = 0;
    bool m_failed = false;
    QString m_errorMessage;
    qsizetype m_errorOffset = -1;
    QList<QPair<QByteArray, Span>> m_topLevelSpans;
};

void encodeInto(const Value &value, QByteArray &out)
{
    switch (value.type()) {
    case Value::Type::Integer:
        out += 'i';
        out += QByteArray::number(value.toInteger());
        out += 'e';
        break;
    case Value::Type::String: {
        const QByteArray s = value.toString();
        out += QByteArray::number(static_cast<qint64>(s.size()));
        out += ':';
        out += s;
        break;
    }
    case Value::Type::List:
        out += 'l';
        for (const Value &item : value.toList())
            encodeInto(item, out);
        out += 'e';
        break;
    case Value::Type::Dict: {
        // Sort a list of indices by key rather than sorting `entries` in place: std::sort
        // swaps elements, and swapping a DictEntry move-assigns a Value, which GCC 16's
        // libstdc++ <variant> mis-analyses as reading uninitialised storage under -O2
        // (-Werror=maybe-uninitialized false positive; the variant holds only Qt's
        // implicitly-shared types plus qint64/monostate, so nothing is ever actually
        // uninitialised). Indexing instead never move-assigns a Value, so it sidesteps the
        // bug entirely rather than papering over it.
        const Dict entries = value.toDict();
        QList<qsizetype> order(entries.size());
        for (qsizetype i = 0; i < entries.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&entries](qsizetype a, qsizetype b) {
            return entries.at(a).first < entries.at(b).first;
        });
        out += 'd';
        for (qsizetype idx : order) {
            const auto &entry = entries.at(idx);
            out += QByteArray::number(static_cast<qint64>(entry.first.size()));
            out += ':';
            out += entry.first;
            encodeInto(entry.second, out);
        }
        out += 'e';
        break;
    }
    case Value::Type::Invalid:
        break;
    }
}

} // namespace

Value decode(const QByteArray &data, ParseError *error, SpanMap *topLevelSpans)
{
    Parser parser(data);
    const Value root = parser.parse(topLevelSpans);
    if (error) {
        error->hasError = parser.failed();
        error->message = parser.errorMessage();
        error->offset = parser.errorOffset();
    }
    if (parser.failed())
        return {};
    return root;
}

QByteArray encode(const Value &value)
{
    QByteArray out;
    encodeInto(value, out);
    return out;
}

} // namespace omaseed::core::bencode
