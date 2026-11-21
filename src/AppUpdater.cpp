#include "AppUpdater.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>

#include "AppInfo.h"

namespace {

QString NormalizeVersion(QString tag) {
    tag = tag.trimmed();
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        tag.remove(0, 1);
    }
    return tag;
}

bool IsNewerVersion(const QString& remoteVersion, const QString& currentVersion) {
    const QVersionNumber remote = QVersionNumber::fromString(remoteVersion);
    const QVersionNumber current = QVersionNumber::fromString(currentVersion);
    if (!remote.isNull() && !current.isNull()) {
        return QVersionNumber::compare(remote, current) > 0;
    }
    return !remoteVersion.isEmpty() && remoteVersion != currentVersion;
}

#if defined(Q_OS_WIN)
QString NormalizeDirPath(const QString& path) {
    if (path.isEmpty()) {
        return {};
    }
    QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (cleaned.endsWith(QLatin1Char('/')) && cleaned.size() > 3) {
        cleaned.chop(1);
    }
    return cleaned;
}

bool MatchesWindowsUninstallRegistry(const QString& appDir) {
    const QString keySuffix = QString::fromLatin1(AppInfo::kWindowsInnoAppId) + QStringLiteral("_is1");
    const QStringList roots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + keySuffix,
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + keySuffix,
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + keySuffix,
    };

    const QString normalizedAppDir = NormalizeDirPath(appDir);
    for (const QString& regPath : roots) {
        QSettings reg(regPath, QSettings::NativeFormat);
        const QString installLoc = NormalizeDirPath(reg.value(QStringLiteral("InstallLocation")).toString());
        const QString innoPath = NormalizeDirPath(reg.value(QStringLiteral("Inno Setup: App Path")).toString());
        if ((!installLoc.isEmpty() && installLoc.compare(normalizedAppDir, Qt::CaseInsensitive) == 0) ||
            (!innoPath.isEmpty() && innoPath.compare(normalizedAppDir, Qt::CaseInsensitive) == 0)) {
            return true;
        }
    }
    return false;
}
#endif

#if defined(Q_OS_MACOS)
QString FindCurrentMacAppBundlePath() {
    QDir dir(QCoreApplication::applicationDirPath());
    // Typical bundle layout: MoviePlayer.app/Contents/MacOS/MoviePlayer
    if (dir.dirName() == QStringLiteral("MacOS") && dir.cdUp() &&
        dir.dirName() == QStringLiteral("Contents") && dir.cdUp() &&
        dir.dirName().endsWith(QStringLiteral(".app"), Qt::CaseInsensitive)) {
        return dir.absolutePath();
    }
    return {};
}
#endif

QString ExpectedArchSuffix() {
#if defined(Q_OS_MACOS)
#if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("arm64");
#else
    return QStringLiteral("x86_64");
#endif
#elif defined(Q_OS_LINUX)
#if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("aarch64");
#else
    return QStringLiteral("x86_64");
#endif
#else
    return QStringLiteral("win64");
#endif
}

bool MatchesAssetForInstallKind(const QString& assetName, InstallKind kind) {
    const QString lower = assetName.toLower();
    switch (kind) {
    case InstallKind::WindowsInstaller:
        return lower.endsWith(QStringLiteral("-win64-setup.exe")) ||
               (lower.contains(QStringLiteral("win64")) && lower.endsWith(QStringLiteral(".exe")));
    case InstallKind::WindowsPortable:
        return lower.endsWith(QStringLiteral("-win64-portable.zip")) ||
               (lower.contains(QStringLiteral("win64")) && lower.endsWith(QStringLiteral(".zip")));
    case InstallKind::MacBundle: {
        const QString arch = ExpectedArchSuffix();
        return lower.contains(QStringLiteral("macos")) && lower.contains(arch) &&
               lower.endsWith(QStringLiteral(".dmg"));
    }
    case InstallKind::LinuxAppImage: {
        const QString arch = ExpectedArchSuffix();
        return lower.contains(QStringLiteral("linux")) && lower.contains(arch) &&
               lower.endsWith(QStringLiteral(".appimage"));
    }
    case InstallKind::LinuxPackage: {
        const QString arch = ExpectedArchSuffix();
        return lower.contains(QStringLiteral("linux")) && lower.contains(arch) &&
               lower.endsWith(QStringLiteral(".deb"));
    }
    case InstallKind::Unknown:
        return false;
    }
    return false;
}

