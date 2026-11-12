#include "core/LibraryCrossing.hpp"

#include <QRegularExpression>

#include <array>

namespace omaseed::core {

namespace {

using NameKind = LibraryCrossing::NameKind;

/// One source name of one record, and where it goes.
struct Step
{
    NameKind kind = NameKind::Label;
    QString name;
    bool kept = false;
    NameKind toKind = NameKind::Label;
    QString toName;
};

EngineKind known(EngineKind kind)
{
    return kind == EngineKind::Unknown ? EngineKind::Qbittorrent : kind;
}

/// A record's names in order, the category first, each once per kind.
QList<Step> sourceNames(EngineKind from, const TorrentRecord &record)
{
    const bool qbt = from == EngineKind::Qbittorrent;
    QList<Step> out;
    const auto add = [&out](NameKind kind, const QString &name) {
        if (name.isEmpty())
            return;
        for (const Step &step : std::as_const(out)) {
            if (step.kind == kind && step.name == name)
                return;
        }
        Step step;
        step.kind = kind;
        step.name = name;
        out.append(step);
    };
    add(qbt ? NameKind::Category : NameKind::Label, record.category);
    for (const QString &tag : record.tags)
        add(qbt ? NameKind::Tag : NameKind::Label, tag);
    return out;
}

/// docs/DECISIONS.md C40 X4: the category is the first label, both ways; Deluge keeps one.
QList<Step> cross(EngineKind from, EngineKind to, const TorrentRecord &record)
{
    from = known(from);
    to = known(to);
    QList<Step> steps = sourceNames(from, record);
    for (qsizetype i = 0; i < steps.size(); ++i) {
        Step &step = steps[i];
        if (from == to) {
            step.kept = true;
            step.toKind = step.kind;
            step.toName = step.name;
        } else if (to == EngineKind::Transmission) {
            step.kept = true;
            step.toKind = NameKind::Label;
            step.toName = step.name;
        } else if (to == EngineKind::Qbittorrent) {
            step.kept = true;
            step.toKind = i == 0 ? NameKind::Category : NameKind::Tag;
            step.toName = step.name;
        } else {
            // Deluge: the category or the first label, in its spelling. A qBittorrent's tag
            // never stands in for a category it doesn't have.
            if (i != 0 || step.kind == NameKind::Tag)
                continue;
            step.toName = LibraryCrossing::delugeLabel(step.name);
            step.kept = !step.toName.isEmpty();
            step.toKind = NameKind::Label;
        }
    }
    return steps;
}

LibraryCrossing::Grouping groupingOf(EngineKind to, const QList<Step> &steps)
{
    LibraryCrossing::Grouping g;
    for (const Step &step : steps) {
        if (!step.kept) {
            g.leftBehind.append(step.name);
        } else if (step.toKind == NameKind::Category ||
                   (step.toKind == NameKind::Label && known(to) == EngineKind::Deluge)) {
            g.category = step.toName;
        } else if (!g.tags.contains(step.toName)) {
            g.tags.append(step.toName); // a category and a tag of one name: one label
        }
    }
    return g;
}

bool actionKept(EngineKind to, const QString &action)
{
    if (action.isEmpty() || action == QLatin1String("Default") || action == QLatin1String("Stop"))
        return true;
    return to == EngineKind::Deluge && action == QLatin1String("Remove");
}

} // namespace

QString LibraryCrossing::delugeLabel(const QString &name)
{
    static const QRegularExpression outside(QStringLiteral("[^a-z0-9_-]+"));
    static const QRegularExpression ends(QStringLiteral("^-+|-+$"));
    QString out = name.toLower();
    out.replace(outside, QStringLiteral("-"));
    out.remove(ends);
    return out;
}

LibraryCrossing::Grouping
LibraryCrossing::grouping(EngineKind from, EngineKind to, const TorrentRecord &record)
{
    return groupingOf(to, cross(from, to, record));
}

QList<LibraryCrossing::Name>
LibraryCrossing::names(EngineKind from, EngineKind to, const QList<TorrentRecord> &records)
{
    QList<Name> out;
    for (const TorrentRecord &record : records) {
        for (const Step &step : cross(from, to, record)) {
            qsizetype at = 0;
            while (at < out.size() &&
                   (out.at(at).kind != step.kind || out.at(at).name != step.name))
                ++at;
            if (at == out.size())
                out.append(Name{step.kind, step.name, {}});
            QList<Outcome> &outcomes = out[at].outcomes;
            Outcome outcome;
            if (step.kept)
                outcome = Outcome{true, step.toKind, step.toName, 0};
            qsizetype same = 0;
            while (same < outcomes.size() && (outcomes.at(same).kept != outcome.kept ||
                                              outcomes.at(same).kind != outcome.kind ||
                                              outcomes.at(same).name != outcome.name))
                ++same;
            if (same == outcomes.size())
                outcomes.append(outcome);
            ++outcomes[same].torrents;
        }
    }
    return out;
}

QList<LibraryCrossing::Loss>
LibraryCrossing::losses(EngineKind from, EngineKind to, const QList<TorrentRecord> &records)
{
    from = known(from);
    to = known(to);
    if (from == to || to == EngineKind::Qbittorrent)
        return {}; // qBittorrent has a place for all a record holds (Deluge's Remove too)
    const bool deluge = to == EngineKind::Deluge;
    std::array<int, 6> counts{};
    const auto count = [&counts](LossKind kind, bool lost) {
        if (lost)
            ++counts[size_t(kind)];
    };
    for (const TorrentRecord &record : records) {
        count(LossKind::SeedingTimeLimit, record.seedingTimeLimit >= 0);
        count(LossKind::IdleLimit, deluge && record.inactiveSeedingTimeLimit >= 0);
        count(LossKind::ShareLimitAction, !actionKept(to, record.shareLimitAction));
        count(LossKind::AutoManagement, record.autoTmm);
        count(LossKind::IncompleteFolder, !record.downloadPath.isEmpty());
        count(LossKind::GroupingNames, deluge && !grouping(from, to, record).leftBehind.isEmpty());
    }
    QList<Loss> out;
    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > 0)
            out.append(Loss{LossKind(i), counts[i]});
    }
    return out;
}

} // namespace omaseed::core
