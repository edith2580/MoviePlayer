#pragma once

#include <QString>
#include <QStringList>

// What happens when a file ends, besides moving on when that's turned on.
enum class RepeatMode { None, One, All };

// Sorts paths the way Explorer/Finder does by name, so "2" comes before "10".
void SortByName(QStringList& paths);

// The videos in the same folder whose names differ from this one's only in numbers
// and in a bracketed 8-digit checksum, like the other episodes of a series. Includes
// the video itself, sorted by name. Reads the folder, so it can be slow on a network drive.
QStringList FindSeries(const QString& video);
