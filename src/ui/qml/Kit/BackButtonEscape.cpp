#include "BackButtonEscape.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>

BackButtonEscape::BackButtonEscape(QQuickItem *parent)
    : QQuickItem(parent)
{}

BackButtonEscape::~BackButtonEscape()
{
    watch(nullptr);
}

void BackButtonEscape::itemChange(ItemChange change, const ItemChangeData &data)
{
    if (change == ItemSceneChange)
        watch(data.window);
    QQuickItem::itemChange(change, data);
}

void BackButtonEscape::watch(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}

bool BackButtonEscape::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !isEnabled())
        return false;
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
        type != QEvent::MouseButtonDblClick)
        return false;
    const auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::BackButton && mouse->button() != Qt::ForwardButton)
        return false;
    if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::BackButton) {
        // Through the window, as a real key comes: to the item with the focus, and on up
        // through its parents until one takes it.
        const QString escape(QChar(0x1b));
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, escape);
        QCoreApplication::sendEvent(m_window, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier, escape);
        QCoreApplication::sendEvent(m_window, &release);
        Q_EMIT escapeSent();
    }
    return true;
}
