#include "ControlBar.h"

#include <QEasingCurve>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

#include "TimeFormat.h"

#if defined(Q_OS_WIN)
using Microsoft::WRL::ComPtr;
#endif

namespace {

constexpr qreal kHitHeight = 56.0;
constexpr qreal kControlsCenterFromBottom = 28.0;
constexpr qreal kEdgePadding = 12.0;
constexpr qreal kPlayPauseSize = 36.0;
constexpr qreal kTrackGap = 12.0;
constexpr qreal kItemGap = 8.0;
constexpr qreal kTimeWidth = 140.0;
constexpr qreal kVolumeTrackWidth = 80.0;
constexpr qreal kVolumeTextWidth = 44.0;
constexpr qreal kFullscreenSize = 36.0;
constexpr qreal kShowFullscreenMinWidth = 240.0;
constexpr qreal kShowTimeMinWidth = 400.0;
constexpr qreal kShowVolumeMinWidth = 600.0;
constexpr qreal kTrackHitHalfHeight = 12.0;
constexpr qreal kTrackThickness = 4.0;
constexpr qreal kTrackThicknessHovered = 6.0;
constexpr qreal kThumbRadius = 7.0;
constexpr int kFontPixelSize = 14;
constexpr qreal kMarkWidth = 2.0;
constexpr qreal kMarkIconHeight = 3.0;
constexpr qreal kMarkIconHalfWidth = 2.5;
constexpr double kMaxVolume = 130.0;  // mpv's default volume-max.

constexpr int kFadeMilliseconds = 120;

const QColor kText(255, 255, 255, 242);
const QColor kHoverFill(255, 255, 255, 41);
const QColor kTrack(255, 255, 255, 82);
const QColor kTrackFill(255, 255, 255, 242);
const QColor kLoopRegion(255, 107, 0, 204);
const QColor kLoopMark(255, 107, 0, 255);
const QColor kLoopPlayed(255, 199, 143, 255);
const QColor kChapterMark(255, 217, 0, 255);
const QColor kScrimBottom(0, 0, 0, 184);
const QColor kScrimTop(0, 0, 0, 0);

}  // namespace

ControlBar::ControlBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    fadeAnimation_ = new QVariantAnimation(this);
    fadeAnimation_->setDuration(kFadeMilliseconds);
    fadeAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(fadeAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        opacity_ = value.toReal();
        RequestRepaint();
    });
    connect(fadeAnimation_, &QVariantAnimation::finished, this, [this] {
        if (!barVisible_) {
            opacity_ = 0.0;
#if defined(Q_OS_WIN)
            RenderToDComp();
#else
            QWidget::hide();
#endif
        }
    });

    QWidget::hide();
}

ControlBar::~ControlBar() {
#if defined(Q_OS_WIN)
    ReleaseDCompResources();
#endif
}

void ControlBar::SetTargetGeometry(const QRect& localInMainWindow, int mainClientHeight) {
    const bool changed = (localRect_ != localInMainWindow || mainClientHeight_ != mainClientHeight);
    localRect_ = localInMainWindow;
    mainClientHeight_ = mainClientHeight;
#if !defined(Q_OS_WIN)
    if (geometry() != localInMainWindow) {
        setGeometry(localInMainWindow);
    }
#endif
    if (changed) {
        RequestRepaint();
    }
}

void ControlBar::SetBarVisible(bool visible) {
    if (visible == barVisible_) {
        return;
    }
    barVisible_ = visible;
#if !defined(Q_OS_WIN)
    if (visible) {
        QWidget::show();
        raise();
    }
#endif
    fadeAnimation_->stop();
    fadeAnimation_->setStartValue(opacity_);
    fadeAnimation_->setEndValue(visible ? 1.0 : 0.0);
    fadeAnimation_->start();
}

void ControlBar::SetFullscreenState(bool fullscreen) {
    if (fullscreen_ != fullscreen) {
        fullscreen_ = fullscreen;
        RequestRepaint();
    }
}

void ControlBar::SetPlayback(const PlaybackState& state) {
    playback_ = state;
    if (barVisible_ || opacity_ > 0.0) {
        RequestRepaint();
    }
}

void ControlBar::SetSeekPreview(std::optional<double> position) {
    if (seekPreview_ != position) {
        seekPreview_ = position;
        RequestRepaint();
    }
}

void ControlBar::SetHoveredPart(Part part) {
    if (hovered_ != part) {
        hovered_ = part;
        RequestRepaint();
    }
}

