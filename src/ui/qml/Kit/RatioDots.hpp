#pragma once

#include <QColor>
#include <QList>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>

/// The ratio ladder's dots (Kit/RatioLadder.qml), drawn as one item.
///
/// They were a Repeater of Rectangles: on a library that uploads some dot moves on nearly
/// every 2 s refresh, and each time the Repeater destroyed and made again one Item per
/// torrent, thousands of them, 50–65 ms of the main thread (#50). Here a new set of points
/// is placed (core::Beeswarm) and painted once, into one texture.
///
/// `points` are the ladder's `{ ratio, size }` (more keys are ignored). The ladder is
/// `ladderWidth` wide; the item reaches `reach` past it on every side, so a dot pushed to the
/// edge of the swarm is not cut off, and dot coordinates (`dot()`, `indexAt()`) are the
/// ladder's own, not the item's. A dot sits where RatioLadder's `_x(ratio)` puts it, with the
/// radius of its `_radius(size)`; the biggest are placed first, each at the first free height
/// from `midY` in steps of 4 × `unit` (at most 40 tries). Below 1.0 a dot is `dueColor` at
/// 35 %, from 1.0 `paidColor` at 75 %; the one at `hoveredIndex` is opaque with a 1 px
/// `hotColor` edge.
class RatioDots : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList points READ points WRITE setPoints NOTIFY pointsChanged)
    Q_PROPERTY(qreal ladderWidth READ ladderWidth WRITE setLadderWidth NOTIFY layoutChanged)
    Q_PROPERTY(qreal padLeft READ padLeft WRITE setPadLeft NOTIFY layoutChanged)
    Q_PROPERTY(qreal padRight READ padRight WRITE setPadRight NOTIFY layoutChanged)
    Q_PROPERTY(qreal midY READ midY WRITE setMidY NOTIFY layoutChanged)
    /// The Theme's spacing scale (`Theme.space(100) / 100`).
    Q_PROPERTY(qreal unit READ unit WRITE setUnit NOTIFY layoutChanged)
    Q_PROPERTY(qreal reach READ reach WRITE setReach NOTIFY layoutChanged)
    Q_PROPERTY(int hoveredIndex READ hoveredIndex WRITE setHoveredIndex NOTIFY hoveredIndexChanged)
    Q_PROPERTY(QColor paidColor READ paidColor WRITE setPaidColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor dueColor READ dueColor WRITE setDueColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor hotColor READ hotColor WRITE setHotColor NOTIFY colorsChanged)
    Q_PROPERTY(int count READ count NOTIFY pointsChanged)
    /// Goes up whenever the dots may have moved: a binding on `indexAt()` reads it to follow.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit RatioDots(QQuickItem *parent = nullptr);

    QVariantList points() const { return m_points; }
    void setPoints(const QVariantList &points);
    qreal ladderWidth() const { return m_ladderWidth; }
    void setLadderWidth(qreal width);
    qreal padLeft() const { return m_padLeft; }
    void setPadLeft(qreal pad);
    qreal padRight() const { return m_padRight; }
    void setPadRight(qreal pad);
    qreal midY() const { return m_midY; }
    void setMidY(qreal y);
    qreal unit() const { return m_unit; }
    void setUnit(qreal unit);
    qreal reach() const { return m_reach; }
    void setReach(qreal reach);
    int hoveredIndex() const { return m_hoveredIndex; }
    void setHoveredIndex(int index);
    QColor paidColor() const { return m_paidColor; }
    void setPaidColor(const QColor &color);
    QColor dueColor() const { return m_dueColor; }
    void setDueColor(const QColor &color);
    QColor hotColor() const { return m_hotColor; }
    void setHotColor(const QColor &color);
    int count() const { return int(m_points.size()); }
    int revision() const { return m_revision; }

    /// The topmost dot under a point in ladder coordinates (the last drawn wins), as an index
    /// into `points`, or -1.
    Q_INVOKABLE int indexAt(qreal x, qreal y);
    /// The `k`th dot in drawing order (biggest first): `{ index, x, y, r }`, `index` into
    /// `points`, in ladder coordinates; empty past the end.
    Q_INVOKABLE QVariantMap dot(int k);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void pointsChanged();
    void layoutChanged();
    void hoveredIndexChanged();
    void colorsChanged();
    void revisionChanged();

protected:
    void updatePolish() override;

private:
    struct Dot
    {
        int index = 0;
        double x = 0, y = 0, r = 0;
        bool paid = false;
    };

    /// The dots must be placed again before they are read or drawn.
    void invalidate();
    void place();
    void setLayout(qreal &field, qreal value);

    QVariantList m_points;
    qreal m_ladderWidth = 0;
    qreal m_padLeft = 0;
    qreal m_padRight = 0;
    qreal m_midY = 0;
    qreal m_unit = 1;
    qreal m_reach = 0;
    int m_hoveredIndex = -1;
    QColor m_paidColor;
    QColor m_dueColor;
    QColor m_hotColor;
    int m_revision = 0;
    bool m_dirty = false;
    QList<Dot> m_dots; ///< in drawing order
};
