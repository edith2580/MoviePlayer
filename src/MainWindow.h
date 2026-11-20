#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

#include "ContextMenu.h"
#include "ControlBar.h"
#include "MpvPlayer.h"
#include "PlaylistPanel.h"
#include "Settings.h"

class MpvVideoWidget;
class QProcess;
class QTimer;

// The top-level window. It takes all keyboard and mouse input and turns it into
// player commands, and decides when the control bar and hybrid playlist panel are shown.
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(const Settings& settings, QWidget* parent = nullptr);
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void ShowInitial();
    void ActivateAndRaise();

    // Opens a video, or adds a subtitle to the current one, depending on the file type.
    void Open(const QString& path);
    // The same for a file given from outside, by Explorer/Finder or the command line. There a
    // subtitle means "play this", so it opens its video, not just adds to what's playing.
    void OpenFromExplorer(const QString& path);
    // Several files make a playlist. One video becomes a playlist of its series.
    void OpenFiles(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    bool HandleKeyDown(QKeyEvent* event);
    void RunShortcut(ShortcutAction action);
    void HandleLeftButtonDown(QPoint point, QPoint globalPoint);
    void HandleLeftButtonUp(QPoint point);
    void HandleIdleTimeout();
    void HandleContextMenu(QPoint globalPoint);
    void HandleStateChanged(const PlaybackState& state);

    // Runs a blocking modal dialog over the video, keeping the bar and cursor out of its way.
    void RunDialog(const std::function<void()>& show);
    void OpenVideoDialog();
    void OpenSettings();
    void ShowChapterList();
    // Starts a new playlist at index, and looks for the rest of the series if asked to.
    void StartPlaylist(QStringList files, size_t index, const QString& preferredSubtitle, bool findSeries);
    void PlayPlaylistItem(size_t index);
    void PlayNext(bool automatic);
    void PlayPrevious();
    void ShowPlaylist();  // Popup menu fallback.
    void TogglePlaylistPanel();
    void SetPlaylistPanelOpen(bool open);
    void SyncPlaylistPanel();
    void AddFilesToPlaylist(const QStringList& paths);
    void ToggleAutoAdvance();
    void ToggleRepeatOne();
    void ToggleRepeatAll();
    // At the cursor, or centered on the window if the cursor is outside it.
    PopupPlace PopupPlaceForKeys() const;
    void FindSeriesInBackground();
    void HandleSeriesFound(unsigned search, const QString& video, const QStringList& series);
    void ApplyRepeatMode();
    void SeekWithPreview(double target, bool exact);
    void ApplyPreferences(const Preferences& preferences);
    // preferredSubtitle, if given, is selected over any other subtitle once the video is ready.
    void OpenVideo(const QString& path, const QString& preferredSubtitle = {});
    void AddSubtitle(const QString& path);
    void RestoreWindowPosition(const Settings& settings);
    void SaveCurrentSettings();
    void LayoutChildren();
    void ShowControls();
    void CycleSubtitle();
    void CycleAudio();
    void AdjustSubtitleDelay(double seconds);
    void ChangeSpeed(double delta);
    void SetSpeed(double speed);
    void SetLoopStart();
    void SetLoopEnd();
    void ClearLoop();
    void StepFrame(bool forward);
    void SetAspect(AspectMode aspect);
    void SetRotation(int degrees);
    void AdjustVideo(VideoAdjustment adjustment, int delta);
    void ResetVideo();
    void AdjustAudioDelay(double seconds);
    void ResetAudioDelay();
    void SetVolumeFromPointer(int clientX);
    void ApplyDefaultWindowSize();
    void ArmOpenRetry(const QString& path, const QString& preferredSubtitle = {});
    void ClearOpenRetry();
    void HandleVideoReady(int width, int height);
    void HandlePlaybackError(const QString& reason, bool unrecognizedFormat);
    // Sizes the window to show the video at this scale, as far as the monitor allows.
    void FitWindowToVideo(double requestedScale, bool center);
    void SetWindowSizePercent(int percent);
    void SeekToPointer(int clientX, bool isFinal);
    void ToggleFullscreen();
    void SetFullscreen(bool fullscreen);
    void UpdateSleepInhibition();
    void SetDisplaySleepInhibited(bool inhibit);
    void SetCursorHidden(bool hidden);
    void UpdateTitle(const PlaybackState& state);

    MpvPlayer* player_ = nullptr;
    MpvVideoWidget* videoWidget_ = nullptr;
    ControlBar* controlBar_ = nullptr;
    PlaylistPanel* playlistPanel_ = nullptr;

    QTimer* idleTimer_ = nullptr;
    QTimer* openRetryTimer_ = nullptr;
    QTimer* seriesTimer_ = nullptr;

    PlaybackState playback_;
    Preferences preferences_;

    QString subtitleTempDirectory_;
    QString currentVideoPath_;
    QString pendingSubtitleName_;  // The file name of the subtitle to select once the video is ready.

    QStringList playlist_;  // Not saved: it's gone when the app closes.
    size_t playlistIndex_ = 0;
    bool keepWindowSize_ = false;  // The next video comes from the playlist, so the window stays as it is.
    qint64 lastExplorerOpenMs_ = 0;  // When a video last came from Explorer, to group a multi-file selection.
    unsigned seriesSearch_ = 0;      // Counts searches, so a late result for an old playlist is ignored.

    QString openRetryPath_;  // Video being opened, retried on transient probe failures (e.g. network drives).
    QString openRetrySubtitle_;
    size_t openRetryCount_ = 0;

    bool startMaximized_ = false;
    bool centerOnFirstFit_ = false;
    int videoWidth_ = 0;  // The open video's own size. 0 if it has none, or isn't ready yet.
    int videoHeight_ = 0;

    bool fullscreen_ = false;
    bool wasMaximizedBeforeFullscreen_ = false;
    QRect normalGeometry_;
    bool sleepInhibited_ = false;
#if defined(Q_OS_MACOS)
    uint32_t macSleepAssertionId_ = 0;
#elif defined(Q_OS_LINUX)
    QProcess* linuxSleepInhibitProcess_ = nullptr;
#endif

    bool cursorHidden_ = false;
    QPoint lastGlobalCursorPosition_{};
    ControlBar::Part hoveredPart_ = ControlBar::Part::None;
    int wheelRemainder_ = 0;

    bool menuOpen_ = false;
    bool dialogOpen_ = false;
    bool announceSubtitleDelay_ = false;
    bool announceLoop_ = false;
    bool restartLoopPending_ = false;  // Playback is at the loop's new end, so it should go back to the start.

    enum class Drag { None, Seek, Volume, Window };
    Drag drag_ = Drag::None;
    QPoint dragStart_{};
    QPoint dragStartGlobal_{};
    QPoint windowStartPos_{};
    int lastDragX_ = 0;
    std::optional<double> seekPreview_;
    qint64 seekPreviewExpiryMs_ = 0;

    QString title_;
};