void ControlBar::RequestRepaint() {
#if defined(Q_OS_WIN)
    RenderToDComp();
#else
    update();
#endif
}

ControlBar::Layout ControlBar::ComputeLayout() const {
    Layout layout;
    layout.width = static_cast<qreal>(localRect_.width());
    const qreal h = static_cast<qreal>(localRect_.height());
    layout.centerY = h - kControlsCenterFromBottom;

    const qreal playLeft = kEdgePadding;
    const qreal halfControl = kPlayPauseSize / 2.0;
    layout.playPause = QRectF(playLeft, layout.centerY - halfControl, kPlayPauseSize, kPlayPauseSize);
    layout.trackLeft = layout.playPause.right() + kTrackGap;

    // Narrow windows drop the extras first, so the seek track stays usable.
    qreal right = layout.width - kEdgePadding;
    layout.showFullscreen = layout.width >= kShowFullscreenMinWidth;
    if (layout.showFullscreen) {
        layout.fullscreen = QRectF(right - kFullscreenSize, layout.playPause.top(), kFullscreenSize, kPlayPauseSize);
        right -= kFullscreenSize + kItemGap;
    }
    layout.showVolume = layout.width >= kShowVolumeMinWidth;
    if (layout.showVolume) {
        layout.volumeText = QRectF(right - kVolumeTextWidth, layout.playPause.top(), kVolumeTextWidth, kPlayPauseSize);
        right -= kVolumeTextWidth + kItemGap;
        layout.volumeTrackRight = right;
        layout.volumeTrackLeft = right - kVolumeTrackWidth;
        right = layout.volumeTrackLeft - kThumbRadius - kItemGap;
    }
    layout.showTime = layout.width >= kShowTimeMinWidth;
    if (layout.showTime) {
        layout.time = QRectF(right - kTimeWidth, layout.playPause.top(), kTimeWidth, kPlayPauseSize);
        right -= kTimeWidth + kTrackGap;
    }
    right -= kThumbRadius;
    layout.trackRight = std::max(right, layout.trackLeft);
    return layout;
}

ControlBar::Frame ControlBar::ComputeFrame() const {
    Frame frame;
    const double position = seekPreview_.value_or(playback_.position.value_or(0.0));
    frame.time = FormatTime(position);
    if (playback_.duration && *playback_.duration > 0.0) {
        frame.time += QStringLiteral(" / ") + FormatTime(*playback_.duration);
        frame.fillFraction = std::clamp(position / *playback_.duration, 0.0, 1.0);
        const auto toFraction = [&](double seconds) {
            return std::clamp(seconds / *playback_.duration, 0.0, 1.0);
        };
        frame.loopStartFraction = playback_.loopStart ? toFraction(*playback_.loopStart) : -1.0;
        frame.loopEndFraction = playback_.loopEnd ? toFraction(*playback_.loopEnd) : -1.0;
        for (const Chapter& chapter : playback_.chapters) {
            if (chapter.time > 0.0 && chapter.time < *playback_.duration) {
                frame.chapterFractions.push_back(toFraction(chapter.time));
            }
        }
    }
    frame.volume = QString::number(std::lround(playback_.volume)) + QLatin1Char('%');
    frame.volumeFraction = std::clamp(playback_.volume / kMaxVolume, 0.0, 1.0);
    frame.fullscreen = fullscreen_;
    frame.paused = playback_.paused;
    frame.hovered = hovered_;
    return frame;
}

ControlBar::Part ControlBar::HitTest(QPoint mainWindowPoint) const {
    if (!barVisible_ || localRect_.height() <= 0 || localRect_.width() <= 0) {
        return Part::None;
    }
    const QPoint local = mainWindowPoint - localRect_.topLeft();
    const qreal x = static_cast<qreal>(local.x());
    const qreal y = static_cast<qreal>(local.y());
    const qreal h = static_cast<qreal>(localRect_.height());
    if (y < h - kHitHeight || y > h || x < 0.0 || x > static_cast<qreal>(localRect_.width())) {
        return Part::None;
    }

    const Layout layout = ComputeLayout();
    if (layout.playPause.contains(QPointF(x, y))) {
        return Part::PlayPause;
    }
    if (layout.showFullscreen && layout.fullscreen.contains(QPointF(x, y))) {
        return Part::Fullscreen;
    }
    if (layout.showVolume &&
        x >= layout.volumeTrackLeft - kThumbRadius && x <= layout.volumeTrackRight + kThumbRadius &&
        std::abs(y - layout.centerY) <= kTrackHitHalfHeight) {
        return Part::VolumeTrack;
    }
    if (layout.trackRight > layout.trackLeft &&
        x >= layout.trackLeft - kThumbRadius && x <= layout.trackRight + kThumbRadius &&
        std::abs(y - layout.centerY) <= kTrackHitHalfHeight) {
        return Part::SeekTrack;
    }
    return Part::Background;
}