QNetworkRequest BuildGitHubRequest(const QUrl& url, bool jsonApi) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("MoviePlayer/%1").arg(QCoreApplication::applicationVersion()));
    if (jsonApi) {
        req.setRawHeader("Accept", "application/vnd.github+json");
    }
    return req;
}

}  // namespace

AppUpdater::AppUpdater(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      installKind_(DetectInstallKind()) {}

AppUpdater::~AppUpdater() {
    Cancel();
}

InstallKind AppUpdater::DetectInstallKind() {
#if defined(Q_OS_WIN)
    if (MatchesWindowsUninstallRegistry(QCoreApplication::applicationDirPath())) {
        return InstallKind::WindowsInstaller;
    }
    return InstallKind::WindowsPortable;
#elif defined(Q_OS_MACOS)
    return InstallKind::MacBundle;
#elif defined(Q_OS_LINUX)
    if (!qEnvironmentVariable("APPIMAGE").isEmpty()) {
        return InstallKind::LinuxAppImage;
    }
    return InstallKind::LinuxPackage;
#else
    return InstallKind::Unknown;
#endif
}

QString AppUpdater::InstallKindLabel(InstallKind kind) {
    switch (kind) {
    case InstallKind::WindowsInstaller:
        return QStringLiteral("Windows 설치 버전");
    case InstallKind::WindowsPortable:
        return QStringLiteral("Windows 포터블 버전");
    case InstallKind::MacBundle:
        return QStringLiteral("macOS 앱 번들");
    case InstallKind::LinuxAppImage:
        return QStringLiteral("Linux AppImage");
    case InstallKind::LinuxPackage:
        return QStringLiteral("Linux 패키지 (.deb)");
    case InstallKind::Unknown:
        return QStringLiteral("알 수 없음");
    }
    return {};
}

bool AppUpdater::SupportsAutoUpdate(InstallKind kind) {
    switch (kind) {
    case InstallKind::WindowsInstaller:
    case InstallKind::WindowsPortable:
    case InstallKind::MacBundle:
    case InstallKind::LinuxAppImage:
        return true;
    case InstallKind::LinuxPackage:
    case InstallKind::Unknown:
        return false;
    }
    return false;
}

bool AppUpdater::IsBusy() const {
    return activeReply_ != nullptr;
}

void AppUpdater::Cancel() {
    if (activeReply_) {
        QNetworkReply* reply = activeReply_;
        activeReply_ = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (downloadFile_) {
        downloadFile_->close();
        downloadFile_->remove();
        downloadFile_->deleteLater();
        downloadFile_ = nullptr;
    }
    downloadPath_.clear();
}

void AppUpdater::CheckForUpdates() {
    if (IsBusy()) {
        return;
    }
    emit checkStarted();
    QNetworkRequest request = BuildGitHubRequest(QUrl(QString::fromLatin1(AppInfo::kGithubLatestReleaseApiUrl)), true);
    activeReply_ = network_->get(request);
    connect(activeReply_, &QNetworkReply::finished, this, [this, reply = activeReply_] {
        HandleCheckReply(reply);
    });
}

void AppUpdater::HandleCheckReply(QNetworkReply* reply) {
    if (activeReply_ == reply) {
        activeReply_ = nullptr;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 404) {
            emit checkFailed(QStringLiteral("게시된 GitHub 릴리스가 없습니다."));
        } else if (httpStatus == 403) {
            emit checkFailed(QStringLiteral("GitHub API 요청 한도를 초과했습니다. 잠시 후 다시 시도하세요."));
        } else {
            emit checkFailed(QStringLiteral("업데이트 확인 실패: %1").arg(reply->errorString()));
        }
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit checkFailed(QStringLiteral("릴리스 정보를 해석하지 못했습니다."));
        return;
    }

    const QJsonObject root = doc.object();
    ReleaseInfo info;
    info.tagName = root.value(QStringLiteral("tag_name")).toString();
    info.version = NormalizeVersion(info.tagName);
    info.htmlUrl = root.value(QStringLiteral("html_url")).toString();
    if (info.htmlUrl.isEmpty()) {
        info.htmlUrl = QString::fromLatin1(AppInfo::kGithubReleasesUrl);
    }

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& val : assets) {
        const QJsonObject asset = val.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (MatchesAssetForInstallKind(name, installKind_)) {
            info.assetName = name;
            info.assetDownloadUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            info.assetSize = asset.value(QStringLiteral("size")).toInteger();
            break;
        }
    }

    info.updateAvailable = IsNewerVersion(info.version, QCoreApplication::applicationVersion());
    info.canAutoUpdate = info.updateAvailable && !info.assetDownloadUrl.isEmpty() && SupportsAutoUpdate(installKind_);
    currentRelease_ = info;
    emit checkFinished(info);
}

