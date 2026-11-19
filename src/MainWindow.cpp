#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <thread>
#include <utility>

#include "FileDialogs.h"
#include "InfoDialog.h"
#include "MpvVideoWidget.h"
#include "SettingsDialog.h"
#include "SmiSubtitles.h"
#include "TimeFormat.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shlwapi.h>
#endif

namespace {

constexpr const char* kAppTitle = "Movie Player";

constexpr int kVolumeStep = 5;
constexpr double kSubtitleDelayStep = 0.1;
constexpr double kAudioDelayStep = 0.1;
constexpr double kSpeedStep = 0.1;
constexpr int kVideoAdjustStep = 2;
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;
constexpr int kMaxScreenPercent = 90;
constexpr int kMinVideoWidth = 320;
constexpr int kMinVideoHeight = 200;
constexpr int kIdleDelayMs = 2000;
constexpr int kExplorerBatchMs = 1500;
constexpr int kOpenRetryDelaysMs[] = {500, 1000, 1500, 2000, 3000};
constexpr qint64 kSeekPreviewExpiryDurationMs = 3000;
constexpr double kSeekPreviewToleranceSeconds = 2.0;

QString SubtitleTempDirectory() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(root).filePath(QStringLiteral("MoviePlayer/%1").arg(QCoreApplication::applicationPid()));
}

qint64 NowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

bool IsNetworkPath(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    if (path.contains(QStringLiteral("://"))) {
        return true;
    }
    if (path.startsWith(QStringLiteral("\\\\")) || path.startsWith(QStringLiteral("//"))) {
        return true;
    }
#if defined(Q_OS_WIN)
    const std::wstring wide = QDir::toNativeSeparators(path).toStdWString();
    if (wide.size() >= 2 && wide[1] == L':') {
        const wchar_t root[] = {wide[0], L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) == DRIVE_REMOTE) {
            return true;
        }
    }
    if (PathIsNetworkPathW(wide.c_str()) != FALSE) {
        return true;
    }
#else
    if (path.startsWith(QStringLiteral("/Volumes/")) ||
        path.startsWith(QStringLiteral("/mnt/")) ||
        path.startsWith(QStringLiteral("/media/")) ||
        path.startsWith(QStringLiteral("/net/")) ||
        path.contains(QStringLiteral("/gvfs/"))) {
        return true;
    }
#endif
    return false;
}

}  // namespace

MainWindow::MainWindow(const Settings& settings, QWidget* parent)
    : QWidget(parent),
      preferences_(settings.preferences),
      subtitleTempDirectory_(SubtitleTempDirectory()) {
    playback_.volume = settings.volume;
    playback_.subtitleEncoding = settings.subtitleEncoding;
    playback_.adjustments = settings.videoAdjustments;

    setWindowTitle(QString::fromUtf8(kAppTitle));
    setMinimumSize(kMinVideoWidth, kMinVideoHeight);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    // Keep Korean/CJK IME from intercepting single-letter shortcut keys like S, A, C.
    setAttribute(Qt::WA_InputMethodEnabled, false);

    videoWidget_ = new MpvVideoWidget(this);
    player_ = new MpvPlayer(settings.volume, settings.subtitleEncoding,
                            settings.videoAdjustments, settings.preferences.subtitleStyle, this);
    videoWidget_->AttachPlayer(player_);
    controlBar_ = new ControlBar(this);

    PlaylistPanel::Callbacks panelCallbacks;
    panelCallbacks.onPlayItem = [this](size_t index) { PlayPlaylistItem(index); };
    panelCallbacks.onToggleAutoAdvance = [this] { ToggleAutoAdvance(); };
    panelCallbacks.onToggleRepeatOne = [this] { ToggleRepeatOne(); };
    panelCallbacks.onToggleRepeatAll = [this] { ToggleRepeatAll(); };
    panelCallbacks.onTogglePause = [this] {
        player_->TogglePause();
        ShowControls();
    };
    panelCallbacks.onFilesDropped = [this](const QStringList& paths) { AddFilesToPlaylist(paths); };
    panelCallbacks.onCloseRequested = [this] { SetPlaylistPanelOpen(false); };
    panelCallbacks.onPanelWidthChanged = [this](int) { LayoutChildren(); };

    playlistPanel_ = new PlaylistPanel(std::move(panelCallbacks), this);
    playlistPanel_->SetToggleShortcut(preferences_.shortcuts[static_cast<size_t>(ShortcutAction::PlaylistShow)]);
    SyncPlaylistPanel();

    idleTimer_ = new QTimer(this);
    idleTimer_->setSingleShot(true);
    connect(idleTimer_, &QTimer::timeout, this, &MainWindow::HandleIdleTimeout);

    openRetryTimer_ = new QTimer(this);
    openRetryTimer_->setSingleShot(true);
    connect(openRetryTimer_, &QTimer::timeout, this, [this] {
        if (!openRetryPath_.isEmpty()) {
            OpenVideo(openRetryPath_, openRetrySubtitle_);
        }
    });

    seriesTimer_ = new QTimer(this);
    seriesTimer_->setSingleShot(true);
    connect(seriesTimer_, &QTimer::timeout, this, &MainWindow::FindSeriesInBackground);

    connect(player_, &MpvPlayer::playbackError, this, &MainWindow::HandlePlaybackError);
    connect(player_, &MpvPlayer::subtitleLoadFailed, this, [this] {
        player_->ShowMessage(QStringLiteral("자막 파일을 열 수 없습니다"));
    });
    connect(player_, &MpvPlayer::stateChanged, this, &MainWindow::HandleStateChanged);
    connect(player_, &MpvPlayer::videoReady, this, &MainWindow::HandleVideoReady);

    RestoreWindowPosition(settings);
    LayoutChildren();
    ApplyRepeatMode();
}

MainWindow::~MainWindow() {
    if (videoWidget_) {
        videoWidget_->Shutdown();
    }
    delete player_;
    player_ = nullptr;
    QDir(subtitleTempDirectory_).removeRecursively();
}

void MainWindow::ShowInitial() {
    if (startMaximized_) {
        showMaximized();
    } else {
        show();
    }
}

void MainWindow::ActivateAndRaise() {
    if (isMinimized()) {
        if (fullscreen_) {
            showFullScreen();
        } else if (startMaximized_) {
            showMaximized();
        } else {
            showNormal();
        }
    }
    raise();
    activateWindow();
}

void MainWindow::ArmOpenRetry(const QString& path, const QString& preferredSubtitle) {
    ClearOpenRetry();
    if (!IsNetworkPath(path)) {
        return;
    }
    openRetryPath_ = path;
    openRetrySubtitle_ = preferredSubtitle;
    openRetryCount_ = 0;
}

void MainWindow::ClearOpenRetry() {
    if (openRetryTimer_) {
        openRetryTimer_->stop();
    }
    openRetryPath_.clear();
    openRetrySubtitle_.clear();
    openRetryCount_ = 0;
}

