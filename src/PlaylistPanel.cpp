#include "PlaylistPanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int kPadding = 10;
constexpr int kToolbarHeight = 40;
constexpr int kToggleWidth = 30;
constexpr int kToggleHeight = 28;
constexpr int kToggleGap = 6;

QColor BlendColors(const QColor& base, const QColor& tint, qreal amount) {
    return QColor::fromRgbF(
        base.redF() + (tint.redF() - base.redF()) * amount,
        base.greenF() + (tint.greenF() - base.greenF()) * amount,
        base.blueF() + (tint.blueF() - base.blueF()) * amount,
        1.0);
}

}  // namespace

class PlaylistPanel::ToggleButton : public QAbstractButton {
public:
    enum class Kind { AutoAdvance, RepeatOne, RepeatAll };

    ToggleButton(Kind kind, const QString& tooltip, QWidget* parent)
        : QAbstractButton(parent), kind_(kind) {
        setToolTip(tooltip);
        setText(tooltip);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover, true);
        setAttribute(Qt::WA_InputMethodEnabled, false);
    }

    void SetCheckedState(bool checked) {
        if (checked_ != checked) {
            checked_ = checked;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const qreal w = static_cast<qreal>(width());
        const qreal h = static_cast<qreal>(height());
        const qreal cx = w * 0.5;
        const qreal cy = h * 0.5;

        const bool hovered = underMouse();
        const bool pressed = isDown();
        const bool focused = hasFocus();

        const QColor windowBg(255, 255, 255);
        const QColor textColor(30, 30, 30);
        const QColor mutedColor(120, 120, 120);
        const QColor accent(0, 120, 215);
        const QColor activeStroke(0, 102, 204);

        painter.fillRect(rect(), windowBg);

        const QRectF pillRect(1.0, 1.0, w - 2.0, h - 2.0);
        if (checked_) {
            const qreal fillAmount = pressed ? 0.28 : hovered ? 0.22 : 0.14;
            const int borderAlpha = (hovered || pressed) ? 178 : 115;
            painter.setPen(QPen(QColor(activeStroke.red(), activeStroke.green(), activeStroke.blue(), borderAlpha), 1.0));
            painter.setBrush(BlendColors(windowBg, accent, fillAmount));
            painter.drawRoundedRect(pillRect, 4.0, 4.0);
        } else if (pressed || hovered) {
            const qreal fillAmount = pressed ? 0.14 : 0.08;
            painter.setPen(QPen(BlendColors(windowBg, textColor, 0.18), 1.0));
            painter.setBrush(BlendColors(windowBg, textColor, fillAmount));
            painter.drawRoundedRect(pillRect, 4.0, 4.0);
        }

        const QColor iconColor = checked_ ? activeStroke : ((hovered || pressed) ? textColor : mutedColor);

        if (kind_ == Kind::AutoAdvance) {
            // Play-next icon (triangle + vertical bar).
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconColor);
            QPainterPath triangle;
            triangle.moveTo(cx - 5.5, cy - 5.0);
            triangle.lineTo(cx + 1.8, cy);
            triangle.lineTo(cx - 5.5, cy + 5.0);
            triangle.closeSubpath();
            painter.drawPath(triangle);
            painter.drawRoundedRect(QRectF(cx + 3.5, cy - 4.8, 2.0, 9.6), 0.8, 0.8);
        } else {
            // Repeat loop: top arc/bar pointing right, bottom arc/bar pointing left.
            const qreal stroke = 1.6;
            const auto drawLoop = [&](qreal dir) {
                QPainterPath path;
                path.moveTo(cx - dir * 6.5, cy - dir * 1.0);
                path.lineTo(cx - dir * 6.5, cy - dir * 2.8);
                path.quadTo(cx - dir * 6.5, cy - dir * 4.8, cx - dir * 4.5, cy - dir * 4.8);
                path.lineTo(cx + dir * 4.8, cy - dir * 4.8);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(iconColor, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);

                const qreal tipX = cx + dir * 6.8;
                const qreal baseX = cx + dir * 3.4;
                const qreal tipY = cy - dir * 4.8;
                QPainterPath arrow;
                arrow.moveTo(baseX, tipY - 2.6);
                arrow.lineTo(tipX, tipY);
                arrow.lineTo(baseX, tipY + 2.6);
                arrow.closeSubpath();
                painter.setPen(Qt::NoPen);
                painter.setBrush(iconColor);
                painter.drawPath(arrow);
            };
            drawLoop(1.0);
            drawLoop(-1.0);

            if (kind_ == Kind::RepeatOne) {
                QFont badgeFont = painter.font();
                badgeFont.setPixelSize(9);
                badgeFont.setBold(true);
                painter.setFont(badgeFont);
                painter.setPen(iconColor);
                painter.drawText(QRectF(cx - 4.0, cy - 5.0, 8.0, 10.0), Qt::AlignCenter, QStringLiteral("1"));
            }
        }

        if (focused) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(accent.red(), accent.green(), accent.blue(), 216), 1.0));
            painter.drawRoundedRect(QRectF(2.5, 2.5, w - 5.0, h - 5.0), 3.0, 3.0);
        }
    }

