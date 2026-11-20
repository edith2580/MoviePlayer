#include "FileAssociation.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
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

#include <QStringList>

namespace {

constexpr const char* kDesktopFileName = "movieplayer.desktop";

constexpr std::array<const char*, 27> kLinuxMimeTypes{
    "video/mp4",
    "video/x-matroska",
    "application/x-matroska",
    "video/x-msvideo",
    "video/avi",
    "video/quicktime",
    "video/x-ms-wmv",
    "video/webm",
    "video/mp2t",
    "video/x-m4v",
    "video/x-flv",
    "video/mpeg",
    "video/mp2p",
    "video/3gpp",
    "video/3gpp2",
    "video/ogg",
    "video/x-ogm+ogg",
    "video/x-theora+ogg",
    "video/x-ms-asf",
    "application/vnd.ms-asf",
    "application/x-subrip",
    "text/x-ssa",
    "text/vtt",
    "application/x-sami",
    "application/smil",
    "application/smil+xml",
    "text/x-microdvd",
};

QString DataHomeDir() {
    QString dir = qEnvironmentVariable("XDG_DATA_HOME");
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.local/share");
    }
    return dir;
}

QString ConfigHomeDir() {
    QString dir = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.config");
    }
    return dir;
}

QString DesktopFilePath() {
    return DataHomeDir() + QStringLiteral("/applications/") + QLatin1String(kDesktopFileName);
}

QString IconFilePath() {
    return DataHomeDir() + QStringLiteral("/icons/hicolor/256x256/apps/movieplayer.png");
}

QString MimeAppsListPath() {
    return ConfigHomeDir() + QStringLiteral("/mimeapps.list");
}

QString PersistentExecutablePath() {
    // Inside an AppImage, applicationFilePath() points to an ephemeral /tmp/.mount_* path.
    // $APPIMAGE holds the actual persistent .AppImage path.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
        return QFileInfo(appImage).absoluteFilePath();
    }
    return QCoreApplication::applicationFilePath();
}

QString EscapeDesktopExecArg(const QString& arg) {
    QString escaped;
    escaped.reserve(arg.size() + 8);
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('$') || ch == QLatin1Char('`')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(ch);
    }
    return escaped;
}

QString ExpectedExecLine() {
    return QStringLiteral("Exec=\"%1\" %F").arg(EscapeDesktopExecArg(PersistentExecutablePath()));
}

QString MimeTypeListString() {
    QString result;
    for (const char* mime : kLinuxMimeTypes) {
        result += QLatin1String(mime);
        result += QLatin1Char(';');
    }
    return result;
}