double ControlBar::SeekFractionAt(int mainWindowX) const {
    const Layout layout = ComputeLayout();
    const qreal span = layout.trackRight - layout.trackLeft;
    if (span <= 0.0) {
        return 0.0;
    }
    const qreal localX = static_cast<qreal>(mainWindowX - localRect_.left());
    return std::clamp(static_cast<double>((localX - layout.trackLeft) / span), 0.0, 1.0);
}

double ControlBar::VolumeAt(int mainWindowX) const {
    const Layout layout = ComputeLayout();
    const qreal span = layout.volumeTrackRight - layout.volumeTrackLeft;
    if (span <= 0.0) {
        return 0.0;
    }
    const qreal localX = static_cast<qreal>(mainWindowX - localRect_.left());
    return std::clamp(static_cast<double>((localX - layout.volumeTrackLeft) / span), 0.0, 1.0) * kMaxVolume;
}

void ControlBar::DrawPlayPauseIcon(QPainter& painter, const Layout& layout, const Frame& frame) {
    const qreal centerX = layout.playPause.center().x();
    painter.setPen(Qt::NoPen);
    if (frame.hovered == Part::PlayPause) {
        painter.setBrush(kHoverFill);
        painter.drawEllipse(QPointF(centerX, layout.centerY), kPlayPauseSize / 2.0, kPlayPauseSize / 2.0);
    }
    painter.setBrush(kText);
    if (frame.paused) {
        QPainterPath playPath;
        playPath.moveTo(centerX - 6.0, layout.centerY - 9.0);
        playPath.lineTo(centerX + 9.0, layout.centerY);
        playPath.lineTo(centerX - 6.0, layout.centerY + 9.0);
        playPath.closeSubpath();
        painter.drawPath(playPath);
    } else {
        const qreal top = layout.centerY - 9.0;
        painter.drawRect(QRectF(centerX - 7.0, top, 5.0, 18.0));
        painter.drawRect(QRectF(centerX + 2.0, top, 5.0, 18.0));
    }
}