private:
    Kind kind_;
    bool checked_ = false;
};

PlaylistPanel::PlaylistPanel(Callbacks callbacks, QWidget* parent)
    : QWidget(parent), callbacks_(std::move(callbacks)) {
#if defined(Q_OS_WIN)
    // Needed on Windows so the panel stacks above mpv's native video HWND in fullscreen mode.
    setAttribute(Qt::WA_NativeWindow, true);
#endif
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setAcceptDrops(true);
    setAttribute(Qt::WA_InputMethodEnabled, false);

    autoAdvanceButton_ = new ToggleButton(ToggleButton::Kind::AutoAdvance, QStringLiteral("다음 파일 자동 재생"), this);
    repeatOneButton_ = new ToggleButton(ToggleButton::Kind::RepeatOne, QStringLiteral("한 개 반복"), this);
    repeatAllButton_ = new ToggleButton(ToggleButton::Kind::RepeatAll, QStringLiteral("전체 목록 반복"), this);

    connect(autoAdvanceButton_, &QAbstractButton::clicked, this, [this] {
        if (callbacks_.onToggleAutoAdvance) {
            callbacks_.onToggleAutoAdvance();
        }
    });
    connect(repeatOneButton_, &QAbstractButton::clicked, this, [this] {
        if (callbacks_.onToggleRepeatOne) {
            callbacks_.onToggleRepeatOne();
        }
    });
    connect(repeatAllButton_, &QAbstractButton::clicked, this, [this] {
        if (callbacks_.onToggleRepeatAll) {
            callbacks_.onToggleRepeatAll();
        }
    });

    countLabel_ = new QLabel(this);
    countLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    list_ = new QListWidget(this);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAttribute(Qt::WA_InputMethodEnabled, false);
    list_->installEventFilter(this);

    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const int row = list_->row(item);
        if (row >= 0 && static_cast<qsizetype>(row) < files_.size() && callbacks_.onPlayItem) {
            callbacks_.onPlayItem(static_cast<size_t>(row));
        }
    });

    emptyLabel_ = new QLabel(QStringLiteral("재생 목록이 비어 있습니다.\n파일을 끌어다 놓으면 추가됩니다."), this);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);

    for (QWidget* w : {static_cast<QWidget*>(autoAdvanceButton_),
                       static_cast<QWidget*>(repeatOneButton_),
                       static_cast<QWidget*>(repeatAllButton_)}) {
        w->installEventFilter(this);
    }

    ApplyTheme();
    RefreshHeaderCount();
    RefreshOptionButtons();
    RefreshList();
    QWidget::hide();
}

