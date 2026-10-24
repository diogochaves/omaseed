// Kit/KeyWatch's `altHeld` against the window losing the focus (DECISIONS C24 H10): what
// tst_KeyWatch.qml cannot do, since the offscreen platform never hands the focus to a second
// window. The events are the ones a compositor's focus change brings.

#include "KeyWatch.hpp"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

class TestKeyWatch : public QObject
{
    Q_OBJECT

private slots:
    void altIsHeldAloneAndLetGo();
    void losingTheFocusLetsAltGo_data();
    void losingTheFocusLetsAltGo();

private:
    static void press(QQuickWindow &window, int key, Qt::KeyboardModifiers modifiers = {})
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(&window, &event);
    }
};

void TestKeyWatch::altIsHeldAloneAndLetGo()
{
    QQuickWindow window;
    KeyWatch watch;
    watch.setParentItem(window.contentItem());
    QSignalSpy held(&watch, &KeyWatch::altHeldChanged);
    QSignalSpy keys(&watch, &KeyWatch::keyPressed);

    // X11 reports alt's press without its modifier, Wayland with it: both are alt alone.
    press(window, Qt::Key_Alt);
    QVERIFY(watch.altHeld());
    press(window, Qt::Key_J, Qt::AltModifier);
    QVERIFY(!watch.altHeld());
    QCOMPARE(keys.size(), 1);
    press(window, Qt::Key_Alt, Qt::AltModifier);
    QVERIFY(watch.altHeld());
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
    QVERIFY(!watch.altHeld());
    press(window, Qt::Key_Alt, Qt::ShiftModifier | Qt::AltModifier);
    QVERIFY(!watch.altHeld());
    QCOMPARE(held.size(), 4);
}

void TestKeyWatch::losingTheFocusLetsAltGo_data()
{
    QTest::addColumn<int>("type");
    QTest::newRow("focus out") << int(QEvent::FocusOut);
    QTest::newRow("deactivated") << int(QEvent::WindowDeactivate);
    QTest::newRow("hidden") << int(QEvent::Hide);
}

void TestKeyWatch::losingTheFocusLetsAltGo()
{
    QFETCH(int, type);
    QQuickWindow window;
    KeyWatch watch;
    watch.setParentItem(window.contentItem());
    press(window, Qt::Key_Alt);
    QVERIFY(watch.altHeld());
    if (type == QEvent::FocusOut) {
        QFocusEvent event(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(&window, &event);
    } else {
        QEvent event(static_cast<QEvent::Type>(type));
        QCoreApplication::sendEvent(&window, &event);
    }
    QVERIFY(!watch.altHeld());
}

QTEST_MAIN(TestKeyWatch)
#include "tst_keywatch.moc"
