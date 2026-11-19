#include "FileDialogs.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

#include <algorithm>

namespace {

QString ExtensionWithDot(const QString& path) {
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? path.mid(dot).toLower() : QString();
}

}  // namespace

QStringList PickVideoFiles(QWidget* owner) {
    QStringList patterns;
    for (const char* ext : kVideoExtensions) {
        patterns.push_back(QStringLiteral("*") + QLatin1String(ext));
    }
    const QString filter = QStringLiteral("동영상 파일 (%1);;모든 파일 (*)").arg(patterns.join(QLatin1Char(' ')));
    return QFileDialog::getOpenFileNames(owner, QStringLiteral("재생할 동영상 선택 (여러 개 선택하면 재생 목록이 됩니다)"), QString(), filter);
}

QString PickSubtitleFile(QWidget* owner) {
    QStringList patterns;
    for (const char* ext : kSubtitleExtensions) {
        patterns.push_back(QStringLiteral("*") + QLatin1String(ext));
    }
    const QString filter = QStringLiteral("자막 파일 (%1);;모든 파일 (*)").arg(patterns.join(QLatin1Char(' ')));
    return QFileDialog::getOpenFileName(owner, QStringLiteral("자막 파일 선택"), QString(), filter);
}

bool IsSubtitleFile(const QString& path) {
    const QByteArray ext = ExtensionWithDot(path).toLatin1();
    if (ext.isEmpty()) {
        return false;
    }
    return std::any_of(kSubtitleExtensions.begin(), kSubtitleExtensions.end(),
                       [&](const char* known) { return ext == known; });
}

bool IsVideoFile(const QString& path) {
    const QByteArray ext = ExtensionWithDot(path).toLatin1();
    if (ext.isEmpty()) {
        return false;
    }
    return std::any_of(kVideoExtensions.begin(), kVideoExtensions.end(),
                       [&](const char* known) { return ext == known; });
}

QString FindVideoForSubtitle(const QString& subtitle) {
    const QFileInfo subtitleInfo(subtitle);
    const QString subtitleName = subtitleInfo.completeBaseName();
    const QDir dir = subtitleInfo.dir();
    if (subtitleName.isEmpty() || !dir.exists()) {
        return {};
    }

    QString best;
    qsizetype bestLength = 0;
    size_t bestRank = kVideoExtensions.size();

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QByteArray ext = ExtensionWithDot(entry.fileName()).toLatin1();
        const auto type = std::find_if(kVideoExtensions.begin(), kVideoExtensions.end(),
                                       [&](const char* known) { return ext == known; });
        if (type == kVideoExtensions.end()) {
            continue;
        }
        const QString videoName = entry.completeBaseName();
        const bool exact = videoName.compare(subtitleName, Qt::CaseInsensitive) == 0;
        const bool tagged = subtitleName.size() > videoName.size() &&
                            subtitleName.at(videoName.size()) == QLatin1Char('.') &&
                            subtitleName.startsWith(videoName, Qt::CaseInsensitive);
        if (!exact && !tagged) {
            continue;
        }
        // The longest name is the closest match. Between formats, the list above decides.
        const size_t rank = static_cast<size_t>(type - kVideoExtensions.begin());
        if (videoName.size() > bestLength || (videoName.size() == bestLength && rank < bestRank)) {
            best = entry.absoluteFilePath();
            bestLength = videoName.size();
            bestRank = rank;
        }
    }
    return best;
}
