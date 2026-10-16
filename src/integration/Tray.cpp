#include "integration/Tray.hpp"

#include "integration/BarMenu.hpp"
#include "integration/ThemeReader.hpp"

#include <QAction>
#include <QActionGroup>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSvgRenderer>
#include <QtDebug>

namespace omaseed::integration {

namespace {

constexpr int kIconSizes[] = {16, 22, 24, 32, 48};

QColor colorOf(const ColorMap &palette, const QString &key, const QColor &fallback)
{
    const QColor colour = QColor::fromString(palette.value(key));
    return colour.isValid() ? colour : fallback;
}

} // namespace

Tray::Tray(ThemeReader *reader, QObject *parent)
    : QObject(parent)
    , m_reader(reader)
    , m_palette(defaultPalette())
{
    buildMenu();

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &Tray::onActivated);

    if (m_reader) {
        connect(m_reader, &ThemeReader::changed, this, &Tray::applyFromReader);
        applyFromReader();
    } else {
        refreshIcon();
    }

    setIconVisible(true);
}

void Tray::setIconVisible(bool visible)
{
    m_iconWanted = visible;
    if (visible && !isAvailable()) {
        waitForHost();
        return;
    }
    m_trayIcon->setVisible(visible);
}

void Tray::waitForHost()
{
    if (!m_loggedUnavailable) {
        m_loggedUnavailable = true;
        qWarning().noquote() << QStringLiteral(
            "Tray: no StatusNotifierHost available; the icon appears when a bar starts one");
    }
    if (m_hostWatcher)
        return;
    // The bar may start after us (autostart order) or restart while we sit in the tray:
    // show the icon as soon as its StatusNotifierWatcher is back.
    m_hostWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                            QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForRegistration,
                                            this);
    connect(m_hostWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (!m_iconWanted)
            return;
        qInfo().noquote() << QStringLiteral("Tray: a StatusNotifierHost appeared; showing the "
                                            "icon");
        m_trayIcon->show();
    });
}

Tray::~Tray()
{
    delete m_menu;
    m_menu = nullptr;
}

bool Tray::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void Tray::buildMenu()
{
    m_menu = new QMenu();

    m_showAction = m_menu->addAction(QStringLiteral("show omaseed"));
    connect(m_showAction, &QAction::triggered, this, &Tray::showRequested);

    m_menu->addSeparator();

    m_speedsInfoAction = m_menu->addAction(QString());
    m_speedsInfoAction->setEnabled(false);

    m_statsInfoAction = m_menu->addAction(QString());
    m_statsInfoAction->setEnabled(false);

    m_menu->addSeparator();

    // Sensible defaults so the two info lines are never blank before the first setStats().
    setStats(QStringLiteral("0 B/s"), QStringLiteral("0 B/s"), 0, 0, QString());

    // A menu of its own until App gives it the shared one: the same rows, nobody answering.
    m_ownModel = new BarMenu(this);
    setMenuModel(m_ownModel);
}

void Tray::setMenuModel(BarMenu *model)
{
    if (!model)
        model = m_ownModel;
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    connect(m_model, &BarMenu::changed, this, &Tray::syncSharedRows);
    syncSharedRows();
}

void Tray::syncSharedRows()
{
    if (!m_model)
        return;
    const QList<BarMenu::Row> rows = m_model->rows();

    bool sameShape = rows.size() == m_sharedActions.size();
    for (int i = 0; sameShape && i < rows.size(); ++i)
        sameShape = rows.at(i).id == m_sharedActions.at(i).first;

    if (!sameShape) {
        for (const auto &entry : std::as_const(m_sharedActions)) {
            m_menu->removeAction(entry.second);
            entry.second->deleteLater();
        }
        m_sharedActions.clear();
        delete m_serverGroup;
        m_serverGroup = nullptr;

        for (const BarMenu::Row &row : rows) {
            QAction *action = new QAction(m_menu);
            if (row.kind == BarMenu::Kind::Separator) {
                action->setSeparator(true);
            } else if (row.kind == BarMenu::Kind::Radio) {
                if (!m_serverGroup) {
                    m_serverGroup = new QActionGroup(this);
                    m_serverGroup->setExclusive(true);
                }
                action->setCheckable(true);
                m_serverGroup->addAction(action);
            } else if (row.kind == BarMenu::Kind::Check) {
                action->setCheckable(true);
            }
            if (row.kind != BarMenu::Kind::Separator && row.kind != BarMenu::Kind::Caption) {
                const QString id = row.id;
                connect(action, &QAction::triggered, this, [this, id]() {
                    if (m_model)
                        m_model->invoke(id);
                    // The row follows the app, not the click: a refused switch or a server
                    // that does not answer leaves the check where the app is.
                    syncSharedRows();
                });
            }
            m_menu->addAction(action);
            m_sharedActions.append({row.id, action});
        }
    }

    for (int i = 0; i < rows.size(); ++i) {
        const BarMenu::Row &row = rows.at(i);
        QAction *action = m_sharedActions.at(i).second;
        if (row.kind == BarMenu::Kind::Separator)
            continue;
        // DBusMenu has no column on the right: a server's figure follows its name (T1).
        const QString text =
            row.detail.isEmpty() ? row.label : QStringLiteral("%1 · %2").arg(row.label, row.detail);
        if (action->text() != text)
            action->setText(text);
        const bool enabled = row.kind != BarMenu::Kind::Caption && row.enabled;
        if (action->isEnabled() != enabled)
            action->setEnabled(enabled);
        if (action->isCheckable() && action->isChecked() != row.checked) {
            const QSignalBlocker blocker(action);
            action->setChecked(row.checked);
        }
    }
}

