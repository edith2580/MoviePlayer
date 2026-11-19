#include "FileAssociation.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#include "FileDialogs.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kProgId[] = L"MoviePlayer.Video";
constexpr wchar_t kProgIdKey[] = L"Software\\Classes\\MoviePlayer.Video";
constexpr wchar_t kSubtitleProgId[] = L"MoviePlayer.Subtitle";
constexpr wchar_t kSubtitleProgIdKey[] = L"Software\\Classes\\MoviePlayer.Subtitle";
constexpr wchar_t kApplicationKey[] = L"Software\\Classes\\Applications\\MoviePlayer.exe";
constexpr wchar_t kAppKey[] = L"Software\\MoviePlayer";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\MoviePlayer\\Capabilities";
constexpr wchar_t kRegisteredApplicationsKey[] = L"Software\\RegisteredApplications";
constexpr wchar_t kRegisteredName[] = L"MoviePlayer";
constexpr wchar_t kAppName[] = L"Movie Player";

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

std::wstring OpenCommand() {
    return L"\"" + ExecutablePath() + L"\" \"%1\"";
}

bool SetText(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), bytes) == ERROR_SUCCESS;
}

std::wstring GetText(const std::wstring& key, const wchar_t* name) {
    wchar_t buffer[33000];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

bool KeyIsEmpty(const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    DWORD subkeys = 1;
    DWORD values = 1;
    RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    return subkeys == 0 && values == 0;
}

void NotifyShell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

std::wstring ToWideExt(const char* ext) {
    return QString::fromLatin1(ext).toStdWString();
}

}  // namespace

bool IsFileAssociationRegistered() {
    return GetText(std::wstring(kProgIdKey) + L"\\shell\\open\\command", nullptr) == OpenCommand() &&
           GetText(std::wstring(kSubtitleProgIdKey) + L"\\shell\\open\\command", nullptr) == OpenCommand();
}

bool RegisterFileAssociation() {
    const std::wstring executable = ExecutablePath();
    const std::wstring command = OpenCommand();
    const std::wstring icon = L"\"" + executable + L"\",0";
    const std::wstring progId(kProgIdKey);
    const std::wstring application(kApplicationKey);
    const std::wstring capabilities(kCapabilitiesKey);

    bool ok = true;
    for (const auto& [key, name] : {std::pair{progId, L"Movie Player 동영상"}, std::pair{std::wstring(kSubtitleProgIdKey), L"Movie Player 자막"}}) {
        ok = SetText(key, nullptr, name) && ok;
        ok = SetText(key + L"\\DefaultIcon", nullptr, icon) && ok;
        ok = SetText(key + L"\\shell\\open\\command", nullptr, command) && ok;
    }

    ok = SetText(application, L"FriendlyAppName", kAppName) && ok;
    ok = SetText(application + L"\\DefaultIcon", nullptr, icon) && ok;
    ok = SetText(application + L"\\shell\\open\\command", nullptr, command) && ok;

    ok = SetText(capabilities, L"ApplicationName", kAppName) && ok;
    ok = SetText(capabilities, L"ApplicationDescription", L"mpv 기반 동영상 플레이어") && ok;
    ok = SetText(capabilities, L"ApplicationIcon", icon) && ok;

    const auto addTypes = [&](const auto& extensions, const wchar_t* id) {
        for (const char* ext : extensions) {
            const std::wstring extension = ToWideExt(ext);
            const std::wstring classesKey = std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithProgids";
            ok = RegSetKeyValueW(HKEY_CURRENT_USER, classesKey.c_str(), id, REG_NONE, nullptr, 0) == ERROR_SUCCESS && ok;
            ok = SetText(capabilities + L"\\FileAssociations", extension.c_str(), id) && ok;
            ok = SetText(application + L"\\SupportedTypes", extension.c_str(), L"") && ok;
        }
    };
    addTypes(kVideoExtensions, kProgId);
    addTypes(kSubtitleExtensions, kSubtitleProgId);

    ok = SetText(kRegisteredApplicationsKey, kRegisteredName, capabilities) && ok;
    NotifyShell();
    return ok;
}

void UnregisterFileAssociation() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kProgIdKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kSubtitleProgIdKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kApplicationKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kAppKey);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegisteredApplicationsKey, kRegisteredName);

    const auto removeTypes = [](const auto& extensions, const wchar_t* id) {
        for (const char* ext : extensions) {
            const std::wstring extension = ToWideExt(ext);
            const std::wstring extensionKey = std::wstring(L"Software\\Classes\\") + extension;
            const std::wstring openWith = extensionKey + L"\\OpenWithProgids";
            RegDeleteKeyValueW(HKEY_CURRENT_USER, openWith.c_str(), id);
            if (KeyIsEmpty(openWith)) {
                RegDeleteKeyW(HKEY_CURRENT_USER, openWith.c_str());
                if (KeyIsEmpty(extensionKey)) {
                    RegDeleteKeyW(HKEY_CURRENT_USER, extensionKey.c_str());
                }
            }
        }
    };
    removeTypes(kVideoExtensions, kProgId);
    removeTypes(kSubtitleExtensions, kSubtitleProgId);
    NotifyShell();
}

void OpenDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=MoviePlayer", nullptr, nullptr, SW_SHOWNORMAL);
}

#elif defined(Q_OS_LINUX)

namespace {

QString DesktopFilePath() {
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share"));
    return dataHome + QStringLiteral("/applications/movieplayer.desktop");
}

QString ExpectedExecLine() {
    return QStringLiteral("Exec=\"%1\" %U").arg(QCoreApplication::applicationFilePath());
}

}  // namespace

bool IsFileAssociationRegistered() {
    QFile file(DesktopFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    return content.contains(ExpectedExecLine());
}

bool RegisterFileAssociation() {
    const QString path = DesktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Movie Player\n"
        "Comment=mpv 기반 동영상 플레이어\n"
        "%1\n"
        "Terminal=false\n"
        "Categories=AudioVideo;Video;Player;\n"
        "MimeType=video/mp4;video/x-matroska;video/x-msvideo;video/quicktime;video/x-ms-wmv;video/webm;video/mp2t;video/x-flv;video/mpeg;video/3gpp;video/ogg;application/x-subrip;text/x-ssa;text/vtt;\n")
        .arg(ExpectedExecLine());
    file.write(content.toUtf8());
    file.close();
    QProcess::execute(QStringLiteral("update-desktop-database"), {QFileInfo(path).absolutePath()});
    return true;
}

void UnregisterFileAssociation() {
    const QString path = DesktopFilePath();
    QFile::remove(path);
    QProcess::execute(QStringLiteral("update-desktop-database"), {QFileInfo(path).absolutePath()});
}

void OpenDefaultAppsSettings() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(DesktopFilePath()).absolutePath()));
}

#else  // macOS and others

bool IsFileAssociationRegistered() {
    return true;
}

bool RegisterFileAssociation() {
    return true;
}

void UnregisterFileAssociation() {
}

void OpenDefaultAppsSettings() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:")));
}

#endif
