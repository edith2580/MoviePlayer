#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {

constexpr const char* kPlayerSection = "Player";
constexpr const char* kWindowSection = "Window";
constexpr const char* kVideoSection = "Video";
constexpr const char* kSubtitleSection = "Subtitle";
constexpr const char* kShortcutSection = "Shortcuts";
constexpr const char* kPlaylistSection = "Playlist";
constexpr std::array<const char*, kVideoAdjustmentCount> kAdjustmentKeys{
    "Brightness", "Contrast", "Saturation", "Hue", "Gamma",
};
constexpr int kMaxAdjustment = 100;
constexpr double kMaxVolume = 130.0;  // mpv's default volume-max.
constexpr int kMinWindowSize = 100;

QString SettingsPath() {
#if defined(Q_OS_WIN)
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        return QDir(appData).filePath(QStringLiteral("MoviePlayer/settings.ini"));
    }
#endif
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        return {};
    }
    return QDir(configDir).filePath(QStringLiteral("settings.ini"));
}

std::optional<int> ReadInt(QSettings& ini, const char* section, const char* key) {
    const QString fullKey = QString::fromLatin1(section) + QLatin1Char('/') + QString::fromLatin1(key);
    if (!ini.contains(fullKey)) {
        return std::nullopt;
    }
    bool ok = false;
    const int value = ini.value(fullKey).toString().toInt(&ok);
    return ok ? std::optional<int>(value) : std::nullopt;
}

std::optional<double> ReadDouble(QSettings& ini, const char* section, const char* key) {
    const QString fullKey = QString::fromLatin1(section) + QLatin1Char('/') + QString::fromLatin1(key);
    if (!ini.contains(fullKey)) {
        return std::nullopt;
    }
    bool ok = false;
    const double value = ini.value(fullKey).toString().toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

QString ReadText(QSettings& ini, const char* section, const char* key) {
    const QString fullKey = QString::fromLatin1(section) + QLatin1Char('/') + QString::fromLatin1(key);
    return ini.value(fullKey).toString();
}

}  // namespace

Settings LoadSettings() {
    Settings settings;
    const QString path = SettingsPath();
    if (path.isEmpty()) {
        return settings;
    }

    QSettings ini(path, QSettings::IniFormat);

    if (const auto volume = ReadDouble(ini, kPlayerSection, "Volume");
        volume && *volume >= 0.0 && *volume <= kMaxVolume) {
        settings.volume = *volume;
    }

    if (ReadText(ini, kPlayerSection, "SubtitleEncoding") == QLatin1String("auto")) {
        settings.subtitleEncoding = SubtitleEncoding::Auto;
    }

    const auto left = ReadInt(ini, kWindowSection, "Left");
    const auto top = ReadInt(ini, kWindowSection, "Top");
    const auto right = ReadInt(ini, kWindowSection, "Right");
    const auto bottom = ReadInt(ini, kWindowSection, "Bottom");
    if (left && top && right && bottom &&
        *right - *left >= kMinWindowSize && *bottom - *top >= kMinWindowSize) {
        settings.windowPosition = QRect(QPoint(*left, *top), QSize(*right - *left, *bottom - *top));
    }
    settings.windowMaximized = ReadInt(ini, kWindowSection, "Maximized").value_or(0) == 1;

    for (size_t i = 0; i < kAdjustmentKeys.size(); ++i) {
        if (const auto value = ReadInt(ini, kVideoSection, kAdjustmentKeys[i]);
            value && *value >= -kMaxAdjustment && *value <= kMaxAdjustment) {
            settings.videoAdjustments[i] = *value;
        }
    }

    Preferences& preferences = settings.preferences;
    if (const auto seekSeconds = ReadInt(ini, kPlayerSection, "SeekSeconds");
        seekSeconds && *seekSeconds >= kMinSeekSeconds && *seekSeconds <= kMaxSeekSeconds) {
        preferences.seekSeconds = *seekSeconds;
    }
    preferences.subtitleStyle.font = ReadText(ini, kSubtitleSection, "Font");
    if (const auto fontSize = ReadInt(ini, kSubtitleSection, "FontSize");
        fontSize && *fontSize >= kMinSubtitleFontSize && *fontSize <= kMaxSubtitleFontSize) {
        preferences.subtitleStyle.fontSize = *fontSize;
    }

    if (const auto autoAdvance = ReadInt(ini, kPlaylistSection, "AutoAdvance");
        autoAdvance && (*autoAdvance == 0 || *autoAdvance == 1)) {
        preferences.autoAdvance = *autoAdvance == 1;
    }
    const QString repeat = ReadText(ini, kPlaylistSection, "Repeat");
    preferences.repeat = repeat == QLatin1String("one")   ? RepeatMode::One
                       : repeat == QLatin1String("all")   ? RepeatMode::All
                                                          : RepeatMode::None;
    preferences.fitWindowOnPlaylistChange = ReadInt(ini, kPlaylistSection, "FitWindow").value_or(0) == 1;

    if (const auto heightPercent = ReadInt(ini, kSubtitleSection, "HeightPercent");
        heightPercent && *heightPercent >= 0 && *heightPercent <= kMaxSubtitleHeightPercent) {
        preferences.subtitleStyle.heightPercent = *heightPercent;
    }
    if (const auto outlineSize = ReadDouble(ini, kSubtitleSection, "OutlineSize");
        outlineSize && std::find(kSubtitleOutlineSizes.begin(), kSubtitleOutlineSizes.end(), *outlineSize) != kSubtitleOutlineSizes.end()) {
        preferences.subtitleStyle.outlineSize = *outlineSize;
    }

    std::array<bool, kShortcutCount> stored{};
    for (size_t i = 0; i < kShortcutCount; ++i) {
        if (const auto rawChord = ReadInt(ini, kShortcutSection, AllShortcuts()[i].settingsName)) {
            const KeyChord chord = NormalizeLoadedChord(*rawChord);
            if (chord == kNoKey || CanBeShortcut(chord)) {
                preferences.shortcuts[i] = chord;
                stored[i] = true;
            }
        }
    }
    // A default added in a newer version gives way to a key the user already assigned.
    for (size_t i = 0; i < kShortcutCount; ++i) {
        for (size_t other = 0; !stored[i] && other < kShortcutCount; ++other) {
            if (stored[other] && preferences.shortcuts[other] == preferences.shortcuts[i]) {
                preferences.shortcuts[i] = kNoKey;
            }
        }
    }
    return settings;
}