void Tray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        emit toggleWindowRequested();
    else if (reason == QSystemTrayIcon::MiddleClick && m_model)
        m_model->invoke(QStringLiteral("alt-speed"));
}

void Tray::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    refreshIcon();
}

void Tray::setDownloadingCount(int count)
{
    count = qMax(0, count);
    if (m_downloadingCount == count)
        return;
    m_downloadingCount = count;
    if (m_state == Downloading)
        refreshIcon();
}

void Tray::setTooltip(const QString &server,
                      const QString &downloadSpeedText,
                      const QString &uploadSpeedText,
                      int downloading,
                      int seeding)
{
    m_tooltip = QStringLiteral("%1 · ↓ %2 ↑ %3 · %4 downloading · %5 seeding")
                    .arg(server, downloadSpeedText, uploadSpeedText)
                    .arg(downloading)
                    .arg(seeding);
    m_trayIcon->setToolTip(m_tooltip);
}

void Tray::setStats(const QString &downloadSpeedText,
                    const QString &uploadSpeedText,
                    int downloading,
                    int seeding,
                    const QString &freeText)
{
    m_speedsInfoAction->setText(
        QStringLiteral("↓ %1 · ↑ %2").arg(downloadSpeedText, uploadSpeedText));
    const QString statsText =
        freeText.isEmpty()
            ? QStringLiteral("%1 downloading · %2 seeding").arg(downloading).arg(seeding)
            : QStringLiteral("%1 downloading · %2 seeding · %3 free")
                  .arg(downloading)
                  .arg(seeding)
                  .arg(freeText);
    m_statsInfoAction->setText(statsText);
}

bool Tray::altSpeedChecked() const
{
    for (const auto &entry : m_sharedActions) {
        if (entry.first == QLatin1String("alt-speed"))
            return entry.second->isChecked();
    }
    return false;
}

void Tray::setAltKind(const QString &kind)
{
    if (m_altKind == kind)
        return;
    m_altKind = kind;
    if (m_state == AltSpeed)
        refreshIcon();
}

void Tray::setPalette(const QHash<QString, QColor> &palette)
{
    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it)
        m_palette.insert(it.key(), it.value());
    refreshIcon();
}

void Tray::applyFromReader()
{
    if (!m_reader)
        return;
    const ColorMap raw = m_reader->palette();

    m_palette[QStringLiteral("accent")] =
        colorOf(raw, QStringLiteral("accent"), m_palette.value(QStringLiteral("accent")));
    m_palette[QStringLiteral("foreground")] =
        colorOf(raw, QStringLiteral("foreground"), m_palette.value(QStringLiteral("foreground")));
    m_palette[QStringLiteral("dim")] =
        colorOf(raw, QStringLiteral("dark_foreground"), m_palette.value(QStringLiteral("dim")));
    m_palette[QStringLiteral("red")] =
        colorOf(raw, QStringLiteral("red"), m_palette.value(QStringLiteral("red")));
    m_palette[QStringLiteral("yellow")] =
        colorOf(raw, QStringLiteral("yellow"), m_palette.value(QStringLiteral("yellow")));
    m_palette[QStringLiteral("green")] =
        colorOf(raw, QStringLiteral("green"), m_palette.value(QStringLiteral("green")));
    m_palette[QStringLiteral("background")] =
        colorOf(raw, QStringLiteral("background"), m_palette.value(QStringLiteral("background")));

    refreshIcon();
}

void Tray::refreshIcon()
{
    m_trayIcon->setIcon(iconForState(m_state));
}

