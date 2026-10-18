#include "StartupSplash.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace {
class StartupActivityIndicator final : public QWidget
{
public:
    explicit StartupActivityIndicator(QWidget *parent) : QWidget(parent), m_motion(new QVariantAnimation(this))
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAccessibleName("Starting WormProfiler");
        m_motion->setObjectName("splashSweep");
        m_motion->setStartValue(0.0);
        m_motion->setEndValue(1.0);
        m_motion->setDuration(1450);
        m_motion->setLoopCount(-1);
        m_motion->setEasingCurve(QEasingCurve::Linear);
        connect(m_motion, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            const QRectF previous = segmentRect();
            m_phase = value.toReal();
            update(previous.united(segmentRect()).toAlignedRect().adjusted(-1, 0, 1, 0));
        });
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        if (style()->styleHint(QStyle::SH_Widget_Animate)) {
            m_motion->start();
            m_motion->setCurrentTime(350);
        }
    }

    void hideEvent(QHideEvent *event) override
    {
        m_motion->stop();
        QWidget::hideEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#1b2021"));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#374243"));
        painter.drawRoundedRect(QRectF(rect()), 1.5, 1.5);
        painter.setBrush(QColor("#8addc5"));
        painter.drawRoundedRect(segmentRect(), 1.5, 1.5);
    }

private:
    QRectF segmentRect() const
    {
        const qreal segment = width() * 0.27;
        return QRectF(-segment + (width() + segment) * m_phase, 0, segment, height());
    }

    QVariantAnimation *m_motion;
    qreal m_phase = 0.24;
};
}

StartupSplash::StartupSplash(QScreen *display)
    : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint)
{
    setObjectName("startupSplash");
    setWindowTitle("WormProfiler");
    setWindowIcon(QGuiApplication::windowIcon());
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_QuitOnClose, false);
    if (display) setScreen(display);

    const QSize dimensions = screen()
        ? QSize(560, 352).boundedTo(screen()->availableGeometry().size() - QSize(32, 32))
        : QSize(560, 352);
    setFixedSize(dimensions);
    if (screen()) move(screen()->availableGeometry().center() - rect().center());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(0);

    m_logo = new QLabel(this);
    m_logo->setObjectName("splashLogo");
    m_logo->setFixedSize(188, 188);
    m_logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logo, 0, Qt::AlignHCenter);
    layout->addSpacing(14);

    auto *title = new QLabel("WormProfiler", this);
    title->setObjectName("splashTitle");
    QFont titleFont = font();
    titleFont.setPointSize(23);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    title->setFixedHeight(40);
    layout->addWidget(title);
    layout->addStretch();

    m_status = new QLabel("Starting WormProfiler", this);
    m_status->setObjectName("splashStatus");
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_status->setFixedHeight(22);
    layout->addWidget(m_status);
    layout->addSpacing(8);

    auto *progress = new StartupActivityIndicator(this);
    progress->setObjectName("splashProgress");
    progress->setFixedHeight(3);
    layout->addWidget(progress);

    setStyleSheet(R"(
        QLabel { background: transparent; color: #f1f5f3; }
        QLabel#splashStatus { color: #a7b7b3; font-size: 12px; }
    )");
    cacheArtwork();
}

void StartupSplash::cacheArtwork()
{
    const qreal scale = devicePixelRatioF();
    m_background = QPixmap(size() * scale);
    m_background.setDevicePixelRatio(scale);
    m_background.fill(QColor("#202627"));
    QPainter painter(&m_background);
    painter.fillRect(QRect(0, height() - 67, width(), 67), QColor("#1b2021"));
    painter.setPen(QPen(QColor("#465051"), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    painter.setPen(QPen(QColor("#354041"), 1));
    painter.drawLine(1, height() - 68, width() - 2, height() - 68);

    const QRectF specimen(width() / 2.0 - 106, 26, 212, 196);
    painter.setPen(QPen(QColor("#50625e"), 1));
    constexpr qreal tick = 10;
    for (const QPointF &corner : {specimen.topLeft(), specimen.topRight(), specimen.bottomLeft(), specimen.bottomRight()}) {
        const qreal dx = corner.x() < specimen.center().x() ? tick : -tick;
        const qreal dy = corner.y() < specimen.center().y() ? tick : -tick;
        painter.drawLine(corner, corner + QPointF(dx, 0));
        painter.drawLine(corner, corner + QPointF(0, dy));
    }
    if (m_logo) m_logo->setPixmap(windowIcon().pixmap(m_logo->size(), scale));
}

bool StartupSplash::event(QEvent *event)
{
    const bool result = QWidget::event(event);
    if (event->type() == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        || event->type() == QEvent::DevicePixelRatioChange
#endif
    ) {
        if (!m_background.isNull()) {
            cacheArtwork();
            update();
        }
    }
    return result;
}

void StartupSplash::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_background);
}

void StartupSplash::showEvent(QShowEvent *event)
{
    if (!m_displayTime.isValid()) m_displayTime.start();
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows") {
        // Windows 11 rounds the opaque native surface without per-pixel alpha.
        const int roundCorners = 2;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), static_cast<DWMWINDOWATTRIBUTE>(33),
                              &roundCorners, sizeof(roundCorners));
    }
#endif
    QWidget::showEvent(event);
}

void StartupSplash::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

bool StartupSplash::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_target && event->type() == QEvent::Paint) {
        m_target->removeEventFilter(this);
        // Let the backing store finish its first frame before removing the splash.
        QTimer::singleShot(0, this, [this] { close(); });
    }
    return QWidget::eventFilter(watched, event);
}

void StartupSplash::finishWhenReady(QWidget &window)
{
    if (m_handoffQueued) return;
    m_handoffQueued = true;
    m_target = &window;
    if (!isVisible()) show();
    if (windowHandle() && window.windowHandle()) {
        windowHandle()->setTransientParent(window.windowHandle());
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "windows") {
            // Preserve native stacking until the main window's first frame is ready.
            SetWindowLongPtr(reinterpret_cast<HWND>(winId()), GWLP_HWNDPARENT,
                             static_cast<LONG_PTR>(window.winId()));
        }
#endif
    }
    m_status->setText("Opening your workspace");
    connect(&window, &QObject::destroyed, this, [this] { close(); });
    const int remaining = static_cast<int>(std::max<qint64>(0, MinimumDisplayMs - m_displayTime.elapsed()));
    QTimer::singleShot(remaining, Qt::PreciseTimer, this, [this] {
        if (m_target) {
            m_target->installEventFilter(this);
            m_target->show();
            m_target->update();
        } else {
            close();
        }
    });
}
