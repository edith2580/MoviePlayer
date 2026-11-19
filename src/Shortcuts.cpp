#include "Shortcuts.h"

#include <QKeySequence>

namespace {

constexpr int kModifierMask = kCtrlFlag | kShiftFlag | kAltFlag;
constexpr int kKeyMask = ~static_cast<int>(Qt::KeyboardModifierMask);

constexpr std::array<ShortcutInfo, kShortcutCount> kShortcuts{{
    {"TogglePause", "재생/일시정지", Qt::Key_Space, false},
    {"SeekBackward", "뒤로 이동", Qt::Key_Left, true},
    {"SeekForward", "앞으로 이동", Qt::Key_Right, true},
    {"VolumeUp", "볼륨 높이기", Qt::Key_Up, true},
    {"VolumeDown", "볼륨 낮추기", Qt::Key_Down, true},
    {"ToggleFullscreen", "전체화면 전환", Qt::Key_Return, false},
    {"ExitFullscreen", "전체화면 해제", Qt::Key_Escape, false},
    {"CycleSubtitle", "자막 트랙 전환", Qt::Key_S, false},
    {"CycleAudio", "오디오 트랙 전환", Qt::Key_A, false},
    {"SubtitleDelayEarlier", "자막 싱크 빠르게", Qt::Key_Comma, true},
    {"SubtitleDelayLater", "자막 싱크 느리게", Qt::Key_Period, true},
    {"SubtitleDelayReset", "자막 싱크 초기화", Qt::Key_Slash, false},
    {"SpeedUp", "배속 빠르게", Qt::Key_C, true},
    {"SpeedDown", "배속 느리게", Qt::Key_X, true},
    {"SpeedReset", "배속 원래대로", Qt::Key_Z, false},
    {"LoopStart", "구간 반복 시작 지정", Qt::Key_J, false},
    {"LoopEnd", "구간 반복 끝 지정", Qt::Key_K, false},
    {"LoopClear", "구간 반복 해제", Qt::Key_L, false},
    {"FrameForward", "다음 프레임", Qt::Key_F, true},
    {"FrameBackward", "이전 프레임", Qt::Key_D, true},
    {"BrightnessDown", "밝기 낮추기", Qt::Key_Q, true},
    {"BrightnessUp", "밝기 높이기", Qt::Key_W, true},
    {"ContrastDown", "대비 낮추기", Qt::Key_E, true},
    {"ContrastUp", "대비 높이기", Qt::Key_R, true},
    {"SaturationDown", "채도 낮추기", Qt::Key_T, true},
    {"SaturationUp", "채도 높이기", Qt::Key_Y, true},
    {"HueDown", "색조 낮추기", Qt::Key_U, true},
    {"HueUp", "색조 높이기", Qt::Key_I, true},
    {"GammaDown", "감마 낮추기", Qt::Key_O, true},
    {"GammaUp", "감마 높이기", Qt::Key_P, true},
    {"VideoReset", "영상 조정 초기화", Qt::Key_Backspace, false},
    {"AudioDelayEarlier", "음성 싱크 빠르게", Qt::Key_BracketLeft, true},
    {"AudioDelayLater", "음성 싱크 느리게", Qt::Key_BracketRight, true},
    {"AudioDelayReset", "음성 싱크 초기화", Qt::Key_Backslash, false},
    {"ChapterList", "챕터 목록", Qt::Key_H, false},
    {"ShowInfo", "재생 정보", Qt::Key_F1, false},
    {"OpenSettings", "설정 열기", Qt::Key_F5, false},
    {"WindowSize50", "화면 크기 50%", Qt::Key_1, false},
    {"WindowSize100", "화면 크기 100%", Qt::Key_2, false},
    {"WindowSize150", "화면 크기 150%", Qt::Key_3, false},
    {"WindowSize200", "화면 크기 200%", Qt::Key_4, false},
    {"PlaylistShow", "재생 목록", Qt::Key_F6, false},
    {"PlaylistNext", "다음 파일", Qt::Key_PageDown, false},
    {"PlaylistPrevious", "이전 파일", Qt::Key_PageUp, false},
}};

int UnshiftKey(int key) {
    switch (key) {
    case Qt::Key_Enter: return Qt::Key_Return;
    case Qt::Key_Exclam: return Qt::Key_1;
    case Qt::Key_At: return Qt::Key_2;
    case Qt::Key_NumberSign: return Qt::Key_3;
    case Qt::Key_Dollar: return Qt::Key_4;
    case Qt::Key_Percent: return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand: return Qt::Key_7;
    case Qt::Key_Asterisk: return Qt::Key_8;
    case Qt::Key_ParenLeft: return Qt::Key_9;
    case Qt::Key_ParenRight: return Qt::Key_0;
    case Qt::Key_Underscore: return Qt::Key_Minus;
    case Qt::Key_Plus: return Qt::Key_Equal;
    case Qt::Key_BraceLeft: return Qt::Key_BracketLeft;
    case Qt::Key_BraceRight: return Qt::Key_BracketRight;
    case Qt::Key_Bar: return Qt::Key_Backslash;
    case Qt::Key_Colon: return Qt::Key_Semicolon;
    case Qt::Key_QuoteDbl: return Qt::Key_Apostrophe;
    case Qt::Key_Less: return Qt::Key_Comma;
    case Qt::Key_Greater: return Qt::Key_Period;
    case Qt::Key_Question: return Qt::Key_Slash;
    case Qt::Key_AsciiTilde: return Qt::Key_QuoteLeft;
    default: return key;
    }
}

int LegacyVkToQtKey(int vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return vk;
    }
    if (vk >= 0x70 && vk <= 0x87) {  // VK_F1..VK_F24
        return Qt::Key_F1 + (vk - 0x70);
    }
    switch (vk) {
    case 0x08: return Qt::Key_Backspace;
    case 0x09: return Qt::Key_Tab;
    case 0x0D: return Qt::Key_Return;
    case 0x13: return Qt::Key_Pause;
    case 0x1B: return Qt::Key_Escape;
    case 0x20: return Qt::Key_Space;
    case 0x21: return Qt::Key_PageUp;
    case 0x22: return Qt::Key_PageDown;
    case 0x23: return Qt::Key_End;
    case 0x24: return Qt::Key_Home;
    case 0x25: return Qt::Key_Left;
    case 0x26: return Qt::Key_Up;
    case 0x27: return Qt::Key_Right;
    case 0x28: return Qt::Key_Down;
    case 0x2D: return Qt::Key_Insert;
    case 0x2E: return Qt::Key_Delete;
    case 0xBA: return Qt::Key_Semicolon;
    case 0xBB: return Qt::Key_Equal;
    case 0xBC: return Qt::Key_Comma;
    case 0xBD: return Qt::Key_Minus;
    case 0xBE: return Qt::Key_Period;
    case 0xBF: return Qt::Key_Slash;
    case 0xC0: return Qt::Key_QuoteLeft;
    case 0xDB: return Qt::Key_BracketLeft;
    case 0xDC: return Qt::Key_Backslash;
    case 0xDD: return Qt::Key_BracketRight;
    case 0xDE: return Qt::Key_Apostrophe;
    default: return 0;
    }
}