void PlaylistPanel::SetOpen(bool open) {
    if (open == open_) {
        if (open) {
            FocusDefaultControl();
        }
        return;
    }
    open_ = open;
    if (open) {
        QWidget::show();
        raise();
        FocusDefaultControl();
    } else {
        QWidget::hide();
    }
}

bool PlaylistPanel::HasPanelFocus() const {
    const QWidget* focused = QApplication::focusWidget();
    return focused && (focused == this || isAncestorOf(focused));
}

void PlaylistPanel::FocusDefaultControl() {
    if (files_.isEmpty()) {
        autoAdvanceButton_->setFocus();
    } else {
        list_->setFocus();
    }
}

void PlaylistPanel::SetPanelWidth(int width) {
    const int clamped = std::clamp(width, kMinPanelWidth, kMaxPanelWidth);
    if (panelWidth_ != clamped) {
        panelWidth_ = clamped;
        if (callbacks_.onPanelWidthChanged) {
            callbacks_.onPanelWidthChanged(panelWidth_);
        }
    }
}

void PlaylistPanel::SetOverlayMode(bool overlay) {
    if (overlayMode_ == overlay) {
        return;
    }
    overlayMode_ = overlay;
    ApplyTheme();
    update();
}

void PlaylistPanel::ApplyTheme() {
    // Unified clean white background for toolbar, count label, list view, and empty state.
    countLabel_->setStyleSheet(QStringLiteral("QLabel { background-color: #ffffff; color: #6d6d6d; }"));
    emptyLabel_->setStyleSheet(QStringLiteral("QLabel { background-color: #ffffff; color: #6d6d6d; }"));
    list_->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #ffffff; color: #111111; border: none; outline: none; }"
        "QListWidget::item { padding: 4px 8px; background-color: #ffffff; color: #111111; }"
        "QListWidget::item:selected { background-color: #cce8ff; color: #000000; }"
        "QListWidget::item:hover:!selected { background-color: #e5f3ff; color: #000000; }"));
}

void PlaylistPanel::SetPlaylist(const QStringList& files, size_t currentIndex) {
    const bool sameFiles = (files == files_);
    const size_t previousIndex = currentIndex_;
    files_ = files;
    currentIndex_ = files_.isEmpty() ? 0 : std::min(currentIndex, static_cast<size_t>(files_.size() - 1));

    RefreshHeaderCount();

    if (sameFiles && !files_.isEmpty() && list_->count() == files_.size()) {
        if (previousIndex != currentIndex_) {
            if (previousIndex < static_cast<size_t>(list_->count())) {
                if (QListWidgetItem* prevItem = list_->item(static_cast<int>(previousIndex))) {
                    QFont f = prevItem->font();
                    f.setBold(false);
                    prevItem->setFont(f);
                }
            }
            if (QListWidgetItem* curItem = list_->item(static_cast<int>(currentIndex_))) {
                QFont f = curItem->font();
                f.setBold(true);
                curItem->setFont(f);
            }
        }
        list_->setCurrentRow(static_cast<int>(currentIndex_));
        if (QListWidgetItem* curItem = list_->item(static_cast<int>(currentIndex_))) {
            list_->scrollToItem(curItem);
        }
        return;
    }

    RefreshList();
}

void PlaylistPanel::SetOptions(bool autoAdvance, RepeatMode repeat) {
    autoAdvance_ = autoAdvance;
    repeat_ = repeat;
    RefreshOptionButtons();
}

void PlaylistPanel::RefreshHeaderCount() {
    if (files_.isEmpty()) {
        countLabel_->setText(QStringLiteral("0개"));
    } else {
        countLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex_ + 1).arg(files_.size()));
    }
}

void PlaylistPanel::RefreshOptionButtons() {
    autoAdvanceButton_->SetCheckedState(autoAdvance_);
    repeatOneButton_->SetCheckedState(repeat_ == RepeatMode::One);
    repeatAllButton_->SetCheckedState(repeat_ == RepeatMode::All);
}

