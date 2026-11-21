#include "MpvVideoWidget.h"

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

void MpvVideoWidget::AttachPlayer(MpvPlayer* player) {
    player_ = player;
    if (isValid()) {
        makeCurrent();
        initializeGL();
        doneCurrent();
    }
}

void MpvVideoWidget::Shutdown() {
    shuttingDown_.store(true, std::memory_order_release);
    disconnect(this, &QOpenGLWidget::frameSwapped, this, &MpvVideoWidget::OnFrameSwapped);
    mpv_render_context* ctx = renderContext_;
    renderContext_ = nullptr;
    if (!ctx) {
        return;
    }
    mpv_render_context_set_update_callback(ctx, nullptr, nullptr);
    if (isValid()) {
        makeCurrent();
        mpv_render_context_free(ctx);
        doneCurrent();
    } else {
        mpv_render_context_free(ctx);
    }
}

void MpvVideoWidget::initializeGL() {
    if (shuttingDown_.load(std::memory_order_acquire) || renderContext_ || !player_ || !player_->Handle()) {
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
            if (!self->shuttingDown_.load(std::memory_order_acquire)) {
                QMetaObject::invokeMethod(self, &MpvVideoWidget::OnMpvRenderUpdate, Qt::QueuedConnection);
            }
        },
        this);
}

void MpvVideoWidget::paintGL() {
    if (shuttingDown_.load(std::memory_order_acquire) || !renderContext_) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const int fboWidth = std::max(1, static_cast<int>(std::lround(width() * dpr)));
    const int fboHeight = std::max(1, static_cast<int>(std::lround(height() * dpr)));

    mpv_opengl_fbo fbo{
        static_cast<int>(defaultFramebufferObject()),
        fboWidth,
        fboHeight,
        0,
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
    if (shuttingDown_.load(std::memory_order_acquire) || !renderContext_) {
        return;
    }
    const uint64_t flags = mpv_render_context_update(renderContext_);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        update();
    }
}

void MpvVideoWidget::OnFrameSwapped() {
    if (!shuttingDown_.load(std::memory_order_acquire) && renderContext_) {
        mpv_render_context_report_swap(renderContext_);
    }
}
