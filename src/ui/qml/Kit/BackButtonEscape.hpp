#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

/// The mouse's back button as one `esc` (#51 D8).
///
/// Put one anywhere in a window. While it is `enabled`, a back-button click anywhere in that
/// window sends the window an `esc` press and release, so it does whatever `esc` does where
/// the keyboard is: close the sheet, palette or menu on top, cancel a rename, clear the
/// selection, then the filter. It watches the window itself, ahead of the items, so the click
/// counts whatever MouseArea is under the pointer. The forward button does nothing. Neither
/// reaches the items: a MouseArea that takes every button (a scrim, a sheet's ground) would
/// read them as a click.
///
/// `escapeSent()` goes out after each `esc`.
class BackButtonEscape : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit BackButtonEscape(QQuickItem *parent = nullptr);
    ~BackButtonEscape() override;

Q_SIGNALS:
    void escapeSent();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watch(QQuickWindow *window);

    QPointer<QQuickWindow> m_window;
};
