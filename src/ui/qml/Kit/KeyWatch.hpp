#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

/// Hears every key press in its window, and takes none (docs/31 F1: the footer's note after
/// `A` stays until the next key).
///
/// Put one anywhere in a window. While it is `enabled`, each key press that reaches the window
/// (a modifier alone does not count) emits `keyPressed()` before any item sees it; the press
/// then goes on as if nothing had looked.
///
/// It also says whether alt is held on its own (`altHeld`, DECISIONS C24 H10: the window's
/// keys are badged while it is): alt pressed with no other modifier sets it; alt's release,
/// any other key (so alt+j never leaves it set), another modifier, the window losing the
/// focus, or the watch being disabled clears it.
class KeyWatch : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool altHeld READ altHeld NOTIFY altHeldChanged)

public:
    explicit KeyWatch(QQuickItem *parent = nullptr);
    ~KeyWatch() override;

    bool altHeld() const { return m_altHeld; }

Q_SIGNALS:
    void keyPressed();
    void altHeldChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watch(QQuickWindow *window);
    void setAltHeld(bool held);

    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_activeConnection;
    bool m_altHeld = false;
};
