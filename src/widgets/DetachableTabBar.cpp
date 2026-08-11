/*
    SPDX-FileCopyrightText: 2018 Tomaz Canabrava <tcanabrava@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DetachableTabBar.h"
#include "KonsoleSettings.h"
#include "widgets/ViewContainer.h"

#include <QApplication>
#include <QDateTime>
#include <QMimeData>
#include <QMouseEvent>

#include <KAcceleratorManager>
#include <KColorScheme>
#include <KLocalizedString>

#include <QColor>
#include <QPainter>

#include <cmath>

namespace Konsole
{
namespace
{
QColor blendedColor(const QColor &background, const QColor &foreground, qreal foregroundOpacity)
{
    const qreal backgroundOpacity = 1.0 - foregroundOpacity;
    return QColor::fromRgbF((background.redF() * backgroundOpacity) + (foreground.redF() * foregroundOpacity),
                            (background.greenF() * backgroundOpacity) + (foreground.greenF() * foregroundOpacity),
                            (background.blueF() * backgroundOpacity) + (foreground.blueF() * foregroundOpacity),
                            1.0);
}

QColor runningPulseColor(const QColor &baseColor, const QColor &highlightColor, bool selected)
{
    constexpr qint64 PulseDurationMs = 1400;
    constexpr qreal FullTurn = 6.2831853071795864769;

    const qreal progress = static_cast<qreal>(QDateTime::currentMSecsSinceEpoch() % PulseDurationMs) / PulseDurationMs;
    const qreal wave = (std::sin(progress * FullTurn) + 1.0) / 2.0;
    const qreal minimumOpacity = selected ? 0.50 : 0.34;
    const qreal maximumOpacity = selected ? 0.88 : 0.72;
    return blendedColor(baseColor, highlightColor, minimumOpacity + ((maximumOpacity - minimumOpacity) * wave));
}

QString terminalTabStatusText(TerminalTabStatus status)
{
    switch (status) {
    case TerminalTabStatus::ForegroundProcess:
        return i18nc("@info:tooltip", "Foreground process running");
    case TerminalTabStatus::AgentIdle:
        return i18nc("@info:tooltip", "Agent idle");
    case TerminalTabStatus::AgentRunning:
        return i18nc("@info:tooltip", "Agent working");
    case TerminalTabStatus::NeedsInput:
        return i18nc("@info:tooltip", "Agent needs input");
    case TerminalTabStatus::None:
        return {};
    }

    return {};
}
}

DetachableTabBar::DetachableTabBar(QWidget *parent)
    : QTabBar(parent)
    , dragType(DragType::NONE)
    , _originalCursor(cursor())
    , tabId(-1)
    , _activityColor(QColor::Invalid)
    , _statusAnimationTimer(this)
{
    setAcceptDrops(true);
    setElideMode(Qt::TextElideMode::ElideLeft);
    KAcceleratorManager::setNoAccel(this);

    _statusAnimationTimer.setInterval(80);
    connect(&_statusAnimationTimer, &QTimer::timeout, this, [this]() {
        update();
    });
}

void DetachableTabBar::setColor(int idx, const QColor &color)
{
    DetachableTabData data = tabData(idx).value<DetachableTabData>();
    if (data.color != color) {
        data.color = color;
        setDetachableTabData(idx, data);
        update(tabRect(idx));
    }
}

void DetachableTabBar::setActivityColor(int idx, const QColor &color)
{
    Q_UNUSED(idx)
    _activityColor = color;
    update();
}

void DetachableTabBar::removeColor(int idx)
{
    DetachableTabData data = tabData(idx).value<DetachableTabData>();
    if (data.color.isValid()) {
        data.color = QColor();
        setDetachableTabData(idx, data);
        update(tabRect(idx));
    }
}

void DetachableTabBar::setProgress(int idx, const std::optional<int> &progress)
{
    DetachableTabData data = tabData(idx).value<DetachableTabData>();
    if (data.progress != progress) {
        data.progress = progress;
        setDetachableTabData(idx, data);
        update(tabRect(idx));
    }
}

void DetachableTabBar::setStatus(int idx, TerminalTabStatus status)
{
    if (idx < 0 || idx >= count()) {
        return;
    }

    DetachableTabData data = tabData(idx).value<DetachableTabData>();
    if (data.status == status) {
        return;
    }

    data.status = status;
    setDetachableTabData(idx, data);
    updateTabToolTip(idx, data);
    updateStatusAnimationTimer();
    update(tabRect(idx));
}

TerminalTabStatus DetachableTabBar::status(int idx) const
{
    if (idx < 0 || idx >= count()) {
        return TerminalTabStatus::None;
    }

    return tabData(idx).value<DetachableTabData>().status;
}

void DetachableTabBar::setBaseToolTip(int idx, const QString &toolTip)
{
    if (idx < 0 || idx >= count()) {
        return;
    }

    DetachableTabData data = tabData(idx).value<DetachableTabData>();
    if (data.baseToolTip == toolTip) {
        return;
    }

    data.baseToolTip = toolTip;
    setDetachableTabData(idx, data);
    updateTabToolTip(idx, data);
}

void DetachableTabBar::setDetachableTabData(int idx, const DetachableTabData &data)
{
    if ((data.color.isValid() && data.color.alpha() > 0) || data.progress.has_value() || data.status != TerminalTabStatus::None
        || !data.baseToolTip.isEmpty()) {
        setTabData(idx, QVariant::fromValue(data));
    } else {
        setTabData(idx, QVariant());
    }
}

void DetachableTabBar::updateStatusAnimationTimer()
{
    bool hasRunningAgent = false;
    for (int tabIndex = 0; tabIndex < count(); ++tabIndex) {
        if (status(tabIndex) == TerminalTabStatus::AgentRunning) {
            hasRunningAgent = true;
            break;
        }
    }

    if (hasRunningAgent) {
        if (!_statusAnimationTimer.isActive()) {
            _statusAnimationTimer.start();
        }
    } else {
        _statusAnimationTimer.stop();
    }
}

void DetachableTabBar::updateTabToolTip(int idx, const DetachableTabData &data)
{
    QString toolTip = data.baseToolTip;
    const QString statusText = terminalTabStatusText(data.status);
    if (!statusText.isEmpty()) {
        if (!toolTip.isEmpty()) {
            toolTip += QLatin1Char('\n');
        }
        toolTip += statusText;
    }
    QTabBar::setTabToolTip(idx, toolTip);
}

void DetachableTabBar::mousePressEvent(QMouseEvent *event)
{
    QTabBar::mousePressEvent(event);
    _containers = window()->findChildren<Konsole::TabbedViewContainer *>();
}

void DetachableTabBar::mouseMoveEvent(QMouseEvent *event)
{
    QTabBar::mouseMoveEvent(event);
    if (dragType != DragType::NONE && contentsRect().adjusted(-30, -30, 30, 30).contains(event->pos())) {
        dragType = DragType::NONE;
        setCursor(_originalCursor);
    }
}

void DetachableTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    // Block signals on middle mouse release, to prevent QTabBar's own handling
    // closing the tab, which is not configurable.
    const bool signalsWereBlocked = blockSignals(event->button() == Qt::MiddleButton);
    QTabBar::mouseReleaseEvent(event);
    blockSignals(signalsWereBlocked);

    switch (event->button()) {
    case Qt::MiddleButton:
        tabId = tabAt(event->pos());
        if (tabId == -1) {
            Q_EMIT newTabRequest();
        } else if (KonsoleSettings::closeTabOnMiddleMouseButton()) {
            Q_EMIT closeTab(tabId);
        }
        break;
    case Qt::LeftButton:
        _containers = window()->findChildren<Konsole::TabbedViewContainer *>();
        break;
    default:
        break;
    }

    setCursor(_originalCursor);

    dragType = DragType::NONE;
}

void DetachableTabBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QTabBar::mouseDoubleClickEvent(event);
    }
}

void DetachableTabBar::dragEnterEvent(QDragEnterEvent *event)
{
    const auto dragId = QStringLiteral("konsole/terminal_display");
    if (!event->mimeData()->hasFormat(dragId)) {
        return;
    }
    auto other_pid = event->mimeData()->data(dragId).toInt();
    // don't accept the drop if it's another instance of konsole
    if (qApp->applicationPid() != other_pid) {
        return;
    }
    event->accept();
}

void DetachableTabBar::dragMoveEvent(QDragMoveEvent *event)
{
    int tabIdx = tabAt(event->position().toPoint());
    if (tabIdx != -1) {
        setCurrentIndex(tabIdx);
    }
}

void DetachableTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);
    if (!event->isAccepted()) {
        return; // Reduces repainting
    }

    QPainter painter(this);
    painter.setPen(Qt::NoPen);

    const int activeTabIndex = currentIndex();
    if (activeTabIndex >= 0) {
        QRect activeTabRect = tabRect(activeTabIndex);
        activeTabRect.setLeft(activeTabRect.left() + 1);
        activeTabRect.setRight(activeTabRect.right() - 1);
        activeTabRect.setHeight(2);

        QColor activeAccent = palette().highlight().color();
        activeAccent.setAlpha(230);
        painter.setBrush(activeAccent);
        painter.drawRect(activeTabRect);
    }

    for (int tabIndex = 0; tabIndex < count(); tabIndex++) {
        const QVariant data = tabData(tabIndex);
        if (!data.isValid() || data.isNull()) {
            continue;
        }

        const DetachableTabData tabData = data.value<DetachableTabData>();

        const bool colorValid = tabData.color.isValid() && tabData.color.alpha() > 0;

        if (!colorValid && !tabData.progress.has_value() && tabData.status == TerminalTabStatus::None) {
            continue;
        }

        const QColor color = colorValid ? tabData.color : palette().highlight().color();

        if (colorValid || tabData.progress.has_value()) {
            painter.setBrush(color);
            QRect colorRect = tabRect(tabIndex);
            colorRect.setTop(painter.fontMetrics().height() + 6); // Color bar top position consider a height the font and fixed spacing of 6px
            colorRect.setHeight(4);
            colorRect.setLeft(colorRect.left() + 6);
            colorRect.setWidth(colorRect.width() - 6);

            // Draw progress, if any, ontop of a faint bar.
            if (tabData.progress.has_value()) {
                painter.setOpacity(0.3);
                painter.drawRect(colorRect);
                painter.setOpacity(1.0);

                colorRect.setWidth(colorRect.width() * tabData.progress.value() / 100.0);
                painter.drawRect(colorRect);
            } else {
                painter.drawRect(colorRect);
            }
        }

        if (tabData.status != TerminalTabStatus::None) {
            const QRect tabRectangle = tabRect(tabIndex);
            QRect statusRect(tabRectangle.right() - 17, tabRectangle.bottom() - 5, 14, 3);
            QColor statusColor = blendedColor(palette().color(QPalette::PlaceholderText), palette().color(QPalette::Text), 0.48);

            switch (tabData.status) {
            case TerminalTabStatus::ForegroundProcess:
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(statusColor, 1.0));
                painter.drawRoundedRect(QRectF(statusRect).adjusted(0.5, 0.5, -0.5, -0.5), 1.0, 1.0);
                break;
            case TerminalTabStatus::AgentIdle:
                statusColor = blendedColor(statusColor, palette().highlight().color(), 0.52);
                painter.setPen(Qt::NoPen);
                painter.setBrush(statusColor);
                painter.drawRoundedRect(statusRect, 1.0, 1.0);
                break;
            case TerminalTabStatus::AgentRunning:
                statusColor = runningPulseColor(statusColor, palette().highlight().color(), tabIndex == currentIndex());
                painter.setPen(Qt::NoPen);
                painter.setBrush(statusColor);
                painter.drawRoundedRect(statusRect, 1.0, 1.0);
                break;
            case TerminalTabStatus::NeedsInput: {
                const KColorScheme viewScheme(palette().currentColorGroup(), KColorScheme::View);
                statusColor = viewScheme.foreground(KColorScheme::NeutralText).color();
                painter.setPen(Qt::NoPen);
                painter.setBrush(statusColor);
                painter.drawRoundedRect(statusRect, 1.0, 1.0);
                break;
            }
            case TerminalTabStatus::None:
                break;
            }
        }
    }
}

void DetachableTabBar::tabRemoved(int index)
{
    QTabBar::tabRemoved(index);
    updateStatusAnimationTimer();
}
}

#include "moc_DetachableTabBar.cpp"
