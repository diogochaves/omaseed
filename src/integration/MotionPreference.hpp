#pragma once

// MotionPreference — whether the desktop asks for less motion (#24 T5: a row that is sending
// pulses, "off under reduced motion", docs/22 §6).
//
// Linux has one place for it: the settings portal's `org.freedesktop.appearance`
// `reduced-motion` (0 no preference, 1 reduce). It is read once, asynchronously, and followed
// through `SettingChanged`. No portal, or one that does not know the key, reads as no
// preference: the animations the user turned on run.

#include <QDBusConnection>
#include <QObject>
#include <QVariant>

class QDBusVariant;

namespace omaseed::integration {

class MotionPreference : public QObject
{
    Q_OBJECT

public:
    explicit MotionPreference(QDBusConnection connection = QDBusConnection::sessionBus(),
                              QObject *parent = nullptr);

    bool reduced() const { return m_reduced; }

    /// The portal's value, however many variants it comes wrapped in: true for 1 only.
    static bool parse(const QVariant &value);

Q_SIGNALS:
    void reducedChanged();

private Q_SLOTS:
    void onSettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);

private:
    void setReduced(bool reduced);

    QDBusConnection m_connection;
    bool m_reduced = false;
};

} // namespace omaseed::integration
