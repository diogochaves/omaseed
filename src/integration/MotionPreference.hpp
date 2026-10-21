#pragma once

// MotionPreference — whether the desktop asks for less motion (#24 T5: a row that is sending
// pulses, "off under reduced motion", docs/22 §6).
//
// Two places say so, and either one is enough:
//  - the settings portal's `org.freedesktop.appearance` `reduced-motion` (0 no preference,
//    1 reduce), read once, asynchronously, and followed through `SettingChanged`;
//  - Hyprland's `animations:enabled` set to false (#111): Omarchy's "no animations" toggle,
//    which 4.5 turns on by itself where the desktop renders on the CPU, a VM for one. Its
//    shell stills its own motion then (Style.reduceMotion). Asked with `hyprctl -j getoption`
//    at start and again on every `configreloaded`, which is when the toggle lands.
// No portal, or one that does not know the key, and no Hyprland read as no preference: the
// animations the user turned on run.

#include <QByteArray>
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariant>

class QDBusVariant;

namespace omaseed::integration {

class HyprlandEvents;

class MotionPreference : public QObject
{
    Q_OBJECT

public:
    /// How MotionPreference reaches Hyprland: the `hyprctl` it asks, and the event socket
    /// whose `configreloaded` makes it ask again. Tests point both elsewhere; an empty
    /// `hyprctl` leaves Hyprland out.
    struct Hyprland
    {
        QString hyprctl = QStringLiteral("hyprctl");
        QString eventSocket = defaultEventSocket();
    };
    /// The session's event socket, "" when not under Hyprland.
    static QString defaultEventSocket();

    explicit MotionPreference(QDBusConnection connection = QDBusConnection::sessionBus(),
                              QObject *parent = nullptr);
    MotionPreference(QDBusConnection connection,
                     const Hyprland &hyprland,
                     QObject *parent = nullptr);
    ~MotionPreference() override;

    bool reduced() const { return m_portalReduced || m_animationsOff; }

    /// The portal's value, however many variants it comes wrapped in: true for 1 only.
    static bool parse(const QVariant &value);

    /// What `hyprctl -j getoption animations:enabled` printed: true only when it says the
    /// animations are off (`"bool": false`, or `"int": 0` from a Hyprland that answers so).
    /// Anything else (no answer, "no such option") is not a request for less motion.
    static bool animationsOff(const QByteArray &getoption);

Q_SIGNALS:
    void reducedChanged();

private Q_SLOTS:
    void onSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    void readPortal();
    void readHyprland();
    void update(bool portalReduced, bool animationsOff);

    QDBusConnection m_connection;
    Hyprland m_hyprland;
    HyprlandEvents *m_hyprlandEvents = nullptr;
    int m_hyprlandRead = 0;
    bool m_portalReduced = false;
    bool m_animationsOff = false;
};

} // namespace omaseed::integration
