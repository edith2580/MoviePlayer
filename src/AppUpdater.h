#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

enum class InstallKind {
    WindowsInstaller,
    WindowsPortable,
    MacBundle,
    LinuxAppImage,
    LinuxPackage,
    Unknown,
};

struct ReleaseInfo {
    QString tagName;
    QString version;
    QString htmlUrl;
    QString assetName;
    QString assetDownloadUrl;
    qint64 assetSize = 0;
    bool updateAvailable = false;
    bool canAutoUpdate = false;
};

class AppUpdater : public QObject {
    Q_OBJECT

public:
    explicit AppUpdater(QObject* parent = nullptr);
    ~AppUpdater() override;

    static InstallKind DetectInstallKind();
    static QString InstallKindLabel(InstallKind kind);
    static bool SupportsAutoUpdate(InstallKind kind);

    bool IsBusy() const;
    void CheckForUpdates();
    void DownloadAndApply(const ReleaseInfo& release);
    void Cancel();

signals:
    void checkStarted();
    void checkFinished(const ReleaseInfo& info);
    void checkFailed(const QString& errorMessage);

    void downloadStarted(const QString& assetName);
    void downloadProgress(qint64 receivedBytes, qint64 totalBytes);
    void downloadFailed(const QString& errorMessage);
    // Emitted when the update is ready to be applied. If restartRequired is true,
    // the caller should close the application so the installer/helper can finish.
    void updateReadyToApply(bool restartRequired, const QString& message);

private:
    void HandleCheckReply(QNetworkReply* reply);
    void HandleDownloadReply(QNetworkReply* reply);
    bool ApplyDownloadedFile(const QString& filePath, bool* restartRequired, QString* message);

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
    QFile* downloadFile_ = nullptr;
    QString downloadPath_;
    InstallKind installKind_ = InstallKind::Unknown;
    ReleaseInfo currentRelease_;
};