void MainWindow::HandlePlaybackError(const QString& reason, bool unrecognizedFormat) {
    if (unrecognizedFormat && !openRetryPath_.isEmpty() && openRetryCount_ < std::size(kOpenRetryDelaysMs)) {
        const int delay = kOpenRetryDelaysMs[openRetryCount_++];
        if (openRetryCount_ >= 2 && player_) {
            player_->ShowMessage(QStringLiteral("파일을 읽는 중..."));
        }
        openRetryTimer_->start(delay);
        return;
    }
    ClearOpenRetry();
    currentVideoPath_.clear();
    const QString text = QStringLiteral("동영상을 재생할 수 없습니다.\n\n") + reason;
    QMessageBox::critical(this, QString::fromUtf8(kAppTitle), text);
}

void MainWindow::Open(const QString& path) {
    ClearOpenRetry();
    lastExplorerOpenMs_ = 0;
    if (IsSubtitleFile(path)) {
        AddSubtitle(path);
        ShowControls();
    } else {
        StartPlaylist({path}, 0, {}, true);
    }
}

void MainWindow::OpenFiles(const QStringList& paths) {
    QStringList videos;
    for (const QString& path : paths) {
        if (!IsSubtitleFile(path)) {
            videos.push_back(path);
        }
    }
    if (videos.size() == 1) {
        Open(videos.front());
    } else if (videos.isEmpty()) {
        for (const QString& path : paths) {
            Open(path);
        }
    } else {
        ClearOpenRetry();
        lastExplorerOpenMs_ = 0;
        SortByName(videos);
        StartPlaylist(std::move(videos), 0, {}, false);
    }
}

void MainWindow::StartPlaylist(QStringList files, size_t index, const QString& preferredSubtitle, bool findSeries) {
    playlist_ = std::move(files);
    playlistIndex_ = index;
    keepWindowSize_ = false;
    ++seriesSearch_;
    seriesTimer_->stop();
    ArmOpenRetry(playlist_[static_cast<qsizetype>(playlistIndex_)], preferredSubtitle);
    OpenVideo(playlist_[static_cast<qsizetype>(playlistIndex_)], preferredSubtitle);
    SyncPlaylistPanel();
    ShowControls();
    if (findSeries) {
        seriesTimer_->start(kExplorerBatchMs);
    }
}

void MainWindow::FindSeriesInBackground() {
    if (playlist_.isEmpty()) {
        return;
    }
    const unsigned search = seriesSearch_;
    const QString video = playlist_[static_cast<qsizetype>(playlistIndex_)];
    // Reading a folder on a network drive can take seconds, so it's done off the UI thread.
    std::thread([this, search, video] {
        QStringList series = FindSeries(video);
        QMetaObject::invokeMethod(
            this,
            [this, search, video, series = std::move(series)]() {
                HandleSeriesFound(search, video, series);
            },
            Qt::QueuedConnection);
    }).detach();
}

void MainWindow::HandleSeriesFound(unsigned search, const QString& video, const QStringList& series) {
    if (search != seriesSearch_ || playlist_.size() != 1 ||
        playlist_.front().compare(video, Qt::CaseInsensitive) != 0) {
        return;
    }
    const auto itself = std::find_if(series.begin(), series.end(), [&](const QString& path) {
        return path.compare(video, Qt::CaseInsensitive) == 0;
    });
    if (itself != series.end()) {
        playlistIndex_ = static_cast<size_t>(itself - series.begin());
        playlist_ = series;
        SyncPlaylistPanel();
    }
}

void MainWindow::PlayPlaylistItem(size_t index) {
    if (index >= static_cast<size_t>(playlist_.size())) {
        return;
    }
    playlistIndex_ = index;
    keepWindowSize_ = !preferences_.fitWindowOnPlaylistChange;
    const QString& target = playlist_[static_cast<qsizetype>(index)];
    ArmOpenRetry(target);
    OpenVideo(target);
    SyncPlaylistPanel();
    player_->ShowMessage(QStringLiteral("%1/%2: %3")
                             .arg(index + 1)
                             .arg(playlist_.size())
                             .arg(QFileInfo(target).fileName()));
    ShowControls();
}

void MainWindow::PlayNext(bool automatic) {
    if (playlist_.isEmpty()) {
        return;
    }
    if (playlistIndex_ + 1 < static_cast<size_t>(playlist_.size())) {
        PlayPlaylistItem(playlistIndex_ + 1);
    } else if (preferences_.repeat == RepeatMode::All) {
        PlayPlaylistItem(0);
    } else if (!automatic) {
        player_->ShowMessage(QStringLiteral("마지막 파일입니다"));
    }
}

void MainWindow::PlayPrevious() {
    if (playlist_.isEmpty()) {
        return;
    }
    if (playlistIndex_ > 0) {
        PlayPlaylistItem(playlistIndex_ - 1);
    } else if (preferences_.repeat == RepeatMode::All) {
        PlayPlaylistItem(static_cast<size_t>(playlist_.size() - 1));
    } else {
        player_->ShowMessage(QStringLiteral("첫 파일입니다"));
    }
}

void MainWindow::ApplyRepeatMode() {
    player_->SetRepeatFile(preferences_.repeat == RepeatMode::One);
}

PopupPlace MainWindow::PopupPlaceForKeys() const {
    const QPoint cursor = QCursor::pos();
    if (frameGeometry().contains(cursor)) {
        return {cursor, false};
    }
    return {mapToGlobal(rect().center()), true};
}

void MainWindow::ShowPlaylist() {
    const PopupPlace place = PopupPlaceForKeys();
    menuOpen_ = true;
    SetCursorHidden(false);
    ShowControls();
    const PlaylistChoice choice = ShowPlaylistMenu(this, place, playlist_, playlistIndex_, preferences_.autoAdvance, preferences_.repeat);
    menuOpen_ = false;
    ShowControls();

    const auto announce = [this](const QString& name, bool on) {
        player_->ShowMessage(name + (on ? QStringLiteral(" 켬") : QStringLiteral(" 끔")));
    };
    switch (choice.action) {
    case PlaylistChoice::Action::Play:
        PlayPlaylistItem(choice.index);
        return;
    case PlaylistChoice::Action::ToggleAutoAdvance:
        preferences_.autoAdvance = !preferences_.autoAdvance;
        if (!preferences_.autoAdvance && preferences_.repeat == RepeatMode::All) {
            preferences_.repeat = RepeatMode::None;
        }
        announce(QStringLiteral("다음 파일 자동 재생"), preferences_.autoAdvance);
        break;
    case PlaylistChoice::Action::ToggleRepeatOne:
        preferences_.repeat = preferences_.repeat == RepeatMode::One ? RepeatMode::None : RepeatMode::One;
        announce(QStringLiteral("한 개 반복"), preferences_.repeat == RepeatMode::One);
        break;
    case PlaylistChoice::Action::ToggleRepeatAll:
        preferences_.repeat = preferences_.repeat == RepeatMode::All ? RepeatMode::None : RepeatMode::All;
        if (preferences_.repeat == RepeatMode::All) {
            preferences_.autoAdvance = true;
        }
        announce(QStringLiteral("전체 목록 반복"), preferences_.repeat == RepeatMode::All);
        break;
    case PlaylistChoice::Action::None:
        return;
    }
    ApplyRepeatMode();
    SaveCurrentSettings();
}

