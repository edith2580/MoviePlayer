#pragma once

#include <QString>
#include <QStringList>
#include <array>

class QWidget;

// The file types the app offers to open, here and in the OS's "Open with" list.
inline constexpr std::array<const char*, 17> kVideoExtensions{
    ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".webm", ".ts",  ".m2ts", ".mts",
    ".m4v", ".flv", ".mpg", ".mpeg", ".3gp", ".ogv", ".vob", ".asf",
};

inline constexpr std::array<const char*, 7> kSubtitleExtensions{
    ".smi", ".sami", ".srt", ".ass", ".ssa", ".vtt", ".sub",
};

// Several can be chosen. Returns nothing if the user cancels.
QStringList PickVideoFiles(QWidget* owner);
// Returns an empty string if the user cancels.
QString PickSubtitleFile(QWidget* owner);

// Decides by file extension, which is what tells a dropped subtitle from a video.
bool IsSubtitleFile(const QString& path);
bool IsVideoFile(const QString& path);

// The video in the same folder that this subtitle belongs to, or an empty string.
// "movie.srt" and "movie.ko.srt" both belong to "movie.mkv". An exact match comes first.
QString FindVideoForSubtitle(const QString& subtitle);
