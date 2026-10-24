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
    else if (change == ItemEnabledHasChanged && !isEnabled())
        setAltHeld(false);
    QQuickItem::itemChange(change, data);
}

void KeyWatch::watch(QQuickWindow *window)
{
    if (m_window == window)
        return;
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_activeConnection);
    }
    m_window = window;
    setAltHeld(false);
    if (m_window) {
        m_window->installEventFilter(this);
        // The window losing the focus is not always a FocusOut the filter sees (a compositor
        // that takes the keyboard away); its `active` going false is.
        m_activeConnection = connect(m_window, &QWindow::activeChanged, this, [this]() {
            if (m_window && !m_window->isActive())
                setAltHeld(false);
        });
    }
}

void KeyWatch::setAltHeld(bool held)
{
    if (m_altHeld == held)
        return;
    m_altHeld = held;
    Q_EMIT altHeldChanged();
}

bool KeyWatch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !isEnabled())
        return false;
    switch (event->type()) {
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
        setAltHeld(false);
        return false;
    case QEvent::KeyRelease:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Alt)
            setAltHeld(false);
        return false;
    case QEvent::KeyPress:
        break;
    default:
        return false;
    }
    const auto *key = static_cast<QKeyEvent *>(event);
    switch (key->key()) {
    case Qt::Key_Alt:
        // Alt alone: the press may or may not carry alt's own modifier (X11 reports the state
        // before the press, Wayland after), but never another one.
        setAltHeld((key->modifiers() & ~(Qt::AltModifier | Qt::KeypadModifier)) == 0);
        return false;
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_AltGr:
        setAltHeld(false);
        return false;
    default:
        break;
    }
    setAltHeld(false);
    Q_EMIT keyPressed();
    return false;
}