void PlaylistPanel::RefreshList() {
    const bool empty = files_.isEmpty();
    list_->setVisible(!empty);
    emptyLabel_->setVisible(empty);

    list_->setUpdatesEnabled(false);
    list_->clear();

    for (qsizetype i = 0; i < files_.size(); ++i) {
        const QString title = QStringLiteral("%1. %2").arg(i + 1).arg(QFileInfo(files_[i]).fileName());
        auto* item = new QListWidgetItem(title, list_);
        item->setToolTip(files_[i]);
        if (static_cast<size_t>(i) == currentIndex_) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }

    if (!empty) {
        list_->setCurrentRow(static_cast<int>(currentIndex_));
        if (QListWidgetItem* curItem = list_->item(static_cast<int>(currentIndex_))) {
            list_->scrollToItem(curItem);
        }
    }
    list_->setUpdatesEnabled(true);
}

void PlaylistPanel::LayoutControls() {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    const int btnY = (kToolbarHeight - kToggleHeight) / 2;
    int x = kPadding;
    autoAdvanceButton_->setGeometry(x, btnY, kToggleWidth, kToggleHeight);
    x += kToggleWidth + kToggleGap;
    repeatOneButton_->setGeometry(x, btnY, kToggleWidth, kToggleHeight);
    x += kToggleWidth + kToggleGap;
    repeatAllButton_->setGeometry(x, btnY, kToggleWidth, kToggleHeight);
    x += kToggleWidth + kToggleGap;

    const int countRight = std::max(x, width() - kPadding);
    countLabel_->setGeometry(x, 0, countRight - x, kToolbarHeight);

    const int listTop = kToolbarHeight + 1;
    const int listHeight = std::max(0, height() - listTop);
    list_->setGeometry(1, listTop, std::max(0, width() - 1), listHeight);

    const int emptyHeight = 48;
    const int emptyY = listTop + std::max(0, (listHeight - emptyHeight) / 2);
    emptyLabel_->setGeometry(kPadding, emptyY, std::max(0, width() - kPadding * 2), emptyHeight);
}

void PlaylistPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    LayoutControls();
}

void PlaylistPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    // Unified white background across the entire panel (toolbar and list area).
    painter.fillRect(rect(), Qt::white);
    // 1px left edge separator and 1px horizontal divider under the toolbar.
    const QColor dividerColor(225, 225, 225);
    painter.fillRect(QRect(0, 0, 1, height()), dividerColor);
    painter.fillRect(QRect(1, kToolbarHeight, width() - 1, 1), dividerColor);
}

void PlaylistPanel::mousePressEvent(QMouseEvent* event) {
    FocusDefaultControl();
    QWidget::mousePressEvent(event);
}

bool PlaylistPanel::HandleKeyDown(QKeyEvent* keyEvent) {
    const KeyChord chord = ChordFromKeyEvent(keyEvent);
    if ((toggleShortcut_ != kNoKey && chord == toggleShortcut_) || chord == Qt::Key_Escape) {
        if (callbacks_.onCloseRequested) {
            callbacks_.onCloseRequested();
        }
        return true;
    }
    if (QApplication::focusWidget() == list_) {
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            const int selected = list_->currentRow();
            if (selected >= 0 && static_cast<qsizetype>(selected) < files_.size() && callbacks_.onPlayItem) {
                callbacks_.onPlayItem(static_cast<size_t>(selected));
            }
            return true;
        }
        if (keyEvent->key() == Qt::Key_Space && callbacks_.onTogglePause) {
            callbacks_.onTogglePause();
            return true;
        }
    }
    return false;
}

bool PlaylistPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (HandleKeyDown(keyEvent)) {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlaylistPanel::keyPressEvent(QKeyEvent* event) {
    if (HandleKeyDown(event)) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PlaylistPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void PlaylistPanel::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile());
        }
    }
    if (!paths.isEmpty() && callbacks_.onFilesDropped) {
        callbacks_.onFilesDropped(paths);
        event->acceptProposedAction();
    }
}