// Updates [Added Associations] (and clears [Removed Associations] on register)
// in ~/.config/mimeapps.list without altering [Default Applications].
void UpdateMimeAppsList(bool add) {
    const QString path = MimeAppsListPath();
    QStringList lines;
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(file.readAll());
            lines = content.split(QLatin1Char('\n'));
            if (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
        }
    }

    const QString desktopId = QLatin1String(kDesktopFileName);
    QStringList outLines;
    outLines.reserve(lines.size() + static_cast<qsizetype>(kLinuxMimeTypes.size()) + 4);

    QString currentSection;
    bool hasAddedSection = false;
    QStringList seenInAdded;

    auto appendMissingAdded = [&] {
        if (!add) {
            return;
        }
        for (const char* rawMime : kLinuxMimeTypes) {
            const QString mime = QLatin1String(rawMime);
            if (!seenInAdded.contains(mime)) {
                outLines.append(mime + QLatin1Char('=') + desktopId + QLatin1Char(';'));
            }
        }
    };

    for (const QString& rawLine : lines) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            if (currentSection == QLatin1String("[Added Associations]")) {
                appendMissingAdded();
            }
            currentSection = trimmed;
            if (currentSection == QLatin1String("[Added Associations]")) {
                hasAddedSection = true;
            }
            outLines.append(rawLine);
            continue;
        }

        const bool inAdded = (currentSection == QLatin1String("[Added Associations]"));
        const bool inRemoved = (currentSection == QLatin1String("[Removed Associations]"));
        const qsizetype eq = rawLine.indexOf(QLatin1Char('='));
        if ((inAdded || inRemoved) && eq > 0) {
            const QString mime = rawLine.left(eq).trimmed();
            bool isOurs = false;
            for (const char* m : kLinuxMimeTypes) {
                if (mime == QLatin1String(m)) {
                    isOurs = true;
                    break;
                }
            }
            if (isOurs) {
                QStringList entries = rawLine.mid(eq + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
                for (QString& e : entries) {
                    e = e.trimmed();
                }
                entries.removeAll(QString());

                if (inAdded) {
                    seenInAdded.append(mime);
                    if (add) {
                        if (!entries.contains(desktopId)) {
                            entries.append(desktopId);
                        }
                    } else {
                        entries.removeAll(desktopId);
                    }
                } else if (inRemoved && add) {
                    entries.removeAll(desktopId);
                }

                if (!entries.isEmpty()) {
                    outLines.append(mime + QLatin1Char('=') + entries.join(QLatin1Char(';')) + QLatin1Char(';'));
                }
                continue;
            }
        }

        outLines.append(rawLine);
    }

    if (currentSection == QLatin1String("[Added Associations]")) {
        appendMissingAdded();
    } else if (add && !hasAddedSection) {
        if (!outLines.isEmpty() && !outLines.last().isEmpty()) {
            outLines.append(QString());
        }
        outLines.append(QStringLiteral("[Added Associations]"));
        appendMissingAdded();
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile outFile(path);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        outFile.write((outLines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    }
}

void InstallUserIcon() {
    const QString iconPath = IconFilePath();
    QDir().mkpath(QFileInfo(iconPath).absolutePath());
    const QPixmap pixmap = QIcon(QStringLiteral(":/res/app.ico")).pixmap(256, 256);
    if (!pixmap.isNull()) {
        pixmap.save(iconPath, "PNG");
    }
}

void RefreshDesktopDatabase(const QString& applicationsDir) {
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!tool.isEmpty()) {
        QProcess::execute(tool, {applicationsDir});
    }
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
    InstallUserIcon();

    const QString path = DesktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QString iconPath = IconFilePath();
    const QString iconEntry = QFileInfo::exists(iconPath) ? iconPath : QStringLiteral("movieplayer");
    const QString content = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Movie Player\n"
        "GenericName=Media Player\n"
        "Comment=mpv 기반 동영상 플레이어\n"
        "%1\n"
        "Icon=%2\n"
        "Terminal=false\n"
        "NoDisplay=false\n"
        "Categories=AudioVideo;Video;Player;Qt;\n"
        "StartupWMClass=MoviePlayer\n"
        "MimeType=%3\n")
        .arg(ExpectedExecLine(), iconEntry, MimeTypeListString());
    file.write(content.toUtf8());
    file.close();

    UpdateMimeAppsList(true);
    RefreshDesktopDatabase(QFileInfo(path).absolutePath());
    return true;
}

void UnregisterFileAssociation() {
    const QString path = DesktopFilePath();
    QFile::remove(path);
    QFile::remove(IconFilePath());
    UpdateMimeAppsList(false);
    RefreshDesktopDatabase(QFileInfo(path).absolutePath());
}

void OpenDefaultAppsSettings() {
    struct SettingsLauncher {
        const char* program;
        QStringList args;
    };
    const SettingsLauncher launchers[] = {
        {"gnome-control-center", {QStringLiteral("default-apps")}},
        {"kcmshell6", {QStringLiteral("kcm_componentchooser")}},
        {"kcmshell5", {QStringLiteral("kcm_componentchooser")}},
        {"xfce4-mime-settings", {}},
    };
    for (const auto& launcher : launchers) {
        const QString exe = QStandardPaths::findExecutable(QLatin1String(launcher.program));
        if (!exe.isEmpty() && QProcess::startDetached(exe, launcher.args)) {
            return;
        }
    }
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
