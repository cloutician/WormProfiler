#include "DesktopStyle.h"
#include "DesktopIcons.h"

#include <QApplication>
#include <QCheckBox>
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QRadioButton>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleOptionSlider>

namespace {
const QColor accent("#147d6b");

class DesktopControlStyle final : public QProxyStyle
{
public:
    DesktopControlStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    void polish(QWidget *widget) override
    {
        QProxyStyle::polish(widget);
        if (qobject_cast<QSlider *>(widget) || qobject_cast<QCheckBox *>(widget)
            || qobject_cast<QRadioButton *>(widget)) widget->setAttribute(Qt::WA_Hover);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_IndicatorWidth: case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth: case PM_ExclusiveIndicatorHeight: return 20;
        case PM_SliderLength: return 22;
        case PM_SliderThickness: return 28;
        case PM_SliderControlThickness: return 22;
        case PM_SmallIconSize: return 18;
        case PM_ButtonIconSize: return 18;
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == SH_DialogButtonBox_ButtonsHaveIcons) return false;
        if (hint == SH_Slider_AbsoluteSetButtons) return Qt::LeftButton;
        if (hint == SH_Slider_PageSetButtons) return Qt::MiddleButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option, const QWidget *widget) const override
    {
        using S = DesktopIcons::Symbol;
        switch (icon) {
        case SP_FileIcon: return DesktopIcons::icon(S::Image);
        case SP_DirIcon: case SP_DirOpenIcon: return DesktopIcons::icon(S::FolderOpen);
        case SP_DialogSaveButton: return DesktopIcons::icon(S::Save);
        case SP_ArrowBack: case SP_ArrowLeft: return DesktopIcons::icon(S::Previous);
        case SP_ArrowForward: case SP_ArrowRight: return DesktopIcons::icon(S::Next);
        case SP_ArrowUp: return DesktopIcons::icon(S::ChevronUp);
        case SP_ArrowDown: return DesktopIcons::icon(S::ChevronDown);
        case SP_TitleBarMaxButton: return DesktopIcons::icon(S::Fit);
        case SP_FileDialogDetailedView: return DesktopIcons::icon(S::ActualSize);
        case SP_FileDialogContentsView: return DesktopIcons::icon(S::Layers);
        case SP_FileDialogNewFolder: return DesktopIcons::icon(S::NewReview);
        case SP_BrowserReload: return DesktopIcons::icon(S::Reset);
        case SP_DialogApplyButton: case SP_DialogOkButton: return DesktopIcons::icon(S::Check);
        case SP_DialogCancelButton: case SP_TitleBarCloseButton:
        case SP_LineEditClearButton: return DesktopIcons::icon(S::Close);
        case SP_MessageBoxWarning: return DesktopIcons::icon(S::Warning);
        case SP_MessageBoxInformation: case SP_MessageBoxQuestion: return DesktopIcons::icon(S::Info);
        case SP_MessageBoxCritical: return DesktopIcons::icon(S::Rejected);
        case SP_ComputerIcon: return DesktopIcons::icon(S::Application);
        default: return QProxyStyle::standardIcon(icon, option, widget);
        }
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const bool enabled = option->state & State_Enabled;
        const bool checked = option->state & (State_On | State_NoChange);
        const bool hover = enabled && (option->state & State_MouseOver);
        const bool pressed = enabled && (option->state & State_Sunken);
        const bool radio = element == PE_IndicatorRadioButton;
        const QRectF box = QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
        const QColor fill = !enabled ? (checked ? QColor("#bcc4c3") : QColor("#f0f1f2"))
                           : checked ? (pressed ? QColor("#0d6556") : hover ? QColor("#198975") : accent)
                           : pressed ? QColor("#e5e8e9") : hover ? QColor("#edf0f0") : QColor("#ffffff");
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(checked ? fill : enabled ? QColor("#777f85") : QColor("#c7cbce"), 1.2));
        painter->setBrush(fill);
        if (radio) painter->drawEllipse(box); else painter->drawRoundedRect(box, 4, 4);
        if (checked) {
            if (radio) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(Qt::white);
                painter->drawEllipse(box.center(), 3.4, 3.4);
            } else if (option->state & State_NoChange) {
                painter->setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
                painter->drawLine(box.center() - QPointF(4, 0), box.center() + QPointF(4, 0));
            } else {
                DesktopIcons::icon(DesktopIcons::Symbol::Check, true).paint(painter, box.toRect().adjusted(1, 1, -1, -1));
            }
        }
        painter->restore();
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget) const override
    {
        if (control != CC_Slider) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!slider) return;
        const QRect handle = subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
        const QRect groove = subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
        const bool horizontal = slider->orientation == Qt::Horizontal;
        const bool enabled = slider->state & State_Enabled;
        const bool hover = enabled && (slider->state & State_MouseOver) && (slider->activeSubControls & SC_SliderHandle);
        const bool pressed = enabled && (slider->state & State_Sunken);
        const QPointF center = handle.center();
        const QPointF start = horizontal ? QPointF(groove.left() + handle.width() / 2., center.y())
                                        : QPointF(center.x(), groove.top() + handle.height() / 2.);
        const QPointF end = horizontal ? QPointF(groove.right() - handle.width() / 2., center.y())
                                      : QPointF(center.x(), groove.bottom() - handle.height() / 2.);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(enabled ? QColor("#bdc3c7") : QColor("#d9dddf"), 4, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(start, end);
        painter->setPen(QPen(enabled ? accent : QColor("#b8c4c0"), 4, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(slider->upsideDown ? end : start, center);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, enabled ? 18 : 8));
        painter->drawEllipse(center + QPointF(0, 1), 10.5, 10.5);
        painter->setPen(QPen(enabled ? QColor("#cdd2d5") : QColor("#dce0e2"), 1));
        painter->setBrush(enabled ? Qt::white : QColor("#f6f7f7"));
        painter->drawEllipse(center, 10, 10);
        painter->setPen(Qt::NoPen);
        painter->setBrush(enabled ? accent : QColor("#b8c4c0"));
        const qreal radius = pressed ? 4 : hover ? 6 : 5;
        painter->drawEllipse(center, radius, radius);
        if (enabled && (slider->state & State_HasFocus)) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(QColor("#23695e"), 1.3));
            painter->drawRoundedRect(QRectF(slider->rect).adjusted(0.7, 0.7, -0.7, -0.7), 4, 4);
        }
        painter->restore();
        if (slider->subControls & SC_SliderTickmarks) {
            QStyleOptionSlider ticks(*slider);
            ticks.subControls = SC_SliderTickmarks;
            QProxyStyle::drawComplexControl(control, &ticks, painter, widget);
        }
    }
};
}