QString Tray::resourceForState(State state)
{
    switch (state) {
    case Disconnected:
        return QStringLiteral(":/omaseed/tray/disconnected.svg");
    case Idle:
        return QStringLiteral(":/omaseed/tray/idle.svg");
    case Downloading:
        return QStringLiteral(":/omaseed/tray/downloading.svg");
    case AltSpeed:
        return QStringLiteral(":/omaseed/tray/altspeed.svg");
    case Error:
        return QStringLiteral(":/omaseed/tray/error.svg");
    }
    return QString();
}

QHash<QString, QColor> Tray::defaultPalette()
{
    return {
        {QStringLiteral("accent"), QColor(QStringLiteral("#7aa2f7"))},
        {QStringLiteral("foreground"), QColor(QStringLiteral("#a9b1d6"))},
        {QStringLiteral("dim"), QColor(QStringLiteral("#565f89"))},
        {QStringLiteral("red"), QColor(QStringLiteral("#f7768e"))},
        {QStringLiteral("yellow"), QColor(QStringLiteral("#e0af68"))},
        {QStringLiteral("green"), QColor(QStringLiteral("#9ece6a"))},
        {QStringLiteral("background"), QColor(QStringLiteral("#1a1b26"))},
    };
}

QIcon Tray::iconForState(State state) const
{
    QFile file(resourceForState(state));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("Tray: missing icon resource for state %1")
                                    .arg(static_cast<int>(state));
        return QIcon();
    }

    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("#ACCENT"), m_palette.value(QStringLiteral("accent")).name());
    svg.replace(QStringLiteral("#FG"), m_palette.value(QStringLiteral("foreground")).name());
    svg.replace(QStringLiteral("#RED"), m_palette.value(QStringLiteral("red")).name());
    svg.replace(QStringLiteral("#DIM"), m_palette.value(QStringLiteral("dim")).name());

    QSvgRenderer renderer(svg.toUtf8());

    QIcon icon;
    for (int size : kIconSizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(0, 0, size, size));

        if (state == Downloading && m_downloadingCount > 0)
            paintCountBadge(&painter, size, m_downloadingCount);
        else if (state == AltSpeed)
            paintAltSpeedMark(&painter, size);
        else if (state == Error)
            paintErrorMark(&painter, size);

        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

void Tray::paintCountBadge(QPainter *painter, int size, int count) const
{
    const QString text = count > 9 ? QStringLiteral("9+") : QString::number(count);
    const qreal badgeSize = size * 0.56;
    const QRectF rect(size - badgeSize, size - badgeSize, badgeSize, badgeSize);

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_palette.value(QStringLiteral("accent")));
    painter->drawEllipse(rect);

    QFont font = painter->font();
    font.setPixelSize(qMax(6, static_cast<int>(badgeSize * 0.6)));
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(m_palette.value(QStringLiteral("background")));
    painter->drawText(rect, Qt::AlignCenter, text);
}

void Tray::paintAltSpeedMark(QPainter *painter, int size) const
{
    const qreal markSize = size * 0.5;
    const QRectF rect(size - markSize, size - markSize, markSize, markSize);

    // docs/31 T1: a burst set is a green disc with ⏶ (up, faster); a turtle or plain
    // alt-speed keeps the yellow ⏷.
    const bool burst = m_altKind == QLatin1String("burst");
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_palette.value(burst ? QStringLiteral("green") : QStringLiteral("yellow")));
    painter->drawEllipse(rect);

    QFont font = painter->font();
    font.setPixelSize(qMax(6, static_cast<int>(markSize * 0.68)));
    painter->setFont(font);
    painter->setPen(m_palette.value(QStringLiteral("background")));
    painter->drawText(rect, Qt::AlignCenter, burst ? QStringLiteral("⏶") : QStringLiteral("⏷"));
}

// The error state must differ from idle by shape, not only by colour: some themes' "red"
// is not red at all (Hackerman's is #50f872). A red disc with an `!` knocked out of it, ringed
// in the background colour so it separates from the red diamond under it (docs/08 §1).
void Tray::paintErrorMark(QPainter *painter, int size) const
{
    const qreal markSize = size * 0.56;
    const QRectF rect(size - markSize, size - markSize, markSize, markSize);
    const QColor background = m_palette.value(QStringLiteral("background"));

    painter->setPen(QPen(background, qMax(1.0, size * 0.08)));
    painter->setBrush(m_palette.value(QStringLiteral("red")));
    painter->drawEllipse(rect.adjusted(0.5, 0.5, -0.5, -0.5));

    const qreal barWidth = qMax(1.5, markSize * 0.2);
    const qreal x = rect.center().x() - barWidth / 2;
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRect(QRectF(x, rect.top() + markSize * 0.2, barWidth, markSize * 0.36));
    painter->drawRect(QRectF(x, rect.top() + markSize * 0.64, barWidth, barWidth));
}

} // namespace omaseed::integration