void MainWindow::TogglePlaylistPanel() {
    if (!playlistPanel_) {
        return;
    }
    SetPlaylistPanelOpen(!playlistPanel_->IsOpen());
}

void MainWindow::SetPlaylistPanelOpen(bool open) {
    if (!playlistPanel_) {
        return;
    }
    if (open == playlistPanel_->IsOpen()) {
        if (open) {
            playlistPanel_->FocusDefaultControl();
        }
        return;
    }

    if (open) {
        SetCursorHidden(false);
        SyncPlaylistPanel();
    }

    const bool normalWindowed = !fullscreen_ && !isMaximized() && !isMinimized();
    const int panelW = playlistPanel_->PanelWidth();

    if (normalWindowed) {
        QRect geom = geometry();
        if (open) {
            // Expand the window to the right so the video area keeps its exact size.
            int newWidth = geom.width() + panelW;
            int newLeft = geom.left();
            if (QScreen* scr = screen()) {
                const QRect avail = scr->availableGeometry();
                const int frameExtraW = frameGeometry().width() - geom.width();
                newWidth = std::min(newWidth, avail.width() - frameExtraW);
                if (newLeft + newWidth + frameExtraW > avail.right()) {
                    newLeft = std::max(avail.left(), avail.right() - newWidth - frameExtraW + 1);
                }
            }
            playlistPanel_->SetOpen(true);
            setGeometry(newLeft, geom.top(), newWidth, geom.height());
        } else {
            const int newWidth = std::max(minimumWidth(), geom.width() - panelW);
            playlistPanel_->SetOpen(false);
            resize(newWidth, geom.height());
        }
    } else {
        playlistPanel_->SetOpen(open);
    }

    LayoutChildren();
    if (!open) {
        setFocus();
    }
    ShowControls();
}

void MainWindow::SyncPlaylistPanel() {
    if (!playlistPanel_) {
        return;
    }
    playlistPanel_->SetPlaylist(playlist_, playlistIndex_);
    playlistPanel_->SetOptions(preferences_.autoAdvance, preferences_.repeat);
}

void MainWindow::AddFilesToPlaylist(const QStringList& paths) {
    QStringList videos;
    QStringList subtitles;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QStringList folderVideos;
            const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo& entry : entries) {
                if (IsVideoFile(entry.fileName())) {
                    folderVideos.push_back(entry.absoluteFilePath());
                }
            }
            SortByName(folderVideos);
            videos.append(folderVideos);
        } else if (IsSubtitleFile(path)) {
            subtitles.push_back(path);
        } else {
            videos.push_back(path);
        }
    }

    if (videos.isEmpty()) {
        for (const QString& subtitle : subtitles) {
            if (!currentVideoPath_.isEmpty()) {
                AddSubtitle(subtitle);
            }
        }
        return;
    }

    SortByName(videos);
    if (playlist_.isEmpty()) {
        StartPlaylist(std::move(videos), 0, {}, false);
        return;
    }

    ++seriesSearch_;
    seriesTimer_->stop();

    size_t added = 0;
    for (const QString& video : videos) {
        const bool exists = std::any_of(playlist_.begin(), playlist_.end(), [&](const QString& existing) {
            return existing.compare(video, Qt::CaseInsensitive) == 0;
        });
        if (!exists) {
            playlist_.push_back(video);
            ++added;
        }
    }
    SyncPlaylistPanel();
    if (added > 0) {
        player_->ShowMessage(QStringLiteral("재생 목록에 %1개 추가").arg(added));
    } else {
        player_->ShowMessage(QStringLiteral("이미 재생 목록에 있는 파일입니다"));
    }
}

void MainWindow::ToggleAutoAdvance() {
    preferences_.autoAdvance = !preferences_.autoAdvance;
    if (!preferences_.autoAdvance && preferences_.repeat == RepeatMode::All) {
        preferences_.repeat = RepeatMode::None;
    }
    player_->ShowMessage(QStringLiteral("다음 파일 자동 재생") +
                         (preferences_.autoAdvance ? QStringLiteral(" 켬") : QStringLiteral(" 끔")));
    ApplyRepeatMode();
    SaveCurrentSettings();
    SyncPlaylistPanel();
}

void MainWindow::ToggleRepeatOne() {
    preferences_.repeat = preferences_.repeat == RepeatMode::One ? RepeatMode::None : RepeatMode::One;
    player_->ShowMessage(QStringLiteral("한 개 반복") +
                         (preferences_.repeat == RepeatMode::One ? QStringLiteral(" 켬") : QStringLiteral(" 끔")));
    ApplyRepeatMode();
    SaveCurrentSettings();
    SyncPlaylistPanel();
}

void MainWindow::ToggleRepeatAll() {
    preferences_.repeat = preferences_.repeat == RepeatMode::All ? RepeatMode::None : RepeatMode::All;
    if (preferences_.repeat == RepeatMode::All) {
        preferences_.autoAdvance = true;
    }
    player_->ShowMessage(QStringLiteral("전체 목록 반복") +
                         (preferences_.repeat == RepeatMode::All ? QStringLiteral(" 켬") : QStringLiteral(" 끔")));
    ApplyRepeatMode();
    SaveCurrentSettings();
    SyncPlaylistPanel();
}

void MainWindow::OpenFromExplorer(const QString& path) {
    if (!IsSubtitleFile(path)) {
        const qint64 now = NowMs();
        const bool sameSelection = lastExplorerOpenMs_ != 0 &&
                                   (now - lastExplorerOpenMs_) < kExplorerBatchMs &&
                                   !playlist_.isEmpty();
        if (sameSelection) {
            lastExplorerOpenMs_ = now;
            ++seriesSearch_;
            seriesTimer_->stop();
            const QString playing = playlist_[static_cast<qsizetype>(playlistIndex_)];
            const bool exists = std::any_of(playlist_.begin(), playlist_.end(), [&](const QString& p) {
                return p.compare(path, Qt::CaseInsensitive) == 0;
            });
            if (!exists) {
                playlist_.push_back(path);
            }
            SortByName(playlist_);
            const qsizetype foundIdx = playlist_.indexOf(playing);
            playlistIndex_ = foundIdx >= 0 ? static_cast<size_t>(foundIdx) : 0;
            SyncPlaylistPanel();
            return;
        }
        Open(path);
        lastExplorerOpenMs_ = now;
        return;
    }

    const QString video = FindVideoForSubtitle(path);
    if (!video.isEmpty() && video.compare(currentVideoPath_, Qt::CaseInsensitive) != 0) {
        ClearOpenRetry();
        lastExplorerOpenMs_ = 0;
        StartPlaylist({video}, 0, path, true);
    } else if (!currentVideoPath_.isEmpty()) {
        Open(path);
    } else {
        const QString text = QStringLiteral("이 자막과 이름이 같은 영상 파일을 찾을 수 없습니다.\n\n") + path;
        QMessageBox::information(this, QString::fromUtf8(kAppTitle), text);
    }
}