QString KeyName(int key) {
    switch (key) {
    case Qt::Key_Space: return QStringLiteral("Space");
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("Enter");
    case Qt::Key_Escape: return QStringLiteral("Esc");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return QStringLiteral("Tab");
    case Qt::Key_Left: return QStringLiteral("←");
    case Qt::Key_Right: return QStringLiteral("→");
    case Qt::Key_Up: return QStringLiteral("↑");
    case Qt::Key_Down: return QStringLiteral("↓");
    case Qt::Key_Insert: return QStringLiteral("Insert");
    case Qt::Key_Delete: return QStringLiteral("Delete");
    case Qt::Key_Home: return QStringLiteral("Home");
    case Qt::Key_End: return QStringLiteral("End");
    case Qt::Key_PageUp: return QStringLiteral("PageUp");
    case Qt::Key_PageDown: return QStringLiteral("PageDown");
    case Qt::Key_Pause: return QStringLiteral("Pause");
    case Qt::Key_Hangul: return QStringLiteral("한/영");
    case Qt::Key_Hangul_Hanja: return QStringLiteral("한자");
    default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
    }
    if (key > 0x20 && key <= 0x7E) {
        return QString(QChar(key));
    }
    const QString seq = QKeySequence(key).toString(QKeySequence::NativeText);
    if (!seq.isEmpty()) {
        return seq;
    }
    return QStringLiteral("키 ") + QString::number(key);
}

}  // namespace

