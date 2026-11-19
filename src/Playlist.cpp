#include "Playlist.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

#include "FileDialogs.h"

namespace {

bool IsHexDigit(QChar c) {
    const ushort u = c.unicode();
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

// The part of a name that stays the same across a series: numbers become "#", and a
// release group's checksum such as "[27AB50FF]" is left out, since it differs per file.
QString SeriesKey(const QString& name) {
    QString key;
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (name[i] == QLatin1Char('[') && i + 9 < name.size() && name[i + 9] == QLatin1Char(']')) {
            bool allHex = true;
            for (qsizetype j = 1; j <= 8; ++j) {
                if (!IsHexDigit(name[i + j])) {
                    allHex = false;
                    break;
                }
            }
            if (allHex) {
                i += 9;
                continue;
            }
        }
        if (name[i].isDigit()) {
            key += QLatin1Char('#');
            while (i + 1 < name.size() && name[i + 1].isDigit()) {
                ++i;
            }
            continue;
        }
        key += name[i].toLower();
    }
    return key;
}

}  // namespace

void SortByName(QStringList& paths) {
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(paths.begin(), paths.end(), [&](const QString& a, const QString& b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
}

QStringList FindSeries(const QString& video) {
    const QFileInfo videoInfo(video);
    const QString key = SeriesKey(videoInfo.completeBaseName());
    // A name with no number to tell episodes apart isn't part of a series.
    if (!key.contains(QLatin1Char('#'))) {
        return {video};
    }

    QStringList series;
    bool foundItself = false;
    const QDir dir = videoInfo.dir();
    if (dir.exists()) {
        const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            if (IsVideoFile(entry.fileName()) && SeriesKey(entry.completeBaseName()) == key) {
                series.push_back(entry.absoluteFilePath());
                foundItself = foundItself || entry.fileName().compare(videoInfo.fileName(), Qt::CaseInsensitive) == 0;
            }
        }
    }
    if (!foundItself) {
        series.push_back(video);  // The folder couldn't be read, or the file is gone.
    }
    SortByName(series);
    return series;
}