void MainWindow::OpenVideo(const QString& path, const QString& preferredSubtitle) {
    videoWidth_ = 0;
    videoHeight_ = 0;
    currentVideoPath_ = path;
    pendingSubtitleName_.clear();
    QDir(subtitleTempDirectory_).removeRecursively();

    QStringList subtitles;
    if (!preferredSubtitle.isEmpty()) {
        const QStringList prepared = IsSmiFile(preferredSubtitle)
                                         ? PrepareSmiFile(preferredSubtitle, subtitleTempDirectory_)
                                         : QStringList{preferredSubtitle};
        subtitles.append(prepared);
        if (!subtitles.isEmpty()) {
            pendingSubtitleName_ = QFileInfo(subtitles.front()).fileName();
        }
    }
    for (const QString& file : PrepareSmiFilesFor(path, subtitleTempDirectory_)) {
        if (!subtitles.contains(file)) {
            subtitles.push_back(file);
        }
    }
    player_->Open(path, subtitles);
}

void MainWindow::AddSubtitle(const QString& path) {
    QStringList files{path};
    if (IsSmiFile(path)) {
        files = PrepareSmiFile(path, subtitleTempDirectory_);
    }
    // The preferred language comes first: add the others quietly, and switch to it last.
    for (qsizetype i = files.size(); i-- > 1;) {
        player_->AddSubtitleFile(files[i], false);
    }
    if (!files.isEmpty()) {
        player_->AddSubtitleFile(files.front(), true);
    }
}

void MainWindow::ApplyDefaultWindowSize() {
    QScreen* scr = screen();
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    if (!scr) {
        resize(kDefaultWidth, kDefaultHeight);
        return;
    }
    const QRect work = scr->availableGeometry();
    int w = kDefaultWidth;
    int h = kDefaultHeight;
    const int maxWidth = work.width() * kMaxScreenPercent / 100;
    const int maxHeight = work.height() * kMaxScreenPercent / 100;
    if (w > maxWidth) {
        h = h * maxWidth / w;
        w = maxWidth;
    }
    if (h > maxHeight) {
        w = w * maxHeight / h;
        h = maxHeight;
    }
    setGeometry(work.left() + (work.width() - w) / 2,
                work.top() + (work.height() - h) / 2,
                w, h);
}

void MainWindow::HandleVideoReady(int width, int height) {
    ClearOpenRetry();
    if (!pendingSubtitleName_.isEmpty()) {
        for (const MediaTrack& track : playback_.tracks) {
            if (track.type == TrackType::Subtitle &&
                track.externalFileName.compare(pendingSubtitleName_, Qt::CaseInsensitive) == 0) {
                player_->SelectSubtitle(track.id);
                break;
            }
        }
        pendingSubtitleName_.clear();
    }
    videoWidth_ = width;
    videoHeight_ = height;
    const bool center = std::exchange(centerOnFirstFit_, false);
    const bool keepSize = std::exchange(keepWindowSize_, false);
    if (!keepSize && !fullscreen_ && !isMaximized() && !isMinimized()) {
        FitWindowToVideo(1.0, center);
    }
}

void MainWindow::FitWindowToVideo(double requestedScale, bool center) {
    const bool sideways = playback_.rotation % 180 != 0;
    const int videoWidth = sideways ? videoHeight_ : videoWidth_;
    const int videoHeight = sideways ? videoWidth_ : videoHeight_;
    QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (videoWidth <= 0 || videoHeight <= 0 || !scr) {
        return;
    }

    const QRect work = scr->availableGeometry();
    const qreal dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;

    // Convert physical video pixels to device-independent logical pixels.
    const double logicalVideoW = static_cast<double>(videoWidth) / dpr;
    const double logicalVideoH = static_cast<double>(videoHeight) / dpr;

    const int frameExtraW = std::max(0, frameGeometry().width() - width());
    const int frameExtraH = std::max(0, frameGeometry().height() - height());
    const int panelW = (playlistPanel_ && playlistPanel_->IsOpen() && !fullscreen_) ? playlistPanel_->PanelWidth() : 0;

    const int maxVideoW = std::max(kMinVideoWidth, work.width() - frameExtraW - panelW);
    const int maxVideoH = std::max(kMinVideoHeight, work.height() - frameExtraH);

    double scale = requestedScale;
    if (logicalVideoW * scale < kMinVideoWidth) {
        scale = static_cast<double>(kMinVideoWidth) / logicalVideoW;
    }
    scale = std::min({scale, static_cast<double>(maxVideoW) / logicalVideoW, static_cast<double>(maxVideoH) / logicalVideoH});

    const int pictureWidth = std::max(kMinVideoWidth, static_cast<int>(std::lround(logicalVideoW * scale)));
    const int pictureHeight = std::max(kMinVideoHeight, static_cast<int>(std::lround(logicalVideoH * scale)));
    const int clientWidth = pictureWidth + panelW;
    const int clientHeight = pictureHeight;

    const QRect current = geometry();
    const int left = center
        ? work.left() + (work.width() - clientWidth) / 2
        : std::clamp(current.left(), work.left(), std::max(work.left(), work.right() - clientWidth - frameExtraW + 1));
    const int top = center
        ? work.top() + (work.height() - frameExtraH - clientHeight) / 2
        : std::clamp(current.top(), work.top(), std::max(work.top(), work.bottom() - clientHeight - frameExtraH + 1));

    setGeometry(left, top, clientWidth, clientHeight);
    LayoutChildren();
}

void MainWindow::SetWindowSizePercent(int percent) {
    if (videoWidth_ <= 0) {
        player_->ShowMessage(QStringLiteral("크기를 맞출 영상이 없습니다"));
        return;
    }
    SetFullscreen(false);
    if (isMaximized()) {
        showNormal();
    }
    FitWindowToVideo(percent / 100.0, false);
    player_->ShowMessage(QStringLiteral("화면 크기 %1%").arg(percent));
    ShowControls();
}

void MainWindow::RestoreWindowPosition(const Settings& settings) {
    if (!settings.windowPosition) {
        ApplyDefaultWindowSize();
        centerOnFirstFit_ = true;
        return;
    }
    QRect saved = *settings.windowPosition;
    // Ensure the restored rectangle is visible on at least one connected screen.
    bool onAnyScreen = false;
    for (QScreen* scr : QGuiApplication::screens()) {
        if (scr->availableGeometry().intersects(saved)) {
            onAnyScreen = true;
            break;
        }
    }
    if (onAnyScreen) {
        setGeometry(saved);
    } else {
        ApplyDefaultWindowSize();
    }
    normalGeometry_ = geometry();
    startMaximized_ = settings.windowMaximized;
}