void ControlBar::DrawFullscreenIcon(QPainter& painter, const Layout& layout, const Frame& frame) {
    const qreal centerX = layout.fullscreen.center().x();
    const qreal centerY = layout.centerY;
    painter.setPen(Qt::NoPen);
    if (frame.hovered == Part::Fullscreen) {
        painter.setBrush(kHoverFill);
        painter.drawEllipse(QPointF(centerX, centerY), kFullscreenSize / 2.0, kFullscreenSize / 2.0);
    }
    QPen pen(kText, 2.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    painter.setPen(pen);
    constexpr qreal kOuter = 8.0;
    constexpr qreal kInner = 3.0;
    for (const qreal sx : {-1.0, 1.0}) {
        for (const qreal sy : {-1.0, 1.0}) {
            const qreal cornerX = centerX + sx * (frame.fullscreen ? kInner : kOuter);
            const qreal cornerY = centerY + sy * (frame.fullscreen ? kInner : kOuter);
            const qreal armX = frame.fullscreen ? sx * (kOuter - kInner) : -sx * (kOuter - kInner);
            const qreal armY = frame.fullscreen ? sy * (kOuter - kInner) : -sy * (kOuter - kInner);
            painter.drawLine(QPointF(cornerX, cornerY), QPointF(cornerX + armX, cornerY));
            painter.drawLine(QPointF(cornerX, cornerY), QPointF(cornerX, cornerY + armY));
        }
    }
}

void ControlBar::DrawSeekTrack(QPainter& painter, const Layout& layout, const Frame& frame) {
    const bool emphasized = frame.hovered == Part::SeekTrack;
    const qreal half = (emphasized ? kTrackThicknessHovered : kTrackThickness) / 2.0;
    const qreal trackSpan = layout.trackRight - layout.trackLeft;
    const qreal fillRight = layout.trackLeft + frame.fillFraction * trackSpan;
    const qreal startX = layout.trackLeft + frame.loopStartFraction * trackSpan;
    const qreal endX = layout.trackLeft + frame.loopEndFraction * trackSpan;

    painter.setPen(Qt::NoPen);
    painter.setBrush(kTrack);
    painter.drawRoundedRect(QRectF(layout.trackLeft, layout.centerY - half, trackSpan, half * 2.0), half, half);

    if (frame.loopStartFraction >= 0.0 && frame.loopEndFraction >= 0.0) {
        const qreal left = std::min(startX, endX);
        const qreal right = std::max(startX, endX);
        painter.setBrush(kLoopRegion);
        painter.drawRect(QRectF(left, layout.centerY - half, right - left, half * 2.0));
    }

    if (fillRight > layout.trackLeft) {
        painter.setBrush(kTrackFill);
        painter.drawRoundedRect(QRectF(layout.trackLeft, layout.centerY - half, fillRight - layout.trackLeft, half * 2.0), half, half);
    }

    if (frame.loopStartFraction >= 0.0 && frame.loopEndFraction >= 0.0) {
        const qreal left = std::min(startX, endX);
        const qreal right = std::min(std::max(startX, endX), fillRight);
        if (right > left) {
            painter.setBrush(kLoopPlayed);
            painter.drawRect(QRectF(left, layout.centerY - half, right - left, half * 2.0));
        }
    }

    const auto drawMark = [&](qreal x, bool fromBelow, const QColor& color) {
        painter.setBrush(color);
        painter.drawRect(QRectF(x - kMarkWidth / 2.0, layout.centerY - half, kMarkWidth, half * 2.0));
        QPainterPath tri;
        if (fromBelow) {
            const qreal tipY = layout.centerY + half;
            tri.moveTo(x, tipY);
            tri.lineTo(x - kMarkIconHalfWidth, tipY + kMarkIconHeight);
            tri.lineTo(x + kMarkIconHalfWidth, tipY + kMarkIconHeight);
        } else {
            const qreal tipY = layout.centerY - half;
            tri.moveTo(x, tipY);
            tri.lineTo(x - kMarkIconHalfWidth, tipY - kMarkIconHeight);
            tri.lineTo(x + kMarkIconHalfWidth, tipY - kMarkIconHeight);
        }
        tri.closeSubpath();
        painter.drawPath(tri);
    };

    for (const qreal chapterFraction : frame.chapterFractions) {
        drawMark(layout.trackLeft + chapterFraction * trackSpan, false, kChapterMark);
    }
    if (frame.loopStartFraction >= 0.0) {
        drawMark(startX, true, kLoopMark);
    }
    if (frame.loopEndFraction >= 0.0) {
        drawMark(endX, true, kLoopMark);
    }

    if (emphasized) {
        painter.setBrush(kTrackFill);
        painter.drawEllipse(QPointF(fillRight, layout.centerY), kThumbRadius, kThumbRadius);
    }
}

void ControlBar::DrawVolume(QPainter& painter, const Layout& layout, const Frame& frame) {
    const bool emphasized = frame.hovered == Part::VolumeTrack;
    const qreal half = (emphasized ? kTrackThicknessHovered : kTrackThickness) / 2.0;
    const qreal span = layout.volumeTrackRight - layout.volumeTrackLeft;
    const qreal fillRight = layout.volumeTrackLeft + frame.volumeFraction * span;

    painter.setPen(Qt::NoPen);
    painter.setBrush(kTrack);
    painter.drawRoundedRect(QRectF(layout.volumeTrackLeft, layout.centerY - half, span, half * 2.0), half, half);

    if (fillRight > layout.volumeTrackLeft) {
        painter.setBrush(kTrackFill);
        painter.drawRoundedRect(QRectF(layout.volumeTrackLeft, layout.centerY - half, fillRight - layout.volumeTrackLeft, half * 2.0),
                                half, half);
    }
    if (emphasized) {
        painter.setBrush(kTrackFill);
        painter.drawEllipse(QPointF(fillRight, layout.centerY), kThumbRadius, kThumbRadius);
    }

    painter.setPen(kText);
    painter.drawText(layout.volumeText, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, frame.volume);
}

void ControlBar::RenderBarContents(QPainter& painter) {
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity(opacity_);

    const Layout layout = ComputeLayout();
    const Frame frame = ComputeFrame();

    // A gradient behind the controls keeps them readable over bright video.
    QLinearGradient scrim(0.0, 0.0, 0.0, static_cast<qreal>(localRect_.height()));
    scrim.setColorAt(0.0, kScrimTop);
    scrim.setColorAt(0.6, kScrimBottom);
    scrim.setColorAt(1.0, kScrimBottom);
    painter.fillRect(QRectF(0.0, 0.0, layout.width, static_cast<qreal>(localRect_.height())), scrim);

    QFont font = painter.font();
    font.setPixelSize(kFontPixelSize);
    painter.setFont(font);

    DrawPlayPauseIcon(painter, layout, frame);
    if (layout.trackRight > layout.trackLeft) {
        DrawSeekTrack(painter, layout, frame);
    }
    if (layout.showTime) {
        painter.setPen(kText);
        painter.drawText(layout.time, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, frame.time);
    }
    if (layout.showVolume) {
        DrawVolume(painter, layout, frame);
    }
    if (layout.showFullscreen) {
        DrawFullscreenIcon(painter, layout, frame);
    }
}

void ControlBar::paintEvent(QPaintEvent*) {
#if !defined(Q_OS_WIN)
    if (localRect_.width() <= 0 || localRect_.height() <= 0 || opacity_ <= 0.0) {
        return;
    }
    QPainter painter(this);
    RenderBarContents(painter);
#endif
}

#if defined(Q_OS_WIN)

bool ControlBar::EnsureDCompResources() {
    if (compositionDevice_ && compositionTarget_ && visual_) {
        return true;
    }
    QWidget* owner = parentWidget();
    if (!owner) {
        return false;
    }
    const HWND mainHwnd = reinterpret_cast<HWND>(owner->winId());
    if (!mainHwnd) {
        return false;
    }

    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3dDevice_, nullptr, &d3dContext_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &d3dDevice_, nullptr, &d3dContext_);
    }
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(d3dDevice_.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_))) ||
        FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositionDevice_))) ||
        FAILED(compositionDevice_->CreateTargetForHwnd(mainHwnd, TRUE, &compositionTarget_)) ||
        FAILED(compositionDevice_->CreateVisual(&visual_))) {
        ReleaseDCompResources();
        return false;
    }
    return true;
}

