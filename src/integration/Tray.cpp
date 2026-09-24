#include "integration/Tray.hpp"

#include "integration/ThemeReader.hpp"

#include <QAction>
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

    m_addAction = m_menu->addAction(QStringLiteral("add torrent…"));
    connect(m_addAction, &QAction::triggered, this, &Tray::addRequested);

    m_menu->addSeparator();

    m_speedsInfoAction = m_menu->addAction(QString());
    m_speedsInfoAction->setEnabled(false);

    m_statsInfoAction = m_menu->addAction(QString());
    m_statsInfoAction->setEnabled(false);

    m_menu->addSeparator();

    m_altSpeedAction = m_menu->addAction(QStringLiteral("alternative speed limits"));
    m_altSpeedAction->setCheckable(true);
    connect(m_altSpeedAction, &QAction::triggered, this, &Tray::altSpeedToggled);

    m_stopAllAction = m_menu->addAction(QStringLiteral("stop all"));
    connect(m_stopAllAction, &QAction::triggered, this, &Tray::stopAllRequested);

    m_startAllAction = m_menu->addAction(QStringLiteral("start all"));
    connect(m_startAllAction, &QAction::triggered, this, &Tray::startAllRequested);

    m_menu->addSeparator();

    m_settingsAction = m_menu->addAction(QStringLiteral("settings…"));
    connect(m_settingsAction, &QAction::triggered, this, &Tray::settingsRequested);

    m_quitAction = m_menu->addAction(QStringLiteral("quit"));
    connect(m_quitAction, &QAction::triggered, this, &Tray::quitRequested);

    // Sensible defaults so the two info lines are never blank before the first setStats().
    setStats(QStringLiteral("0 B/s"), QStringLiteral("0 B/s"), 0, 0, QString());
}

void Tray::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        emit toggleWindowRequested();
    else if (reason == QSystemTrayIcon::MiddleClick)
        emit altSpeedToggled(!m_altSpeedAction->isChecked());
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
    return m_altSpeedAction->isChecked();
}

void Tray::setAltSpeedChecked(bool checked)
{
    if (m_altSpeedAction->isChecked() == checked)
        return;
    const QSignalBlocker blocker(m_altSpeedAction);
    m_altSpeedAction->setChecked(checked);
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

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_palette.value(QStringLiteral("yellow")));
    painter->drawEllipse(rect);

    QFont font = painter->font();
    font.setPixelSize(qMax(6, static_cast<int>(markSize * 0.68)));
    painter->setFont(font);
    painter->setPen(m_palette.value(QStringLiteral("background")));
    painter->drawText(rect, Qt::AlignCenter, QStringLiteral("⏷"));
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
