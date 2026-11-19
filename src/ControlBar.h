#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

#include "MpvPlayer.h"

class QPainter;
class QVariantAnimation;

// The playback controls drawn over the bottom of the video area.
// - On Windows: rendered with QPainter into a premultiplied ARGB QImage and
//   composited directly inside MainWindow's HWND via DirectComposition so it
//   sits above mpv's native D3D11 video HWND and never leaves MainWindow.
// - On macOS & Linux: rendered directly as a translucent child QWidget above
//   MpvVideoWidget (QOpenGLWidget).
class ControlBar : public QWidget {
    Q_OBJECT

public:
    enum class Part { None, Background, PlayPause, SeekTrack, VolumeTrack, Fullscreen };

    static constexpr int kBarHeight = 72;

    explicit ControlBar(QWidget* parent = nullptr);
    ~ControlBar() override;

    ControlBar(const ControlBar&) = delete;
    ControlBar& operator=(const ControlBar&) = delete;

    void SetTargetGeometry(const QRect& localInMainWindow, int mainClientHeight);
    void SetBarVisible(bool visible);
    bool IsBarVisible() const { return barVisible_; }

    void SetPlayback(const PlaybackState& state);
    // While set, the bar shows this position instead of the playback position.
    void SetSeekPreview(std::optional<double> position);
    void SetHoveredPart(Part part);
    void SetFullscreenState(bool fullscreen);

    // Returns Part::None while the bar is hidden. point is in MainWindow coordinates.
    Part HitTest(QPoint mainWindowPoint) const;
    // Where mainWindowX falls along the seek track, from 0 to 1.
    double SeekFractionAt(int mainWindowX) const;
    // The volume that mainWindowX falls on along the volume slider.
    double VolumeAt(int mainWindowX) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Layout {
        qreal width = 0.0;
        qreal centerY = 0.0;
        QRectF playPause{};
        qreal trackLeft = 0.0;
        qreal trackRight = 0.0;
        QRectF time{};
        qreal volumeTrackLeft = 0.0;
        qreal volumeTrackRight = 0.0;
        QRectF volumeText{};
        QRectF fullscreen{};
        bool showTime = false;
        bool showVolume = false;
        bool showFullscreen = false;
    };

    struct Frame {
        QString time;
        QString volume;
        qreal fillFraction = 0.0;
        qreal volumeFraction = 0.0;
        qreal loopStartFraction = -1.0;  // < 0 when not set.
        qreal loopEndFraction = -1.0;
        std::vector<qreal> chapterFractions;
        bool paused = false;
        bool fullscreen = false;
        Part hovered = Part::None;
    };

    Layout ComputeLayout() const;
    Frame ComputeFrame() const;
    void RenderBarContents(QPainter& painter);
    void DrawPlayPauseIcon(QPainter& painter, const Layout& layout, const Frame& frame);
    void DrawFullscreenIcon(QPainter& painter, const Layout& layout, const Frame& frame);
    void DrawSeekTrack(QPainter& painter, const Layout& layout, const Frame& frame);
    void DrawVolume(QPainter& painter, const Layout& layout, const Frame& frame);
    void RequestRepaint();

#if defined(Q_OS_WIN)
    bool EnsureDCompResources();
    void ReleaseDCompResources();
    void RenderToDComp();

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    int swapChainWidth_ = 0;
    int swapChainHeight_ = 0;
#endif

    QVariantAnimation* fadeAnimation_ = nullptr;
    QRect localRect_;
    int mainClientHeight_ = 0;
    qreal opacity_ = 0.0;

    bool barVisible_ = false;
    bool fullscreen_ = false;
    PlaybackState playback_;
    std::optional<double> seekPreview_;
    Part hovered_ = Part::None;
};
