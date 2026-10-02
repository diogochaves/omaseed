#include "core/FilesViewState.hpp"

#include "core/StateFile.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace omaseed::core::FilesViewState {

namespace {

QString sectionKey()
{
    return QStringLiteral("files");
}

} // namespace

bool hideSkipped()
{
    return StateFile::readSection(sectionKey()).value(QStringLiteral("hideSkipped")).toBool(false);
}

void setHideSkipped(bool hide)
{
    QJsonObject section = StateFile::readSection(sectionKey());
    if (section.value(QStringLiteral("hideSkipped")).toBool(false) == hide)
        return;
    section.insert(QStringLiteral("hideSkipped"), hide);
    StateFile::writeSection(sectionKey(), section);
}

QString filterFor(const QString &hash)
{
    if (hash.isEmpty())
        return {};
    const QJsonArray filters =
        StateFile::readSection(sectionKey()).value(QStringLiteral("filters")).toArray();
    for (const QJsonValue &value : filters) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("hash")).toString() == hash)
            return entry.value(QStringLiteral("text")).toString();
    }
    return {};
}

void rememberFilter(const QString &hash, const QString &text)
{
    if (hash.isEmpty())
        return;
    QJsonObject section = StateFile::readSection(sectionKey());
    const QJsonArray before = section.value(QStringLiteral("filters")).toArray();

    QJsonArray after;
    if (!text.isEmpty())
        after.append(QJsonObject{{QStringLiteral("hash"), hash}, {QStringLiteral("text"), text}});
    for (const QJsonValue &value : before) {
        if (after.size() >= kMaxFilters)
            break;
        const QJsonObject entry = value.toObject();
        const QString entryHash = entry.value(QStringLiteral("hash")).toString();
        if (entryHash.isEmpty() || entryHash == hash ||
            entry.value(QStringLiteral("text")).toString().isEmpty())
            continue;
        after.append(entry);
    }
    if (after == before)
        return;
    section.insert(QStringLiteral("filters"), after);
    StateFile::writeSection(sectionKey(), section);
}

} // namespace omaseed::core::FilesViewState