void AppUpdater::DownloadAndApply(const ReleaseInfo& release) {
    if (IsBusy()) {
        return;
    }
    if (release.assetDownloadUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(release.htmlUrl.isEmpty()
                                           ? QString::fromLatin1(AppInfo::kGithubReleasesUrl)
                                           : release.htmlUrl));
        return;
    }

    currentRelease_ = release;

    QString targetDir;
    if (installKind_ == InstallKind::LinuxPackage) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    if (targetDir.isEmpty()) {
        targetDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                        .filePath(QStringLiteral("MoviePlayerUpdate"));
    }
    QDir().mkpath(targetDir);

    const QString safeName = QFileInfo(release.assetName).fileName();
    downloadPath_ = QDir(targetDir).filePath(safeName.isEmpty() ? QStringLiteral("update.bin") : safeName);

    downloadFile_ = new QFile(downloadPath_, this);
    if (!downloadFile_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = downloadFile_->errorString();
        downloadFile_->deleteLater();
        downloadFile_ = nullptr;
        emit downloadFailed(QStringLiteral("임시 파일을 만들 수 없습니다: %1").arg(err));
        return;
    }

    emit downloadStarted(safeName);

    QNetworkRequest request = BuildGitHubRequest(QUrl(release.assetDownloadUrl), false);
    activeReply_ = network_->get(request);
    connect(activeReply_, &QNetworkReply::readyRead, this, [this] {
        if (activeReply_ && downloadFile_) {
            downloadFile_->write(activeReply_->readAll());
        }
    });
    connect(activeReply_, &QNetworkReply::downloadProgress, this, &AppUpdater::downloadProgress);
    connect(activeReply_, &QNetworkReply::finished, this, [this, reply = activeReply_] {
        HandleDownloadReply(reply);
    });
}

void AppUpdater::HandleDownloadReply(QNetworkReply* reply) {
    if (activeReply_ == reply) {
        activeReply_ = nullptr;
    }
    reply->deleteLater();

    if (downloadFile_) {
        if (reply->bytesAvailable() > 0) {
            downloadFile_->write(reply->readAll());
        }
        downloadFile_->flush();
        downloadFile_->close();
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (downloadFile_) {
            downloadFile_->remove();
            downloadFile_->deleteLater();
            downloadFile_ = nullptr;
        }
        emit downloadFailed(QStringLiteral("다운로드 실패: %1").arg(reply->errorString()));
        return;
    }

    const QString savedPath = downloadPath_;
    if (downloadFile_) {
        downloadFile_->deleteLater();
        downloadFile_ = nullptr;
    }

    bool restartRequired = false;
    QString message;
    if (!ApplyDownloadedFile(savedPath, &restartRequired, &message)) {
        emit downloadFailed(message);
        return;
    }
    emit updateReadyToApply(restartRequired, message);
}