void SaveSettings(const Settings& settings) {
    const QString path = SettingsPath();
    if (path.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings ini(path, QSettings::IniFormat);

    ini.beginGroup(QLatin1String(kPlayerSection));
    ini.setValue(QStringLiteral("Volume"), QString::number(std::llround(settings.volume)));
    ini.setValue(QStringLiteral("SubtitleEncoding"),
                 settings.subtitleEncoding == SubtitleEncoding::Auto ? QStringLiteral("auto") : QStringLiteral("korean"));
    ini.setValue(QStringLiteral("SeekSeconds"), settings.preferences.seekSeconds);
    ini.endGroup();

    ini.beginGroup(QLatin1String(kWindowSection));
    if (settings.windowPosition) {
        const QRect& rect = *settings.windowPosition;
        ini.setValue(QStringLiteral("Left"), rect.left());
        ini.setValue(QStringLiteral("Top"), rect.top());
        ini.setValue(QStringLiteral("Right"), rect.left() + rect.width());
        ini.setValue(QStringLiteral("Bottom"), rect.top() + rect.height());
    }
    ini.setValue(QStringLiteral("Maximized"), settings.windowMaximized ? 1 : 0);
    ini.endGroup();

    ini.beginGroup(QLatin1String(kVideoSection));
    for (size_t i = 0; i < kAdjustmentKeys.size(); ++i) {
        ini.setValue(QLatin1String(kAdjustmentKeys[i]), settings.videoAdjustments[i]);
    }
    ini.endGroup();

    const Preferences& preferences = settings.preferences;
    ini.beginGroup(QLatin1String(kSubtitleSection));
    ini.setValue(QStringLiteral("Font"), preferences.subtitleStyle.font);
    ini.setValue(QStringLiteral("FontSize"), preferences.subtitleStyle.fontSize);
    ini.setValue(QStringLiteral("OutlineSize"), QString::asprintf("%g", preferences.subtitleStyle.outlineSize));
    ini.setValue(QStringLiteral("HeightPercent"), preferences.subtitleStyle.heightPercent);
    ini.endGroup();

    ini.beginGroup(QLatin1String(kPlaylistSection));
    ini.setValue(QStringLiteral("AutoAdvance"), preferences.autoAdvance ? 1 : 0);
    ini.setValue(QStringLiteral("Repeat"),
                 preferences.repeat == RepeatMode::One   ? QStringLiteral("one")
                 : preferences.repeat == RepeatMode::All ? QStringLiteral("all")
                                                         : QStringLiteral("none"));
    ini.setValue(QStringLiteral("FitWindow"), preferences.fitWindowOnPlaylistChange ? 1 : 0);
    ini.endGroup();

    // Only changed keys are stored, so a future change to a default reaches everyone else.
    ini.beginGroup(QLatin1String(kShortcutSection));
    for (size_t i = 0; i < kShortcutCount; ++i) {
        const ShortcutInfo& info = AllShortcuts()[i];
        const QString keyName = QLatin1String(info.settingsName);
        if (preferences.shortcuts[i] == info.defaultKey) {
            ini.remove(keyName);
        } else {
            ini.setValue(keyName, preferences.shortcuts[i]);
        }
    }
    ini.endGroup();
    ini.sync();
}
