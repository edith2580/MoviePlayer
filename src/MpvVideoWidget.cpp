#include "MpvVideoWidget.h"

#if defined(Q_OS_WIN)
#include <windows.h>

MpvVideoWidget::MpvVideoWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    const WId handle = winId();
    // Windows skips a disabled child HWND when routing mouse clicks, wheel
    // events, and file drops, sending them directly to MainWindow.
    EnableWindow(reinterpret_cast<HWND>(handle), FALSE);
}

MpvVideoWidget::~MpvVideoWidget() = default;

std::optional<int64_t> MpvVideoWidget::NativeWindowId() const {
    return static_cast<int64_t>(winId());
}

void MpvVideoWidget::AttachPlayer(MpvPlayer*) {
}

void MpvVideoWidget::Shutdown() {
}

void MpvVideoWidget::OnMpvRenderUpdate() {
}

void MpvVideoWidget::OnFrameSwapped() {
}

#else

#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "MpvPlayer.h"

namespace {

constexpr int kGlRgba8 = 0x8058;

void* GetProcAddress(void*, const char* name) {
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        return nullptr;
    }
    return reinterpret_cast<void*>(context->getProcAddress(name));
}

}  // namespace

MpvVideoWidget::MpvVideoWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setFocusPolicy(Qt::NoFocus);

    connect(this, &QOpenGLWidget::frameSwapped, this, &MpvVideoWidget::OnFrameSwapped);
}

MpvVideoWidget::~MpvVideoWidget() {
    Shutdown();
}

std::optional<int64_t> MpvVideoWidget::NativeWindowId() const {
    return std::nullopt;
}

void MpvVideoWidget::AttachPlayer(MpvPlayer* player) {
    player_ = player;
    if (isValid()) {
        makeCurrent();
        initializeGL();
        doneCurrent();
    }
}

void MpvVideoWidget::Shutdown() {
    if (!renderContext_) {
        return;
    }
    makeCurrent();
    mpv_render_context_set_update_callback(renderContext_, nullptr, nullptr);
    mpv_render_context_free(renderContext_);
    renderContext_ = nullptr;
    doneCurrent();
}

void MpvVideoWidget::initializeGL() {
    if (renderContext_ || !player_ || !player_->Handle()) {
        return;
    }

    mpv_opengl_init_params glInitParams{&GetProcAddress, nullptr};
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    const int status = mpv_render_context_create(&renderContext_, player_->Handle(), params);
    if (status < 0) {
        throw std::runtime_error(std::string("mpv_render_context_create: ") + mpv_error_string(status));
    }

    mpv_render_context_set_update_callback(
        renderContext_,
        [](void* ctx) {
            auto* self = static_cast<MpvVideoWidget*>(ctx);
            QMetaObject::invokeMethod(self, &MpvVideoWidget::OnMpvRenderUpdate, Qt::QueuedConnection);
        },
        this);
}

void MpvVideoWidget::paintGL() {
    if (!renderContext_) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const int fboWidth = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int fboHeight = std::max(1, static_cast<int>(std::lround(height() * dpr)));

    mpv_opengl_fbo fbo{
        static_cast<int>(defaultFramebufferObject()),
        fboWidth,
        fboHeight,
        kGlRgba8,
    };
    int flipY = 1;
    int blockForTargetTime = 0;
    mpv_render_param params[]{
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(renderContext_, params);

    if (QOpenGLContext* ctx = QOpenGLContext::currentContext()) {
        ctx->functions()->glFlush();
    }
}

void MpvVideoWidget::OnMpvRenderUpdate() {
    if (!renderContext_) {
        return;
    }
    const uint64_t flags = mpv_render_context_update(renderContext_);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        update();
    }
}

void MpvVideoWidget::OnFrameSwapped() {
    if (renderContext_) {
        mpv_render_context_report_swap(renderContext_);
    }
}

#endif