bool AppUpdater::ApplyDownloadedFile(const QString& filePath, bool* restartRequired, QString* message) {
    *restartRequired = false;

#if defined(Q_OS_WIN)
    if (installKind_ == InstallKind::WindowsInstaller) {
        // Launch Inno Setup installer silently; it will close/restart the app automatically.
        const QStringList args = {
            QStringLiteral("/SP-"),
            QStringLiteral("/SILENT"),
            QStringLiteral("/CLOSEAPPLICATIONS"),
            QStringLiteral("/RESTARTAPPLICATIONS"),
        };
        if (!QProcess::startDetached(filePath, args)) {
            *message = QStringLiteral("설치 프로그램을 실행하지 못했습니다.");
            return false;
        }
        *restartRequired = true;
        *message = QStringLiteral("설치 프로그램을 시작했습니다. 잠시 후 자동으로 업데이트됩니다.");
        return true;
    }

    if (installKind_ == InstallKind::WindowsPortable) {
        const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
        const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString zipPath = QDir::toNativeSeparators(filePath);
        const QString tempRoot = QDir::toNativeSeparators(QFileInfo(filePath).absolutePath());
        const QString scriptPath = QDir(tempRoot).filePath(QStringLiteral("apply_portable_update.ps1"));
        const qint64 pid = QCoreApplication::applicationPid();

        auto psEscape = [](QString s) {
            return s.replace(QLatin1Char('\''), QStringLiteral("''"));
        };

        const QString script = QStringLiteral(
            "$ErrorActionPreference = 'Stop'\n"
            "$targetPid = %1\n"
            "$zipPath = '%2'\n"
            "$appDir = '%3'\n"
            "$exePath = '%4'\n"
            "$stageDir = Join-Path '%5' ('stage_' + [Guid]::NewGuid().ToString('N'))\n"
            "for ($i = 0; $i -lt 60; $i++) {\n"
            "    if (-not (Get-Process -Id $targetPid -ErrorAction SilentlyContinue)) { break }\n"
            "    Start-Sleep -Milliseconds 250\n"
            "}\n"
            "Start-Sleep -Milliseconds 300\n"
            "New-Item -ItemType Directory -Force -Path $stageDir | Out-Null\n"
            "Expand-Archive -LiteralPath $zipPath -DestinationPath $stageDir -Force\n"
            "Copy-Item -Path (Join-Path $stageDir '*') -Destination $appDir -Recurse -Force\n"
            "Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue\n"
            "Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue\n"
            "Start-Process -FilePath $exePath\n")
                                   .arg(pid)
                                   .arg(psEscape(zipPath), psEscape(appDir), psEscape(exePath), psEscape(tempRoot));

        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            *message = QStringLiteral("업데이트 스크립트를 생성하지 못했습니다.");
            return false;
        }
        // Write UTF-8 with BOM so Windows PowerShell 5.1 parses Korean paths correctly.
        scriptFile.write("\xEF\xBB\xBF");
        scriptFile.write(script.toUtf8());
        scriptFile.close();

        const QStringList args = {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-WindowStyle"),
            QStringLiteral("Hidden"),
            QStringLiteral("-ExecutionPolicy"),
            QStringLiteral("Bypass"),
            QStringLiteral("-File"),
            QDir::toNativeSeparators(scriptPath),
        };
        if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
            *message = QStringLiteral("포터블 업데이트 스크립트를 실행하지 못했습니다.");
            return false;
        }
        *restartRequired = true;
        *message = QStringLiteral("업데이트를 적용하고 앱을 다시 시작합니다.");
        return true;
    }