void configureDesktopApplication(QApplication &application)
{
    application.setOrganizationName("WormProfiler");
    application.setApplicationName("WormProfiler");
    application.setWindowIcon(DesktopIcons::icon(DesktopIcons::Symbol::Application));
    application.setStyle(new DesktopControlStyle);
#ifdef Q_OS_WIN
    QFont font;
    font.setFamilies({"Segoe UI Variable Text", "Segoe UI"});
    font.setPointSize(10);
    application.setFont(font);
#endif
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f4f5f6"));
    palette.setColor(QPalette::WindowText, QColor("#24292e"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f7f8f9"));
    palette.setColor(QPalette::Text, QColor("#24292e"));
    palette.setColor(QPalette::Button, QColor("#ffffff"));
    palette.setColor(QPalette::ButtonText, QColor("#24292e"));
    palette.setColor(QPalette::Highlight, QColor("#e0f0eb"));
    palette.setColor(QPalette::HighlightedText, QColor("#125e51"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#939a9f"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#939a9f"));
    application.setPalette(palette);
    application.setStyleSheet(R"(
        QMainWindow, QDialog { background: #f4f5f6; }
        QToolTip { background: #ffffff; color: #24292e; border: 1px solid #d6dadd; padding: 6px 9px; }
        QMenuBar { background: #f4f5f6; border: 0; padding: 3px 8px; }
        QMenuBar::item { padding: 5px 10px; border-radius: 4px; }
        QMenuBar::item:selected { background: #e5e8ea; }
        QMenu { background: #fcfcfd; border: 1px solid #d9dde0; border-radius: 6px; padding: 5px; }
        QMenu::item { padding: 7px 28px 7px 10px; border-radius: 4px; }
        QMenu::item:selected { background: #e7eeeb; color: #24292e; }
        QMenu::item:disabled { color: #939a9f; }
        QMenu::separator { height: 1px; background: #e1e4e6; margin: 5px; }
        QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #e0e4e6; spacing: 4px; padding: 6px 10px; }
        QToolBar::separator { background: #e0e4e6; width: 1px; margin: 6px; }
        QToolButton { padding: 6px; min-width: 20px; min-height: 20px; border: 1px solid transparent; border-radius: 4px; }
        QToolButton:hover { background: #edf0f1; }
        QToolButton:pressed { background: #e2e7e8; }
        QToolButton:checked { background: #e0f0eb; color: #125e51; }
        QToolButton:focus { border-color: #387c6c; }
        QToolButton:disabled { color: #939a9f; }
        QToolButton[popupMode="2"] { padding-right: 20px; }
        QToolButton::menu-indicator { image: url(:/icons/chevron-down.svg); width: 10px; height: 10px; subcontrol-position: right center; right: 5px; }
        QLineEdit#imageSearch QToolButton { min-width: 0; min-height: 0; padding: 0; margin: 0; border: 0; background: transparent; }
        QTabWidget::pane { border: 0; border-top: 1px solid #e0e4e6; }
        QTabBar { background: #f4f5f6; }
        QTabBar::tab { background: transparent; color: #626b72; padding: 10px 20px; margin: 2px 2px 0 0; border: 0; border-bottom: 3px solid transparent; }
        QTabBar::tab:selected { background: #ffffff; border-top-left-radius: 6px; border-top-right-radius: 6px; border-bottom-color: #147d6b; color: #24292e; }
        QTabBar::tab:hover:!selected { background: #e8ebed; border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QWidget#inspector, QWidget#reviewInspector, QWidget#imageBrowser, QWidget#reviewBrowser, QWidget#wormProfileBrowser, QWidget#wormProfileInspector { background: #f9fafb; }
        QLabel#panelTitle { font-size: 14px; font-weight: 600; color: #24292e; padding: 4px 0; }
        QLabel#sectionTitle { font-size: 12px; font-weight: 600; color: #4e5b63; padding-top: 8px; }
        QLabel#secondaryText { color: #737d84; font-size: 11px; }
        QLabel#imageHeader { font-size: 12px; font-weight: 600; padding: 8px 10px; background: #ffffff; }
        QLabel#imageMetadata { color: #657078; font-size: 11px; padding: 6px 9px; background: #f9fafb; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #ffffff; color: #24292e; padding: 5px 7px; border: 1px solid #d5dadd; border-bottom-color: #aab2b8; border-radius: 4px; selection-background-color: #cfe7de; selection-color: #24292e; min-height: 20px; }
        QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover { background: #fdfefe; border-color: #aeb8be; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-bottom: 2px solid #147d6b; padding-bottom: 4px; }
        QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { background: #f0f2f3; color: #939a9f; border-color: #e0e4e6; }
        QSpinBox, QDoubleSpinBox { padding-right: 20px; }
        QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px; border: 0; border-top-right-radius: 3px; background: transparent; margin: 1px 1px 0 0; }
        QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; border: 0; border-bottom-right-radius: 3px; background: transparent; margin: 0 1px 1px 0; }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: #e7efec; }
        QSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDoubleSpinBox::up-button:pressed, QDoubleSpinBox::down-button:pressed { background: #d7e7e0; }
        QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/icons/chevron-up.svg); width: 10px; height: 10px; }
        QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 10px; height: 10px; }
        QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:off { image: url(:/icons/chevron-up-disabled.svg); }
        QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:off { image: url(:/icons/chevron-down-disabled.svg); }
        QComboBox { padding-right: 28px; }
        QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: 26px; border: 0; }
        QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
        QComboBox::down-arrow:disabled { image: url(:/icons/chevron-down-disabled.svg); }
        QComboBox QAbstractItemView { background: #ffffff; border: 1px solid #d5dadd; padding: 4px; selection-background-color: #e0f0eb; selection-color: #125e51; outline: none; }
        QCheckBox, QRadioButton { spacing: 8px; padding: 4px 0; }
        QCheckBox:disabled, QRadioButton:disabled { color: #939a9f; }
        QCheckBox:focus, QRadioButton:focus { background: #e9f2ee; border-radius: 4px; }
        QSlider:horizontal { min-height: 28px; }
        QSlider:vertical { min-width: 28px; }
        QPushButton { background: #ffffff; color: #24292e; border: 1px solid #d5dadd; border-bottom-color: #b7bfc4; border-radius: 4px; padding: 6px 10px; min-height: 18px; }
        QPushButton:hover { background: #f4f7f6; border-color: #b0beb8; }
        QPushButton:pressed { background: #e8efec; color: #4d5b55; border-bottom-color: #c9d1cd; }
        QPushButton:focus { border: 1px solid #247362; }
        QPushButton:disabled { color: #939a9f; border-color: #e0e4e6; background: #f0f2f3; }
        QPushButton[primary="true"], QPushButton:default { background: #147d6b; color: #ffffff; border-color: #147d6b; border-bottom-color: #0b5c4e; }
        QPushButton[primary="true"]:hover, QPushButton:default:hover { background: #116e5e; border-color: #116e5e; }
        QPushButton[primary="true"]:pressed, QPushButton:default:pressed { background: #1e8876; }
        QPushButton[primary="true"]:focus, QPushButton:default:focus { border: 2px solid #163f36; padding: 5px 9px; }
        QPushButton[primary="true"]:disabled, QPushButton:default:disabled { background: #b8ccc5; border-color: #b8ccc5; color: #f6faf8; }
        QPushButton[iconOnly="true"] { padding: 0; }
        QListWidget { border: 0; background: #f9fafb; outline: none; }
        QListWidget::item { padding: 5px; margin: 2px 0; border: 1px solid transparent; border-radius: 4px; }
        QListWidget::item:selected { background: #e0f0eb; color: #125e51; border-color: #c8e2d8; }
        QListWidget::item:hover:!selected { background: #edf0f2; }
        QListWidget::item:focus { border-color: #669b89; }
        QSplitter::handle { background: #e0e4e6; width: 1px; height: 1px; }
        QSplitter::handle:hover { background: #91b8a8; }
        QScrollArea { border: 0; background: transparent; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 1px; }
        QScrollBar::handle:vertical { background: #bfc6ca; min-height: 32px; border: 2px solid #f4f5f6; border-radius: 4px; }
        QScrollBar::handle:vertical:hover { background: #8e9b9f; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: 0; background: none; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 1px 2px; }
        QScrollBar::handle:horizontal { background: #bfc6ca; min-width: 32px; border: 2px solid #f4f5f6; border-radius: 4px; }
        QScrollBar::handle:horizontal:hover { background: #8e9b9f; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; border: 0; background: none; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }
        QStatusBar { background: #f4f5f6; border-top: 1px solid #e0e4e6; color: #657078; }
        QStatusBar::item { border: 0; }
        QProgressBar { border: 0; border-radius: 2px; background: #dfe5e3; max-height: 4px; }
        QProgressBar::chunk { background: #147d6b; border-radius: 2px; }
        QDateEdit, QPlainTextEdit { background: #ffffff; color: #24292e; padding: 5px 7px; border: 1px solid #d5dadd; border-bottom-color: #aab2b8; border-radius: 4px; selection-background-color: #cfe7de; selection-color: #24292e; }
        QDateEdit { min-height: 20px; padding-right: 30px; }
        QDateEdit:hover, QPlainTextEdit:hover { border-color: #aeb8be; }
        QDateEdit:focus, QPlainTextEdit:focus { border-bottom: 2px solid #147d6b; padding-bottom: 4px; }
        QDateEdit:disabled, QPlainTextEdit:disabled { background: #f0f2f3; color: #939a9f; border-color: #e0e4e6; }
        QDateEdit::drop-down { subcontrol-origin: border; subcontrol-position: top right; width: 30px; border: 0; }
        QDateEdit::down-arrow { image: url(:/icons/calendar-days.svg); width: 16px; height: 16px; }
        QCalendarWidget { background: #ffffff; border: 1px solid #d5dadd; }
        QCalendarWidget QWidget#qt_calendar_navigationbar { background: #f4f5f6; }
        QCalendarWidget QToolButton { color: #24292e; }
        QCalendarWidget QAbstractItemView { background: #ffffff; color: #24292e; selection-background-color: #147d6b; selection-color: #ffffff; outline: none; border: 0; }
        QTableWidget { background: #ffffff; alternate-background-color: #f8fafb; border: 0; gridline-color: #e6eaed; selection-background-color: #e0f0eb; selection-color: #125e51; }
        QTableWidget::item { padding: 5px 8px; }
        QHeaderView::section { background: #f1f4f5; color: #4e5b63; padding: 7px 8px; border: 0; border-right: 1px solid #e0e4e6; border-bottom: 1px solid #d5dadd; font-weight: 600; }
    )");
}
