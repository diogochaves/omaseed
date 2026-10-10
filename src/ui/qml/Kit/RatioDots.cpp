#include "RatioDots.hpp"

#include "core/Beeswarm.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

/// RatioLadder's `_x`: a log axis from 0.1 to 30, anything else clamped onto its ends.
double xOf(double ratio, double width, double padLeft, double padRight)
{
    static const double lo = std::log10(0.1);
    static const double hi = std::log10(30.0);
    const double r = std::clamp(std::isfinite(ratio) ? ratio : 0.1, 0.1, 30.0);
    return padLeft + (width - padLeft - padRight) * (std::log10(r) - lo) / (hi - lo);
}

/// RatioLadder's `_radius`: sqrt(size in GiB) × 1.6, clamped to 2.5..11, in Theme units.
double radiusOf(double size, double unit)
{
    const double gib = std::max(0.0, size) / kGiB;
    return std::clamp(std::sqrt(gib) * 1.6, 2.5, 11.0) * unit;
}

} // namespace

RatioDots::RatioDots(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void RatioDots::invalidate()
{
    m_dirty = true;
    ++m_revision;
    Q_EMIT revisionChanged();
    polish();
}

void RatioDots::setPoints(const QVariantList &points)
{
    m_points = points;
    if (m_hoveredIndex >= m_points.size())
        m_hoveredIndex = -1;
    Q_EMIT pointsChanged();
    invalidate();
}

void RatioDots::setLayout(qreal &field, qreal value)
{
    if (qFuzzyCompare(field, value))
        return;
    field = value;
    Q_EMIT layoutChanged();
    invalidate();
}

void RatioDots::setLadderWidth(qreal width)
{
    setLayout(m_ladderWidth, width);
}

void RatioDots::setPadLeft(qreal pad)
{
    setLayout(m_padLeft, pad);
}

void RatioDots::setPadRight(qreal pad)
{
    setLayout(m_padRight, pad);
}

void RatioDots::setMidY(qreal y)
{
    setLayout(m_midY, y);
}

void RatioDots::setUnit(qreal unit)
{
    setLayout(m_unit, unit);
}

void RatioDots::setReach(qreal reach)
{
    if (qFuzzyCompare(m_reach, reach))
        return;
    m_reach = reach;
    Q_EMIT layoutChanged();
    update(); // the same dots, drawn further in
}

void RatioDots::setHoveredIndex(int index)
{
    if (index == m_hoveredIndex)
        return;
    m_hoveredIndex = index;
    Q_EMIT hoveredIndexChanged();
    update();
}

void RatioDots::setPaidColor(const QColor &color)
{
    if (color == m_paidColor)
        return;
    m_paidColor = color;
    Q_EMIT colorsChanged();
    update();
}

void RatioDots::setDueColor(const QColor &color)
{
    if (color == m_dueColor)
        return;
    m_dueColor = color;
    Q_EMIT colorsChanged();
    update();
}

void RatioDots::setHotColor(const QColor &color)
{
    if (color == m_hotColor)
        return;
    m_hotColor = color;
    Q_EMIT colorsChanged();
    update();
}

void RatioDots::place()
{
    if (!m_dirty)
        return;
    m_dirty = false;
    const qsizetype n = m_points.size();
    QList<double> ratios(n), sizes(n);
    for (qsizetype i = 0; i < n; ++i) {
        const QVariantMap p = m_points.at(i).toMap();
        ratios[i] = p.value(QStringLiteral("ratio")).toDouble();
        sizes[i] = p.value(QStringLiteral("size")).toDouble();
    }
    // Biggest first; equal sizes keep their order in `points`.
    QList<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(
        order.begin(), order.end(), [&](int a, int b) { return sizes.at(a) > sizes.at(b); });
    QList<double> xs, rs;
    xs.reserve(n);
    rs.reserve(n);
    for (int i : std::as_const(order)) {
        xs.append(xOf(ratios.at(i), m_ladderWidth, m_padLeft, m_padRight));
        rs.append(radiusOf(sizes.at(i), m_unit));
    }
    const QList<double> ys = omaseed::core::Beeswarm::place(xs, rs, m_midY, 4 * m_unit, 40);
    m_dots.clear();
    m_dots.reserve(n);
    for (qsizetype k = 0; k < n; ++k)
        m_dots.append(Dot{order.at(k), xs.at(k), ys.at(k), rs.at(k), ratios.at(order.at(k)) >= 1});
}

void RatioDots::updatePolish()
{
    if (!m_dirty)
        return;
    place();
    update();
}

int RatioDots::indexAt(qreal x, qreal y)
{
    place();
    for (qsizetype k = m_dots.size() - 1; k >= 0; --k) {
        const Dot &d = m_dots.at(k);
        if (std::hypot(d.x - x, d.y - y) <= d.r)
            return d.index;
    }
    return -1;
}

QVariantMap RatioDots::dot(int k)
{
    place();
    if (k < 0 || k >= m_dots.size())
        return {};
    const Dot &d = m_dots.at(k);
    return {{QStringLiteral("index"), d.index},
            {QStringLiteral("x"), d.x},
            {QStringLiteral("y"), d.y},
            {QStringLiteral("r"), d.r}};
}

void RatioDots::paint(QPainter *painter)
{
    // Called while the scene graph syncs, with the GUI thread waiting, so the dots are stable
    // (and placing them here, should a polish not have, is safe).
    place();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(m_reach, m_reach);
    QColor paid = m_paidColor, due = m_dueColor;
    paid.setAlphaF(paid.alphaF() * 0.75f);
    due.setAlphaF(due.alphaF() * 0.35f);
    painter->setPen(Qt::NoPen);
    for (const Dot &d : std::as_const(m_dots)) {
        const QRectF box(d.x - d.r, d.y - d.r, 2 * d.r, 2 * d.r);
        if (d.index == m_hoveredIndex) {
            // As a Rectangle draws its border: 1 px inside the edge, the dot itself opaque.
            painter->setBrush(d.paid ? m_paidColor : m_dueColor);
            painter->setPen(QPen(m_hotColor, 1));
            painter->drawEllipse(box.adjusted(0.5, 0.5, -0.5, -0.5));
            painter->setPen(Qt::NoPen);
            continue;
        }
        painter->setBrush(d.paid ? paid : due);
        painter->drawEllipse(box);
    }
}
