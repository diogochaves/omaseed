#include "KeyWatch.hpp"

#include <QKeyEvent>

KeyWatch::KeyWatch(QQuickItem *parent)
    : QQuickItem(parent)
{}

KeyWatch::~KeyWatch()
{
    watch(nullptr);
}

void KeyWatch::itemChange(ItemChange change, const ItemChangeData &data)
{
    if (change == ItemSceneChange)
        watch(data.window);
    QQuickItem::itemChange(change, data);
}

void KeyWatch::watch(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}

bool KeyWatch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !isEnabled() || event->type() != QEvent::KeyPress)
        return false;
    switch (static_cast<QKeyEvent *>(event)->key()) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_AltGr:
        return false;
    default:
        break;
    }
    Q_EMIT keyPressed();
    return false;
}