void ControlBar::ReleaseDCompResources() {
    swapChain_.Reset();
    visual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    dxgiFactory_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    swapChainWidth_ = 0;
    swapChainHeight_ = 0;
}

void ControlBar::RenderToDComp() {
    QWidget* owner = parentWidget();
    if (!owner || !EnsureDCompResources()) {
        return;
    }

    const bool show = (barVisible_ || opacity_ > 0.0) && localRect_.width() > 0 && localRect_.height() > 0;
    if (!show) {
        compositionTarget_->SetRoot(nullptr);
        compositionDevice_->Commit();
        return;
    }

    const qreal dpr = owner->devicePixelRatioF() > 0.0 ? owner->devicePixelRatioF() : 1.0;
    const int physW = std::max(1, static_cast<int>(std::lround(localRect_.width() * dpr)));
    const int physH = std::max(1, static_cast<int>(std::lround(localRect_.height() * dpr)));
    const int physClientH = std::max(physH, static_cast<int>(std::lround(mainClientHeight_ * dpr)));

    if (!swapChain_ || swapChainWidth_ != physW || swapChainHeight_ != physH) {
        if (swapChain_) {
            if (FAILED(swapChain_->ResizeBuffers(0, static_cast<UINT>(physW), static_cast<UINT>(physH), DXGI_FORMAT_UNKNOWN, 0))) {
                ReleaseDCompResources();
                return;
            }
        } else {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = static_cast<UINT>(physW);
            desc.Height = static_cast<UINT>(physH);
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
            if (FAILED(dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_)) ||
                FAILED(visual_->SetContent(swapChain_.Get()))) {
                ReleaseDCompResources();
                return;
            }
        }
        swapChainWidth_ = physW;
        swapChainHeight_ = physH;
    }

    QImage image(physW, physH, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        RenderBarContents(painter);
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        return;
    }
    d3dContext_->UpdateSubresource(backBuffer.Get(), 0, nullptr, image.constBits(), static_cast<UINT>(image.bytesPerLine()), 0);
    swapChain_->Present(0, 0);

    visual_->SetOffsetX(0.0f);
    visual_->SetOffsetY(static_cast<float>(physClientH - physH));
    compositionTarget_->SetRoot(visual_.Get());
    compositionDevice_->Commit();
}

#endif