const std::array<ShortcutInfo, kShortcutCount>& AllShortcuts() {
    return kShortcuts;
}

const ShortcutInfo& GetShortcutInfo(ShortcutAction action) {
    return kShortcuts[static_cast<size_t>(action)];
}

ShortcutTable DefaultShortcuts() {
    ShortcutTable table{};
    for (size_t i = 0; i < kShortcutCount; ++i) {
        table[i] = kShortcuts[i].defaultKey;
    }
    return table;
}

KeyChord ChordFromKeyEvent(const QKeyEvent* event) {
    if (!event) {
        return kNoKey;
    }
    int key = event->key();
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
    }
    key = UnshiftKey(key);
    KeyChord chord = key & kKeyMask;
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & Qt::ControlModifier) {
        chord |= kCtrlFlag;
    }
    if (mods & Qt::ShiftModifier) {
        chord |= kShiftFlag;
    }
    if (mods & Qt::AltModifier) {
        chord |= kAltFlag;
    }
    return chord;
}

KeyChord NormalizeLoadedChord(int rawChord) {
    if (rawChord == kNoKey) {
        return kNoKey;
    }
    // Legacy Win32 chords fit in 0x01..0x7FF (low byte VK, 0x100/0x200/0x400 modifiers).
    if (rawChord > 0 && rawChord <= 0x7FF && (rawChord & static_cast<int>(Qt::KeyboardModifierMask)) == 0) {
        const int vk = rawChord & 0xFF;
        const int qtKey = LegacyVkToQtKey(vk);
        if (qtKey == 0) {
            return kNoKey;
        }
        KeyChord converted = qtKey;
        if (rawChord & 0x100) {
            converted |= kCtrlFlag;
        }
        if (rawChord & 0x200) {
            converted |= kShiftFlag;
        }
        if (rawChord & 0x400) {
            converted |= kAltFlag;
        }
        return converted;
    }
    return rawChord;
}

std::optional<ShortcutAction> FindShortcut(const ShortcutTable& shortcuts, KeyChord chord) {
    if (chord == kNoKey) {
        return std::nullopt;
    }
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        if (shortcuts[i] == chord) {
            return static_cast<ShortcutAction>(i);
        }
    }
    return std::nullopt;
}

QString KeyChordName(KeyChord chord) {
    if (chord == kNoKey) {
        return {};
    }
    QString name;
    if (chord & kCtrlFlag) {
        name += QStringLiteral("Ctrl+");
    }
    if (chord & kShiftFlag) {
        name += QStringLiteral("Shift+");
    }
    if (chord & kAltFlag) {
        name += QStringLiteral("Alt+");
    }
    return name + KeyName(chord & kKeyMask);
}

bool IsModifierKey(int qtKey) {
    switch (qtKey) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        return false;
    }
}

bool CanBeShortcut(KeyChord chord) {
    const int key = chord & kKeyMask;
    const int mods = chord & ~kKeyMask;
    if (key == 0 || (mods & ~kModifierMask) != 0 || IsModifierKey(key) || key == Qt::Key_unknown) {
        return false;
    }
    switch (key) {
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Menu:          // Opens the right-click menu.
    case Qt::Key_Print:         // PrintScreen.
    case Qt::Key_SysReq:
    case Qt::Key_Hangul:        // Input-method keys.
    case Qt::Key_Hangul_Hanja:
    case Qt::Key_Kanji:
    case Qt::Key_Kana_Shift:
        return false;
    default:
        break;
    }
    if (chord & kAltFlag) {
        if (key == Qt::Key_F4 || key == Qt::Key_Space || key == Qt::Key_Tab || key == Qt::Key_Escape) {
            return false;
        }
    }
    return !((chord & kCtrlFlag) && key == Qt::Key_Escape);  // Opens the Start menu on Windows.
}
