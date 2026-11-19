#pragma once

#include <QKeyEvent>
#include <QString>
#include <array>
#include <optional>

// Everything a keyboard shortcut can do. AllShortcuts() lists them in this order.
enum class ShortcutAction {
    TogglePause,
    SeekBackward,
    SeekForward,
    VolumeUp,
    VolumeDown,
    ToggleFullscreen,
    ExitFullscreen,
    CycleSubtitle,
    CycleAudio,
    SubtitleDelayEarlier,
    SubtitleDelayLater,
    SubtitleDelayReset,
    SpeedUp,
    SpeedDown,
    SpeedReset,
    LoopStart,
    LoopEnd,
    LoopClear,
    FrameForward,
    FrameBackward,
    BrightnessDown,
    BrightnessUp,
    ContrastDown,
    ContrastUp,
    SaturationDown,
    SaturationUp,
    HueDown,
    HueUp,
    GammaDown,
    GammaUp,
    VideoReset,
    AudioDelayEarlier,
    AudioDelayLater,
    AudioDelayReset,
    ChapterList,
    ShowInfo,
    OpenSettings,
    WindowSize50,
    WindowSize100,
    WindowSize150,
    WindowSize200,
    PlaylistShow,
    PlaylistNext,
    PlaylistPrevious,
    Count,
};
inline constexpr size_t kShortcutCount = static_cast<size_t>(ShortcutAction::Count);

// A Qt key combined with Ctrl/Shift/Alt modifier flags (QKeyCombination::toCombined()).
using KeyChord = int;
inline constexpr KeyChord kNoKey = 0;
inline constexpr KeyChord kCtrlFlag = static_cast<int>(Qt::ControlModifier);
inline constexpr KeyChord kShiftFlag = static_cast<int>(Qt::ShiftModifier);
inline constexpr KeyChord kAltFlag = static_cast<int>(Qt::AltModifier);

using ShortcutTable = std::array<KeyChord, kShortcutCount>;

struct ShortcutInfo {
    const char* settingsName;
    const char* label;
    KeyChord defaultKey;
    bool repeats;  // Whether holding the key down keeps doing it.
};

const std::array<ShortcutInfo, kShortcutCount>& AllShortcuts();
const ShortcutInfo& GetShortcutInfo(ShortcutAction action);
ShortcutTable DefaultShortcuts();

// Extracts the normalized key + modifiers from a Qt key event.
KeyChord ChordFromKeyEvent(const QKeyEvent* event);
// Converts legacy Win32 virtual-key chords from older settings.ini files if needed.
KeyChord NormalizeLoadedChord(int rawChord);

std::optional<ShortcutAction> FindShortcut(const ShortcutTable& shortcuts, KeyChord chord);

// For display, such as "Ctrl+←". Empty for kNoKey.
QString KeyChordName(KeyChord chord);

bool IsModifierKey(int qtKey);
// False for keys that the OS or the input method keeps for itself, such as Alt+F4.
bool CanBeShortcut(KeyChord chord);
