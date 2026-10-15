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
class KeyWatch : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit KeyWatch(QQuickItem *parent = nullptr);
    ~KeyWatch() override;

Q_SIGNALS:
    void keyPressed();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watch(QQuickWindow *window);

    QPointer<QQuickWindow> m_window;
};
