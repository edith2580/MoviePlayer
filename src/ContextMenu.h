#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

#include "MpvPlayer.h"
#include "Playlist.h"
#include "Shortcuts.h"

class QWidget;

// What the user picked from the right-click menu.
struct MenuChoice {
    enum class Action {
        None,
        OpenVideo,
        OpenSubtitle,
        SelectSubtitle,  // trackId empty means subtitles off.
        SelectAudio,
        ResetSubtitleDelay,
        SetSubtitleEncoding,
        OpenSettings,
        SetSpeed,
        LoopStart,
        LoopEnd,
        ClearLoop,
        StepForward,
        StepBackward,
        SetAspect,
        SetRotation,
        AdjustVideo,
        ResetVideo,
        ResetAudioDelay,
    };
    Action action = Action::None;
    std::optional<int64_t> trackId;
    SubtitleEncoding encoding = SubtitleEncoding::Korean;
    double speed = 1.0;
    AspectMode aspect = AspectMode::Original;
    int rotation = 0;
    VideoAdjustment adjustment = VideoAdjustment::Brightness;
    int adjustmentDirection = 0;  // +1 or -1.
};

// Shows the menu at a global screen position and blocks until it closes. Playback keeps running.
MenuChoice ShowContextMenu(QWidget* owner, QPoint screenPoint, const PlaybackState& state, const ShortcutTable& shortcuts);

// Where a popup list opens: with its top left corner at a screen point, or centered on it.
struct PopupPlace {
    QPoint point{};
    bool centered = false;
};

// Shows the chapters and blocks until the menu closes. Returns the index of the one picked.
std::optional<size_t> ShowChapterMenu(QWidget* owner, PopupPlace place, const PlaybackState& state);

struct PlaylistChoice {
    enum class Action { None, Play, ToggleAutoAdvance, ToggleRepeatOne, ToggleRepeatAll };
    Action action = Action::None;
    size_t index = 0;  // The file to play.
};

// Shows the playlist and its play options, and blocks until the menu closes.
PlaylistChoice ShowPlaylistMenu(QWidget* owner, PopupPlace place, const QStringList& files, size_t current,
                                bool autoAdvance, RepeatMode repeat);

// A short human-readable name for a track, for menus and on-screen messages.
QString TrackLabel(const MediaTrack& track);

// The same for other settings.
QString AspectLabel(AspectMode aspect);
QString VideoAdjustmentLabel(VideoAdjustment adjustment);
