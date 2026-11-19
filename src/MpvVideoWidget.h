#pragma once

#if defined(Q_OS_WIN)
#include <QWidget>
#else
#include <QOpenGLWidget>
#endif
#include <cstdint>
#include <optional>

struct mpv_render_context;
class MpvPlayer;

// Platform-optimal video surface for libmpv:
// - On Windows: a dedicated native child HWND passed via "wid", so libmpv uses
//   its native Direct3D 11 swapchain and zero-copy d3d11va hardware decoding
//   without Windows OpenGL (WGL) driver bugs.
// - On macOS & Linux (X11/Wayland): a QOpenGLWidget backed by mpv_render_context,
//   so video and child overlay widgets composite inside a single window without
//   relying on X11-only window IDs or global popup coordinates on Wayland.
class MpvVideoWidget : public
#if defined(Q_OS_WIN)
    QWidget
#else
    QOpenGLWidget
#endif
{
    Q_OBJECT

public:
    explicit MpvVideoWidget(QWidget* parent = nullptr);
    ~MpvVideoWidget() override;

    MpvVideoWidget(const MpvVideoWidget&) = delete;
    MpvVideoWidget& operator=(const MpvVideoWidget&) = delete;

    std::optional<int64_t> NativeWindowId() const;
    void AttachPlayer(MpvPlayer* player);
    void Shutdown();

protected:
#if defined(Q_OS_WIN)
    QPaintEngine* paintEngine() const override { return nullptr; }
#else
    void initializeGL() override;
    void paintGL() override;
#endif

private slots:
    void OnMpvRenderUpdate();
    void OnFrameSwapped();

private:
#if !defined(Q_OS_WIN)
    MpvPlayer* player_ = nullptr;
    mpv_render_context* renderContext_ = nullptr;
#endif
};