#elif defined(Q_OS_MACOS)
    if (installKind_ == InstallKind::MacBundle) {
        const QString appBundlePath = FindCurrentMacAppBundlePath();
        const QFileInfo parentInfo(QFileInfo(appBundlePath).absolutePath());
        if (!appBundlePath.isEmpty() && parentInfo.isWritable()) {
            const QString tempRoot = QFileInfo(filePath).absolutePath();
            const QString scriptPath = QDir(tempRoot).filePath(QStringLiteral("apply_macos_update.sh"));
            const qint64 pid = QCoreApplication::applicationPid();

            auto shEscape = [](QString s) {
                return s.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            };

            const QString script = QStringLiteral(
                "#!/bin/sh\n"
                "set -e\n"
                "TARGET_PID=%1\n"
                "DMG='%2'\n"
                "APP_PATH='%3'\n"
                "MNT=\"$(mktemp -d /tmp/movieplayer_update.XXXXXX)\"\n"
                "i=0\n"
                "while kill -0 \"$TARGET_PID\" 2>/dev/null && [ $i -lt 60 ]; do\n"
                "  sleep 0.25\n"
                "  i=$((i + 1))\n"
                "done\n"
                "hdiutil attach -nobrowse -readonly -mountpoint \"$MNT\" \"$DMG\" >/dev/null\n"
                "if [ -d \"$MNT/MoviePlayer.app\" ]; then\n"
                "  rm -rf \"$APP_PATH\"\n"
                "  cp -R \"$MNT/MoviePlayer.app\" \"$APP_PATH\"\n"
                "fi\n"
                "hdiutil detach \"$MNT\" -quiet || true\n"
                "rmdir \"$MNT\" 2>/dev/null || true\n"
                "rm -f \"$DMG\"\n"
                "open \"$APP_PATH\"\n")
                                       .arg(pid)
                                       .arg(shEscape(filePath), shEscape(appBundlePath));

            QFile scriptFile(scriptPath);
            if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                scriptFile.write(script.toUtf8());
                scriptFile.close();
                scriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
                if (QProcess::startDetached(QStringLiteral("/bin/sh"), {scriptPath})) {
                    *restartRequired = true;
                    *message = QStringLiteral("업데이트를 적용하고 앱을 다시 시작합니다.");
                    return true;
                }
            }
        }
        // Fallback when bundle location is read-only or not inside an .app bundle: open DMG directly.
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        *restartRequired = false;
        *message = QStringLiteral("DMG 이미지를 열었습니다. MoviePlayer.app을 응용 프로그램 폴더로 복사하세요.");
        return true;
    }
#elif defined(Q_OS_LINUX)
    if (installKind_ == InstallKind::LinuxAppImage) {
        const QString currentAppImage = qEnvironmentVariable("APPIMAGE");
        if (!currentAppImage.isEmpty()) {
            QFile::setPermissions(filePath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                      QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                      QFileDevice::ReadOther | QFileDevice::ExeOther);

            const QString tempRoot = QFileInfo(filePath).absolutePath();
            const QString scriptPath = QDir(tempRoot).filePath(QStringLiteral("apply_appimage_update.sh"));
            const qint64 pid = QCoreApplication::applicationPid();

            auto shEscape = [](QString s) {
                return s.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            };

            const QString script = QStringLiteral(
                "#!/bin/sh\n"
                "set -e\n"
                "TARGET_PID=%1\n"
                "NEW_IMG='%2'\n"
                "CUR_IMG='%3'\n"
                "i=0\n"
                "while kill -0 \"$TARGET_PID\" 2>/dev/null && [ $i -lt 60 ]; do\n"
                "  sleep 0.25\n"
                "  i=$((i + 1))\n"
                "done\n"
                "mv -f \"$NEW_IMG\" \"$CUR_IMG\"\n"
                "chmod +x \"$CUR_IMG\"\n"
                "nohup \"$CUR_IMG\" >/dev/null 2>&1 &\n")
                                       .arg(pid)
                                       .arg(shEscape(filePath), shEscape(currentAppImage));

            QFile scriptFile(scriptPath);
            if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                *message = QStringLiteral("업데이트 스크립트를 생성하지 못했습니다.");
                return false;
            }
            scriptFile.write(script.toUtf8());
            scriptFile.close();
            scriptFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

            if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {scriptPath})) {
                *message = QStringLiteral("AppImage 업데이트 스크립트를 실행하지 못했습니다.");
                return false;
            }
            *restartRequired = true;
            *message = QStringLiteral("AppImage를 교체하고 앱을 다시 시작합니다.");
            return true;
        }
    }

    if (installKind_ == InstallKind::LinuxPackage) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
        *restartRequired = false;
        *message = QStringLiteral("패키지를 다운로드했습니다: %1\n터미널에서 sudo apt install \"%1\" 로 설치하세요.").arg(filePath);
        return true;
    }
#endif

    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
    *restartRequired = false;
    *message = QStringLiteral("다운로드가 완료되었습니다: %1").arg(filePath);
    return true;
}
