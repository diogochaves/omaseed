#pragma once

// A small JSON Schema check for the tests of skin.schema.json (DECISIONS C37 U4): only the
// keywords SkinReader::schema() writes (type, enum, const, minimum, maximum, minLength,
// maxLength, pattern, properties, patternProperties, additionalProperties, propertyNames,
// required, anyOf, not, and `$ref` into `#/$defs/`). Enough to say whether a skin file is inside
// the schema, without a validator in CI's container.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cmath>

namespace omaseed::tests {

/// A schema `pattern` as PCRE2 reads it: ECMA's `\uXXXX` written as `\x{XXXX}`.
inline QRegularExpression schemaPattern(const QString &pattern)
{
    QString pcre = pattern;
    static const QRegularExpression escape(QStringLiteral("\\\\u([0-9a-fA-F]{4})"));
    pcre.replace(escape, QStringLiteral("\\x{\\1}"));
    return QRegularExpression(pcre);
}

inline bool schemaTypeIs(const QJsonValue &value, const QString &type)
{
    if (type == QLatin1String("object"))
        return value.isObject();
    if (type == QLatin1String("array"))
        return value.isArray();
    if (type == QLatin1String("string"))
        return value.isString();
    if (type == QLatin1String("boolean"))
        return value.isBool();
    if (type == QLatin1String("number"))
        return value.isDouble();
    if (type == QLatin1String("integer"))
        return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
    return false;
}

/// What makes `value` fall outside `schema`, one line per problem, each with its path; empty
/// when it is inside. `root` is the whole schema, for `$ref` (`schema` itself when empty).
inline QStringList schemaProblems(const QJsonValue &value,
                                  const QJsonObject &schema,
                                  const QString &path = QStringLiteral("$"),
                                  const QJsonObject &root = QJsonObject())
{
    const QJsonObject top = root.isEmpty() ? schema : root;
    QStringList problems;
    if (schema.contains(QLatin1String("$ref"))) {
        const QString ref = schema.value(QLatin1String("$ref")).toString();
        const QString prefix = QStringLiteral("#/$defs/");
        const QJsonObject def =
            top.value(QLatin1String("$defs")).toObject().value(ref.mid(prefix.size())).toObject();
        if (!ref.startsWith(prefix) || def.isEmpty())
            problems << path + QStringLiteral(": $ref to nothing: ") + ref;
        else
            problems << schemaProblems(value, def, path, top);
    }
    const auto fail = [&](const QString &why) { problems << path + QStringLiteral(": ") + why; };

    if (schema.contains(QLatin1String("type")) &&
        !schemaTypeIs(value, schema.value(QLatin1String("type")).toString()))
        fail(QStringLiteral("not ") + schema.value(QLatin1String("type")).toString());
    if (schema.contains(QLatin1String("const")) && value != schema.value(QLatin1String("const")))
        fail(QStringLiteral("not the const"));
    if (schema.contains(QLatin1String("enum")) &&
        !schema.value(QLatin1String("enum")).toArray().contains(value))
        fail(QStringLiteral("not in the enum"));
    if (value.isDouble()) {
        if (schema.contains(QLatin1String("minimum")) &&
            value.toDouble() < schema.value(QLatin1String("minimum")).toDouble())
            fail(QStringLiteral("below minimum"));
        if (schema.contains(QLatin1String("maximum")) &&
            value.toDouble() > schema.value(QLatin1String("maximum")).toDouble())
            fail(QStringLiteral("above maximum"));
    }
    if (value.isString()) {
        const QString text = value.toString();
        if (schema.contains(QLatin1String("minLength")) &&
            text.size() < schema.value(QLatin1String("minLength")).toInt())
            fail(QStringLiteral("too short"));
        if (schema.contains(QLatin1String("maxLength")) &&
            text.size() > schema.value(QLatin1String("maxLength")).toInt())
            fail(QStringLiteral("too long"));
        if (schema.contains(QLatin1String("pattern")) &&
            !schemaPattern(schema.value(QLatin1String("pattern")).toString())
                 .match(text)
                 .hasMatch())
            fail(QStringLiteral("doesn't match its pattern"));
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties = schema.value(QLatin1String("properties")).toObject();
        const QJsonObject patterns = schema.value(QLatin1String("patternProperties")).toObject();
        for (const QJsonValue &name : schema.value(QLatin1String("required")).toArray())
            if (!object.contains(name.toString()))
                fail(name.toString() + QStringLiteral(" is required"));
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString at = path + QLatin1Char('.') + it.key();
            if (schema.contains(QLatin1String("propertyNames")))
                problems << schemaProblems(QJsonValue(it.key()),
                                           schema.value(QLatin1String("propertyNames")).toObject(),
                                           at + QStringLiteral(" (name)"),
                                           top);
            bool matched = false;
            if (properties.contains(it.key())) {
                matched = true;
                problems << schemaProblems(
                    it.value(), properties.value(it.key()).toObject(), at, top);
            }
            for (auto p = patterns.constBegin(); p != patterns.constEnd(); ++p)
                if (schemaPattern(p.key()).match(it.key()).hasMatch()) {
                    matched = true;
                    problems << schemaProblems(it.value(), p.value().toObject(), at, top);
                }
            if (matched || !schema.contains(QLatin1String("additionalProperties")))
                continue;
            const QJsonValue additional = schema.value(QLatin1String("additionalProperties"));
            if (additional.isBool() && !additional.toBool())
                problems << at + QStringLiteral(": not a property the schema has");
            else if (additional.isObject())
                problems << schemaProblems(it.value(), additional.toObject(), at, top);
        }
    }
    if (schema.contains(QLatin1String("anyOf"))) {
        bool any = false;
        for (const QJsonValue &option : schema.value(QLatin1String("anyOf")).toArray())
            any = any || schemaProblems(value, option.toObject(), path, top).isEmpty();
        if (!any)
            fail(QStringLiteral("matches none of anyOf"));
    }
    if (schema.contains(QLatin1String("not")) &&
        schemaProblems(value, schema.value(QLatin1String("not")).toObject(), path, top).isEmpty())
        fail(QStringLiteral("matches its not"));
    return problems;
}

} // namespace omaseed::tests
