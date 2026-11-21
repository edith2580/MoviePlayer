#pragma once

#include <QOpenGLWidget>

#include <atomic>

struct mpv_render_context;
class MpvPlayer;

// Unified QOpenGLWidget video surface for libmpv across Windows, macOS, and Linux.
// Renders libmpv into Qt's OpenGL FBO so translucent child overlays (ControlBar,
// PlaylistPanel) composite cleanly inside a single window on all platforms.
class MpvVideoWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit MpvVideoWidget(QWidget* parent = nullptr);
    ~MpvVideoWidget() override;

    MpvVideoWidget(const MpvVideoWidget&) = delete;
    MpvVideoWidget& operator=(const MpvVideoWidget&) = delete;

    void AttachPlayer(MpvPlayer* player);
    void Shutdown();

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void OnMpvRenderUpdate();
    void OnFrameSwapped();

private:
    MpvPlayer* player_ = nullptr;
    mpv_render_context* renderContext_ = nullptr;
    std::atomic<bool> shuttingDown_{false};
};
