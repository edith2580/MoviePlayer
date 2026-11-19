#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "Playlist.h"
#include "Shortcuts.h"

class QLabel;
class QListWidget;
class QListWidgetItem;

// A hybrid playlist panel living inside MainWindow:
// - In windowed/maximized mode, it sits side-by-side on the right edge of the video area.
// - In fullscreen mode, it switches to a translucent dark overlay drawer over the right edge of the video.
class PlaylistPanel : public QWidget {
    Q_OBJECT

public:
    static constexpr int kDefaultPanelWidth = 300;
    static constexpr int kMinPanelWidth = 220;
    static constexpr int kMaxPanelWidth = 640;

    struct Callbacks {
        std::function<void(size_t index)> onPlayItem;
        std::function<void()> onToggleAutoAdvance;
        std::function<void()> onToggleRepeatOne;
        std::function<void()> onToggleRepeatAll;
        std::function<void()> onTogglePause;
        std::function<void(const QStringList& paths)> onFilesDropped;
        std::function<void()> onCloseRequested;
        std::function<void(int newWidth)> onPanelWidthChanged;
    };

    PlaylistPanel(Callbacks callbacks, QWidget* parent = nullptr);
    ~PlaylistPanel() override = default;

    PlaylistPanel(const PlaylistPanel&) = delete;
    PlaylistPanel& operator=(const PlaylistPanel&) = delete;

    void SetOpen(bool open);
    bool IsOpen() const { return open_; }
    bool HasPanelFocus() const;
    void FocusDefaultControl();

    int PanelWidth() const { return panelWidth_; }
    void SetPanelWidth(int width);

    void SetOverlayMode(bool overlay);
    bool IsOverlayMode() const { return overlayMode_; }

    void SetPlaylist(const QStringList& files, size_t currentIndex);
    void SetOptions(bool autoAdvance, RepeatMode repeat);
    void SetToggleShortcut(KeyChord shortcut) { toggleShortcut_ = shortcut; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    class ToggleButton;

    void LayoutControls();
    void ApplyTheme();
    void RefreshList();
    void RefreshHeaderCount();
    void RefreshOptionButtons();
    bool HandleKeyDown(QKeyEvent* keyEvent);

    Callbacks callbacks_;
    ToggleButton* autoAdvanceButton_ = nullptr;
    ToggleButton* repeatOneButton_ = nullptr;
    ToggleButton* repeatAllButton_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* emptyLabel_ = nullptr;

    int panelWidth_ = kDefaultPanelWidth;
    bool open_ = false;
    bool overlayMode_ = false;

    QStringList files_;
    size_t currentIndex_ = 0;
    bool autoAdvance_ = true;
    RepeatMode repeat_ = RepeatMode::None;
    KeyChord toggleShortcut_ = Qt::Key_F6;
};