void MainWindow::SaveCurrentSettings() {
    QRect savedRect = (fullscreen_ || isMaximized() || isMinimized()) ? normalGeometry_ : geometry();
    if (!fullscreen_ && !isMaximized() && playlistPanel_ && playlistPanel_->IsOpen()) {
        // Store the video-only window width so next launch (when the panel starts closed) matches the video.
        savedRect.setWidth(std::max(minimumWidth(), savedRect.width() - playlistPanel_->PanelWidth()));
    }

    Settings settings;
    settings.volume = playback_.volume;
    settings.subtitleEncoding = playback_.subtitleEncoding;
    if (savedRect.width() >= 100 && savedRect.height() >= 100) {
        settings.windowPosition = savedRect;
    }
    settings.videoAdjustments = playback_.adjustments;
    settings.preferences = preferences_;
    settings.windowMaximized = fullscreen_ ? wasMaximizedBeforeFullscreen_ : isMaximized();
    SaveSettings(settings);
}

void MainWindow::RunDialog(const std::function<void()>& show) {
    dialogOpen_ = true;
    idleTimer_->stop();
    hoveredPart_ = ControlBar::Part::None;
    controlBar_->SetHoveredPart(hoveredPart_);
    controlBar_->SetBarVisible(false);
    SetCursorHidden(false);

    show();

    dialogOpen_ = false;
    ShowControls();
}

void MainWindow::OpenVideoDialog() {
    QStringList paths;
    RunDialog([this, &paths] { paths = PickVideoFiles(this); });
    if (!paths.isEmpty()) {
        OpenFiles(paths);
    }
}

void MainWindow::OpenSettings() {
    RunDialog([this] {
        ShowSettingsDialog(this, preferences_, [this](const Preferences& preferences) { ApplyPreferences(preferences); });
    });
}

void MainWindow::ShowChapterList() {
    if (playback_.chapters.empty()) {
        player_->ShowMessage(QStringLiteral("챕터가 없습니다"));
        return;
    }
    const PopupPlace place = PopupPlaceForKeys();

    menuOpen_ = true;
    SetCursorHidden(false);
    ShowControls();
    const std::optional<size_t> picked = ShowChapterMenu(this, place, playback_);
    menuOpen_ = false;
    ShowControls();

    if (picked && *picked < playback_.chapters.size()) {
        const Chapter& chapter = playback_.chapters[*picked];
        SeekWithPreview(chapter.time, true);
        player_->ShowMessage(QStringLiteral("챕터 %1/%2: %3")
                                 .arg(*picked + 1)
                                 .arg(playback_.chapters.size())
                                 .arg(chapter.title));
    }
}

void MainWindow::ApplyPreferences(const Preferences& preferences) {
    if (!(preferences.subtitleStyle == preferences_.subtitleStyle)) {
        player_->SetSubtitleStyle(preferences.subtitleStyle);
    }
    preferences_ = preferences;
    if (playlistPanel_) {
        playlistPanel_->SetToggleShortcut(preferences_.shortcuts[static_cast<size_t>(ShortcutAction::PlaylistShow)]);
    }
    SaveCurrentSettings();
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    LayoutChildren();
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    if (!fullscreen_ && !isMaximized() && !isMinimized()) {
        normalGeometry_ = geometry();
    }
    LayoutChildren();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!fullscreen_ && !isMaximized() && !isMinimized()) {
        normalGeometry_ = geometry();
    }
    LayoutChildren();
}

void MainWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized() && controlBar_) {
        controlBar_->SetBarVisible(false);
    }
}

void MainWindow::hideEvent(QHideEvent* event) {
    if (controlBar_) {
        controlBar_->SetBarVisible(false);
    }
    QWidget::hideEvent(event);
}

void MainWindow::LayoutChildren() {
    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) {
        return;
    }

    const bool panelOpen = playlistPanel_ && playlistPanel_->IsOpen();
    const int panelW = panelOpen
        ? std::min(playlistPanel_->PanelWidth(), std::max(PlaylistPanel::kMinPanelWidth, w / 2))
        : 0;

    if (playlistPanel_) {
        playlistPanel_->SetOverlayMode(fullscreen_);
    }

    if (fullscreen_) {
        // In fullscreen, the video fills 100% of the screen and PlaylistPanel overlays on the right.
        if (videoWidget_) {
            videoWidget_->setGeometry(0, 0, w, h);
        }
        if (panelOpen) {
            playlistPanel_->setGeometry(w - panelW, 0, panelW, h);
            playlistPanel_->raise();
        }
    } else {
        // In windowed/maximized mode, the video and PlaylistPanel sit side-by-side.
        const int videoW = std::max(0, w - panelW);
        if (videoWidget_) {
            videoWidget_->setGeometry(0, 0, videoW, h);
        }
        if (panelOpen) {
            playlistPanel_->setGeometry(videoW, 0, panelW, h);
        }
    }

    if (controlBar_) {
        const int uncoveredW = std::max(0, w - panelW);
        const int barH = std::min(h, ControlBar::kBarHeight);
        const QRect localBarRect(0, h - barH, uncoveredW, barH);
        controlBar_->SetTargetGeometry(localBarRect, h);
        controlBar_->raise();
    }
    if (panelOpen && fullscreen_) {
        playlistPanel_->raise();
    }
}

