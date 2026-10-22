#include "core/engines/EngineKind.hpp"

#include <QCoreApplication>
#include <QVersionNumber>

namespace omaseed::core {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("EngineKind", text);
}

/// "v5.2.3" → 5.2.3, "4.1.3 (6e5b2d1)" → 4.1.3, "3.00" → 3.0; null when there is no number.
QVersionNumber readVersion(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QLatin1Char('v')) || trimmed.startsWith(QLatin1Char('V')))
        trimmed.remove(0, 1);
    return QVersionNumber::fromString(trimmed);
}

} // namespace

QString engineKindToString(EngineKind kind)
{
    switch (kind) {
    case EngineKind::Qbittorrent:
        return QStringLiteral("qbittorrent");
    case EngineKind::Transmission:
        return QStringLiteral("transmission");
    case EngineKind::Deluge:
        return QStringLiteral("deluge");
    case EngineKind::Unknown:
        break;
    }
    return QString();
}

EngineKind engineKindFromString(const QString &text, bool *ok)
{
    if (ok)
        *ok = true;
    for (const EngineKind kind : kKnownEngines) {
        if (text == engineKindToString(kind))
            return kind;
    }
    if (ok)
        *ok = text.isEmpty();
    return EngineKind::Unknown;
}

QString engineDisplayName(EngineKind kind)
{
    switch (kind) {
    case EngineKind::Qbittorrent:
        return QStringLiteral("qBittorrent");
    case EngineKind::Transmission:
        return QStringLiteral("Transmission");
    case EngineKind::Deluge:
        return QStringLiteral("Deluge");
    case EngineKind::Unknown:
        break;
    }
    return QString();
}

bool isEngineDriven(EngineKind kind)
{
    return kind == EngineKind::Qbittorrent;
}

EngineSupport engineSupport(EngineKind kind, const QString &version, const QString &apiVersion)
{
    EngineSupport support;
    support.driven = isEngineDriven(kind);
    const QString name = engineDisplayName(kind);
    const QString shown = version.trimmed().startsWith(QLatin1Char('v')) ? version.trimmed().mid(1)
                                                                         : version.trimmed();

    switch (kind) {
    case EngineKind::Transmission: {
        // C14 E3: 4.1 and later, JSON-RPC 2.0 only. 4.1 is the first to send its RPC version
        // (6.0.0) with the 409; an older one sends none, or a lower one.
        support.minimum = QStringLiteral("4.1");
        const QVersionNumber rpc = readVersion(apiVersion);
        if (rpc.isNull() || rpc < QVersionNumber(6, 0, 0)) {
            support.versionRefused = true;
            support.refusal =
                shown.isEmpty()
                    ? tr("This Transmission is older than 4.1: omaseed needs 4.1 or later")
                    : tr("Transmission %1 is too old: omaseed needs 4.1 or later").arg(shown);
            support.advice = tr("omaseed speaks Transmission's JSON-RPC 2.0, which came with "
                                "4.1. Update Transmission on that machine, then test again.");
        }
        break;
    }
    case EngineKind::Deluge: {
        // C25 K8: below 2.1 there is no prefetch of a magnet's metadata.
        support.minimum = QStringLiteral("2.1");
        const QVersionNumber number = readVersion(version);
        if (!number.isNull() && number < QVersionNumber(2, 1)) {
            support.versionRefused = true;
            support.refusal = tr("Deluge %1 is too old: omaseed needs 2.1 or later").arg(shown);
            support.advice =
                tr("Update Deluge on that machine (its daemon and deluge-web), then test again.");
        }
        break;
    }
    case EngineKind::Qbittorrent:
    case EngineKind::Unknown:
        break;
    }

    if (support.refusal.isEmpty() && !support.driven && kind != EngineKind::Unknown) {
        // K9: named, and refused, keeping what was typed.
        support.refusal = tr("omaseed can't drive %1 yet").arg(name);
        support.advice = tr("This build of omaseed drives qBittorrent. %1 support is on its "
                            "way; until then, add a qBittorrent server.")
                             .arg(name);
    }
    return support;
}

} // namespace omaseed::core