void MainWindow::ShowControls() {
    controlBar_->SetBarVisible(true);
    idleTimer_->start(kIdleDelayMs);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (HandleKeyDown(event)) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MainWindow::HandleKeyDown(QKeyEvent* event) {
    const bool isRepeat = event->isAutoRepeat();
    const KeyChord chord = ChordFromKeyEvent(event);
    const std::optional<ShortcutAction> action = FindShortcut(preferences_.shortcuts, chord);

    if (chord == Qt::Key_Escape && playlistPanel_ && playlistPanel_->IsOpen() && action != ShortcutAction::PlaylistShow) {
        if (!isRepeat) {
            SetPlaylistPanelOpen(false);
        }
        return true;
    }
    if (chord == Qt::Key_Return && currentVideoPath_.isEmpty() && playback_.fileName.isEmpty()) {
        if (!isRepeat) {
            OpenVideoDialog();
        }
        return true;
    }
    if (!action) {
        return false;
    }
    if (!isRepeat || GetShortcutInfo(*action).repeats) {
        RunShortcut(*action);
    }
    return true;
}

void MainWindow::RunShortcut(ShortcutAction action) {
    switch (action) {
    case ShortcutAction::TogglePause:
        player_->TogglePause();
        ShowControls();
        break;
    case ShortcutAction::SeekBackward:
        player_->SeekBy(-preferences_.seekSeconds);
        ShowControls();
        break;
    case ShortcutAction::SeekForward:
        player_->SeekBy(preferences_.seekSeconds);
        ShowControls();
        break;
    case ShortcutAction::VolumeUp:
        player_->ChangeVolume(kVolumeStep);
        ShowControls();
        break;
    case ShortcutAction::VolumeDown:
        player_->ChangeVolume(-kVolumeStep);
        ShowControls();
        break;
    case ShortcutAction::ToggleFullscreen:
        ToggleFullscreen();
        break;
    case ShortcutAction::ExitFullscreen:
        SetFullscreen(false);
        break;
    case ShortcutAction::CycleSubtitle:
        CycleSubtitle();
        break;
    case ShortcutAction::CycleAudio:
        CycleAudio();
        break;
    case ShortcutAction::SubtitleDelayEarlier:
        AdjustSubtitleDelay(-kSubtitleDelayStep);
        break;
    case ShortcutAction::SubtitleDelayLater:
        AdjustSubtitleDelay(kSubtitleDelayStep);
        break;
    case ShortcutAction::SubtitleDelayReset:
        announceSubtitleDelay_ = true;
        player_->ResetSubtitleDelay();
        break;
    case ShortcutAction::SpeedUp:
        ChangeSpeed(kSpeedStep);
        break;
    case ShortcutAction::SpeedDown:
        ChangeSpeed(-kSpeedStep);
        break;
    case ShortcutAction::SpeedReset:
        SetSpeed(1.0);
        break;
    case ShortcutAction::LoopStart:
        SetLoopStart();
        break;
    case ShortcutAction::LoopEnd:
        SetLoopEnd();
        break;
    case ShortcutAction::LoopClear:
        ClearLoop();
        break;
    case ShortcutAction::FrameForward:
        StepFrame(true);
        break;
    case ShortcutAction::FrameBackward:
        StepFrame(false);
        break;
    case ShortcutAction::BrightnessDown:
        AdjustVideo(VideoAdjustment::Brightness, -kVideoAdjustStep);
        break;
    case ShortcutAction::BrightnessUp:
        AdjustVideo(VideoAdjustment::Brightness, kVideoAdjustStep);
        break;
    case ShortcutAction::ContrastDown:
        AdjustVideo(VideoAdjustment::Contrast, -kVideoAdjustStep);
        break;
    case ShortcutAction::ContrastUp:
        AdjustVideo(VideoAdjustment::Contrast, kVideoAdjustStep);
        break;
    case ShortcutAction::SaturationDown:
        AdjustVideo(VideoAdjustment::Saturation, -kVideoAdjustStep);
        break;
    case ShortcutAction::SaturationUp:
        AdjustVideo(VideoAdjustment::Saturation, kVideoAdjustStep);
        break;
    case ShortcutAction::HueDown:
        AdjustVideo(VideoAdjustment::Hue, -kVideoAdjustStep);
        break;
    case ShortcutAction::HueUp:
        AdjustVideo(VideoAdjustment::Hue, kVideoAdjustStep);
        break;
    case ShortcutAction::GammaDown:
        AdjustVideo(VideoAdjustment::Gamma, -kVideoAdjustStep);
        break;
    case ShortcutAction::GammaUp:
        AdjustVideo(VideoAdjustment::Gamma, kVideoAdjustStep);
        break;
    case ShortcutAction::VideoReset:
        ResetVideo();
        break;
    case ShortcutAction::AudioDelayEarlier:
        AdjustAudioDelay(-kAudioDelayStep);
        break;
    case ShortcutAction::AudioDelayLater:
        AdjustAudioDelay(kAudioDelayStep);
        break;
    case ShortcutAction::AudioDelayReset:
        ResetAudioDelay();
        break;
    case ShortcutAction::ChapterList:
        ShowChapterList();
        break;
    case ShortcutAction::ShowInfo:
        RunDialog([this] {
            ShowInfoDialog(this, *player_, playback_, preferences_.shortcuts[static_cast<size_t>(ShortcutAction::ShowInfo)]);
        });
        break;
    case ShortcutAction::OpenSettings:
        OpenSettings();
        break;
    case ShortcutAction::WindowSize50:
        SetWindowSizePercent(50);
        break;
    case ShortcutAction::WindowSize100:
        SetWindowSizePercent(100);
        break;
    case ShortcutAction::WindowSize150:
        SetWindowSizePercent(150);
        break;
    case ShortcutAction::WindowSize200:
        SetWindowSizePercent(200);
        break;
    case ShortcutAction::PlaylistShow:
        TogglePlaylistPanel();
        break;
    case ShortcutAction::PlaylistNext:
        PlayNext(false);
        break;
    case ShortcutAction::PlaylistPrevious:
        PlayPrevious();
        break;
    case ShortcutAction::Count:
        break;
    }
}

void MainWindow::ChangeSpeed(double delta) {
    player_->ShowMessage(QString::asprintf("배속 %g배", player_->ChangeSpeed(delta)));
}

void MainWindow::SetSpeed(double speed) {
    player_->SetSpeed(speed);
    player_->ShowMessage(QString::asprintf("배속 %g배", speed));
}

void MainWindow::SetLoopStart() {
    announceLoop_ = true;
    player_->SetLoopStart();
    ShowControls();
}

void MainWindow::SetLoopEnd() {
    announceLoop_ = true;
    restartLoopPending_ = true;
    player_->SetLoopEnd();
    ShowControls();
}

void MainWindow::ClearLoop() {
    announceLoop_ = true;
    restartLoopPending_ = false;
    player_->ClearLoop();
    ShowControls();
}

void MainWindow::StepFrame(bool forward) {
    player_->StepFrame(forward);
    ShowControls();
}

void MainWindow::SetAspect(AspectMode aspect) {
    player_->SetAspect(aspect);
    player_->ShowMessage(QStringLiteral("화면 비율: ") + AspectLabel(aspect));
}

void MainWindow::SetRotation(int degrees) {
    player_->SetRotation(degrees);
    player_->ShowMessage(degrees == 0 ? QStringLiteral("회전 안 함") : QStringLiteral("회전 %1도").arg(degrees));
}

void MainWindow::AdjustVideo(VideoAdjustment adjustment, int delta) {
    const int value = player_->ChangeVideoAdjustment(adjustment, delta);
    player_->ShowMessage(QStringLiteral("%1 %2").arg(VideoAdjustmentLabel(adjustment), QString::asprintf("%+d", value)));
}

void MainWindow::ResetVideo() {
    player_->ResetVideoAdjustments();
    player_->ShowMessage(QStringLiteral("영상 조정 초기화"));
}

void MainWindow::AdjustAudioDelay(double seconds) {
    player_->ShowMessage(QString::asprintf("음성 싱크 %+.1f초", player_->ChangeAudioDelay(seconds)));
}

void MainWindow::ResetAudioDelay() {
    player_->ResetAudioDelay();
    player_->ShowMessage(QStringLiteral("음성 싱크 +0.0초"));
}

void MainWindow::CycleSubtitle() {
    const MediaTrack* first = nullptr;
    const MediaTrack* next = nullptr;
    bool passedSelected = false;
    for (const MediaTrack& track : playback_.tracks) {
        if (track.type != TrackType::Subtitle) {
            continue;
        }
        if (!first) {
            first = &track;
        }
        if (passedSelected && !next) {
            next = &track;
        }
        passedSelected = passedSelected || track.selected;
    }
    if (!first) {
        player_->ShowMessage(QStringLiteral("자막 없음"));
    } else if (next || !passedSelected) {
        const MediaTrack& target = next ? *next : *first;
        player_->SelectSubtitle(target.id);
        player_->ShowMessage(QStringLiteral("자막: ") + TrackLabel(target));
    } else {
        player_->SelectSubtitle(std::nullopt);
        player_->ShowMessage(QStringLiteral("자막 끔"));
    }
}

void MainWindow::CycleAudio() {
    const MediaTrack* first = nullptr;
    const MediaTrack* next = nullptr;
    bool passedSelected = false;
    for (const MediaTrack& track : playback_.tracks) {
        if (track.type != TrackType::Audio) {
            continue;
        }
        if (!first) {
            first = &track;
        }
        if (passedSelected && !next) {
            next = &track;
        }
        passedSelected = passedSelected || track.selected;
    }
    if (const MediaTrack* target = next ? next : first) {
        player_->SelectAudio(target->id);
        player_->ShowMessage(QStringLiteral("오디오: ") + TrackLabel(*target));
    } else {
        player_->ShowMessage(QStringLiteral("오디오 없음"));
    }
}

void MainWindow::AdjustSubtitleDelay(double seconds) {
    announceSubtitleDelay_ = true;
    player_->ChangeSubtitleDelay(seconds);
}

void MainWindow::contextMenuEvent(QContextMenuEvent* event) {
    if (playlistPanel_ && playlistPanel_->IsOpen() && playlistPanel_->geometry().contains(event->pos())) {
        return;
    }
    HandleContextMenu(event->globalPos());
    event->accept();
}

void MainWindow::HandleContextMenu(QPoint globalPoint) {
    menuOpen_ = true;
    SetCursorHidden(false);
    const MenuChoice choice = ShowContextMenu(this, globalPoint, playback_, preferences_.shortcuts);
    menuOpen_ = false;
    ShowControls();

    switch (choice.action) {
    case MenuChoice::Action::OpenVideo:
        OpenVideoDialog();
        break;
    case MenuChoice::Action::OpenSubtitle: {
        const QString path = PickSubtitleFile(this);
        if (!path.isEmpty()) {
            AddSubtitle(path);
        }
        break;
    }
    case MenuChoice::Action::SelectSubtitle:
        player_->SelectSubtitle(choice.trackId);
        break;
    case MenuChoice::Action::SelectAudio:
        if (choice.trackId) {
            player_->SelectAudio(*choice.trackId);
        }
        break;
    case MenuChoice::Action::ResetSubtitleDelay:
        announceSubtitleDelay_ = true;
        player_->ResetSubtitleDelay();
        break;
    case MenuChoice::Action::SetSubtitleEncoding:
        player_->SetSubtitleEncoding(choice.encoding);
        break;
    case MenuChoice::Action::OpenSettings:
        OpenSettings();
        break;
    case MenuChoice::Action::SetSpeed:
        SetSpeed(choice.speed);
        break;
    case MenuChoice::Action::LoopStart:
        SetLoopStart();
        break;
    case MenuChoice::Action::LoopEnd:
        SetLoopEnd();
        break;
    case MenuChoice::Action::ClearLoop:
        ClearLoop();
        break;
    case MenuChoice::Action::StepForward:
        StepFrame(true);
        break;
    case MenuChoice::Action::StepBackward:
        StepFrame(false);
        break;
    case MenuChoice::Action::SetAspect:
        SetAspect(choice.aspect);
        break;
    case MenuChoice::Action::SetRotation:
        SetRotation(choice.rotation);
        break;
    case MenuChoice::Action::AdjustVideo:
        AdjustVideo(choice.adjustment, choice.adjustmentDirection * kVideoAdjustStep);
        break;
    case MenuChoice::Action::ResetVideo:
        ResetVideo();
        break;
    case MenuChoice::Action::ResetAudioDelay:
        ResetAudioDelay();
        break;
    case MenuChoice::Action::None:
        break;
    }
}

void MainWindow::wheelEvent(QWheelEvent* event) {
    wheelRemainder_ += event->angleDelta().y();
    const int notches = wheelRemainder_ / 120;
    wheelRemainder_ %= 120;
    if (notches != 0) {
        player_->ChangeVolume(notches * kVolumeStep);
        ShowControls();
    }
    event->accept();
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        HandleLeftButtonDown(event->position().toPoint(), event->globalPosition().toPoint());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint point = event->position().toPoint();
        if (controlBar_->HitTest(point) == ControlBar::Part::None) {
            ToggleFullscreen();
        } else {
            HandleLeftButtonDown(point, event->globalPosition().toPoint());
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    const QPoint point = event->position().toPoint();
    const QPoint globalPos = event->globalPosition().toPoint();

    if (drag_ == Drag::Window) {
        if ((point - dragStart_).manhattanLength() >= QApplication::startDragDistance()) {
            if (windowHandle() && windowHandle()->startSystemMove()) {
                drag_ = Drag::None;
                return;
            }
            if (!isMaximized()) {
                move(windowStartPos_ + (globalPos - dragStartGlobal_));
            }
        }
        return;
    }
    if (drag_ == Drag::Seek) {
        lastDragX_ = point.x();
        SeekToPointer(point.x(), false);
        return;
    }
    if (drag_ == Drag::Volume) {
        SetVolumeFromPointer(point.x());
        return;
    }

    if (globalPos == lastGlobalCursorPosition_) {
        return;
    }
    lastGlobalCursorPosition_ = globalPos;

    SetCursorHidden(false);
    ShowControls();
    hoveredPart_ = controlBar_->HitTest(point);
    controlBar_->SetHoveredPart(hoveredPart_);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        HandleLeftButtonUp(event->position().toPoint());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::leaveEvent(QEvent* event) {
    hoveredPart_ = ControlBar::Part::None;
    controlBar_->SetHoveredPart(hoveredPart_);
    if (drag_ == Drag::None && !menuOpen_ && !playback_.paused) {
        idleTimer_->stop();
        controlBar_->SetBarVisible(false);
    }
    lastGlobalCursorPosition_ = QCursor::pos();
    QWidget::leaveEvent(event);
}

void MainWindow::HandleLeftButtonDown(QPoint point, QPoint globalPoint) {
    setFocus();
    switch (controlBar_->HitTest(point)) {
    case ControlBar::Part::PlayPause:
        player_->TogglePause();
        ShowControls();
        break;
    case ControlBar::Part::SeekTrack:
        drag_ = Drag::Seek;
        lastDragX_ = point.x();
        SeekToPointer(point.x(), false);
        break;
    case ControlBar::Part::VolumeTrack:
        drag_ = Drag::Volume;
        SetVolumeFromPointer(point.x());
        break;
    case ControlBar::Part::Fullscreen:
        ToggleFullscreen();
        break;
    case ControlBar::Part::None:
    case ControlBar::Part::Background:
        if (!fullscreen_) {
            drag_ = Drag::Window;
            dragStart_ = point;
            dragStartGlobal_ = globalPoint;
            windowStartPos_ = pos();
        }
        break;
    }
}

void MainWindow::HandleLeftButtonUp(QPoint point) {
    if (drag_ == Drag::None) {
        return;
    }
    const Drag finished = drag_;
    drag_ = Drag::None;
    if (finished == Drag::Seek) {
        SeekToPointer(point.x(), true);
    } else if (finished == Drag::Volume) {
        SetVolumeFromPointer(point.x());
    }
    ShowControls();
}

void MainWindow::SetVolumeFromPointer(int clientX) {
    player_->SetVolume(std::round(controlBar_->VolumeAt(clientX)));
}

void MainWindow::SeekToPointer(int clientX, bool isFinal) {
    if (!playback_.duration) {
        return;
    }
    SeekWithPreview(controlBar_->SeekFractionAt(clientX) * *playback_.duration, isFinal);
}

void MainWindow::SeekWithPreview(double target, bool exact) {
    player_->SeekTo(target, exact);
    seekPreview_ = target;
    seekPreviewExpiryMs_ = NowMs() + kSeekPreviewExpiryDurationMs;
    controlBar_->SetSeekPreview(seekPreview_);
}

void MainWindow::HandleIdleTimeout() {
    if (drag_ != Drag::None || menuOpen_ || hoveredPart_ != ControlBar::Part::None) {
        return;
    }
    if (!playback_.paused || dialogOpen_) {
        controlBar_->SetBarVisible(false);
    }
    if (fullscreen_ && !dialogOpen_ && !(playlistPanel_ && playlistPanel_->IsOpen())) {
        SetCursorHidden(true);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        OpenFiles(paths);
        ActivateAndRaise();
        event->acceptProposedAction();
    }
}

void MainWindow::HandleStateChanged(const PlaybackState& state) {
    if (!openRetryPath_.isEmpty() && state.position && *state.position > 0.0 && !state.fileName.isEmpty() &&
        QFileInfo(openRetryPath_).fileName().compare(state.fileName, Qt::CaseInsensitive) == 0) {
        ClearOpenRetry();
    }
    const bool reachedEnd = state.atEnd && !playback_.atEnd && !state.fileName.isEmpty();
    const bool delayChanged = state.subtitleDelay != playback_.subtitleDelay;
    const bool loopChanged = state.loopStart != playback_.loopStart || state.loopEnd != playback_.loopEnd;
    playback_ = state;
    UpdateTitle(state);

    if (delayChanged && announceSubtitleDelay_) {
        announceSubtitleDelay_ = false;
        player_->ShowMessage(QString::asprintf("자막 싱크 %+.1f초", state.subtitleDelay));
    }

    if (state.loopStart && state.loopEnd && *state.loopEnd < *state.loopStart) {
        restartLoopPending_ = true;
        player_->SetLoop(*state.loopEnd, *state.loopStart);
        return;
    }
    if (loopChanged && restartLoopPending_ && state.loopEnd) {
        restartLoopPending_ = false;
        if (!state.paused) {
            player_->SeekTo(state.loopStart.value_or(0.0), true);
        }
    }
    if (loopChanged && announceLoop_) {
        announceLoop_ = false;
        if (state.loopStart && state.loopEnd) {
            player_->ShowMessage(QStringLiteral("구간 반복 %1 ~ %2").arg(FormatTime(*state.loopStart), FormatTime(*state.loopEnd)));
        } else if (state.loopStart) {
            player_->ShowMessage(QStringLiteral("구간 시작 ") + FormatTime(*state.loopStart));
        } else if (state.loopEnd) {
            player_->ShowMessage(QStringLiteral("구간 끝 ") + FormatTime(*state.loopEnd));
        } else {
            player_->ShowMessage(QStringLiteral("구간 반복 해제"));
        }
    }

    if (seekPreview_ && drag_ != Drag::Seek) {
        const bool arrived = state.position && std::abs(*state.position - *seekPreview_) < kSeekPreviewToleranceSeconds;
        if (arrived || NowMs() >= seekPreviewExpiryMs_) {
            seekPreview_.reset();
            controlBar_->SetSeekPreview(std::nullopt);
        }
    }
    controlBar_->SetPlayback(state);

    if (reachedEnd && preferences_.autoAdvance && preferences_.repeat != RepeatMode::One) {
        PlayNext(true);
    }
}

void MainWindow::ToggleFullscreen() {
    SetFullscreen(!fullscreen_);
}

void MainWindow::SetFullscreen(bool fullscreen) {
    if (fullscreen == fullscreen_) {
        return;
    }

    if (fullscreen) {
        wasMaximizedBeforeFullscreen_ = isMaximized();
        if (!wasMaximizedBeforeFullscreen_) {
            normalGeometry_ = geometry();
        }
        fullscreen_ = true;
        showFullScreen();
    } else {
        SetCursorHidden(false);
        fullscreen_ = false;
        if (wasMaximizedBeforeFullscreen_) {
            showMaximized();
        } else {
            showNormal();
            if (normalGeometry_.isValid()) {
                setGeometry(normalGeometry_);
            }
        }
    }
    controlBar_->SetFullscreenState(fullscreen_);
    LayoutChildren();
    ShowControls();
}

void MainWindow::SetCursorHidden(bool hidden) {
    if (hidden == cursorHidden_) {
        return;
    }
    cursorHidden_ = hidden;
    if (hidden) {
        setCursor(Qt::BlankCursor);
    } else {
        unsetCursor();
    }
}

void MainWindow::UpdateTitle(const PlaybackState& state) {
    QString title = QString::fromUtf8(kAppTitle);
    if (!state.fileName.isEmpty()) {
        // The file name goes last: long names get cut off in the title bar.
        title = FormatTime(state.position.value_or(0.0));
        if (state.duration) {
            title += QStringLiteral(" / ") + FormatTime(*state.duration);
        }
        title += QStringLiteral(" | 볼륨 ") + QString::number(std::lround(state.volume)) + QLatin1Char('%');
        if (state.paused) {
            title += QStringLiteral(" | 일시정지");
        }
        title += QStringLiteral(" | ") + state.fileName;
    }
    if (title != title_) {
        title_ = title;
        setWindowTitle(title_);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    SaveCurrentSettings();
    ClearOpenRetry();
    if (controlBar_) {
        controlBar_->hide();
    }
    if (videoWidget_) {
        videoWidget_->Shutdown();
    }
    // mpv renders into videoWidget_, so destroy player_ before the window goes away.
    delete player_;
    player_ = nullptr;
    QDir(subtitleTempDirectory_).removeRecursively();
    QWidget::closeEvent(event);
}
