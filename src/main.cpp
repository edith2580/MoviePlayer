#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QSurfaceFormat>
#include <QUrl>

#include <exception>
#include <functional>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "MainWindow.h"

namespace {

constexpr const char* kSingleInstanceBaseName = "MoviePlayer.SingleInstance";
constexpr int kConnectTimeoutMs = 1500;
constexpr int kWriteTimeoutMs = 3000;

QString NormalizeInputPath(const QString& rawPath) {
    if (rawPath.isEmpty()) {
        return rawPath;
    }
    if (rawPath.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QUrl url(rawPath);
        if (url.isLocalFile()) {
            return QDir::toNativeSeparators(QFileInfo(url.toLocalFile()).absoluteFilePath());
        }
    }
    if (rawPath.contains(QStringLiteral("://"))) {
        return rawPath;
    }
    return QDir::toNativeSeparators(QFileInfo(rawPath).absoluteFilePath());
}

QString PathFromArguments(const QStringList& args) {
    if (args.size() > 1) {
        return NormalizeInputPath(args[1]);
    }
    return {};
}

#if defined(Q_OS_WIN)
// The SID of the user a process runs as, or empty if it can't be read
// (as for another user's process, whose token we may not open).
std::vector<unsigned char> ProcessUserSid(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    const bool ok = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    CloseHandle(token);
    if (!ok) {
        return {};
    }
    const PSID sid = reinterpret_cast<const TOKEN_USER*>(buffer.data())->User.Sid;
    std::vector<unsigned char> copy(GetLengthSid(sid));
    if (!CopySid(static_cast<DWORD>(copy.size()), copy.data(), sid)) {
        return {};
    }
    return copy;
}
#endif

// A name of our own for each user. A fixed name is shared by everyone on the machine
// (Windows pipe names span all sessions, and on Linux it's a socket in the shared /tmp),
// so another user could take it first and receive the paths we open.
QString SingleInstanceServerName() {
    const QString base = QString::fromLatin1(kSingleInstanceBaseName);
#if defined(Q_OS_WIN)
    const std::vector<unsigned char> sid = ProcessUserSid(GetCurrentProcess());
    if (sid.empty()) {
        return base;
    }
    const QByteArray key = QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(sid.data()), static_cast<qsizetype>(sid.size())),
        QCryptographicHash::Sha256).toHex().left(16);
    return base + QLatin1Char('-') + QString::fromLatin1(key);
#else
    const QString name = base + QLatin1Char('-') + QString::number(geteuid());
#if defined(Q_OS_LINUX)
    // The session's runtime directory is private to the user (mode 0700).
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty() && QFileInfo(runtimeDir).isDir()) {
        return QDir(runtimeDir).filePath(name);
    }
#endif
    // Otherwise it lands in the temp directory: per user on macOS, shared /tmp on Linux,
    // where ServerIsSameUser() still keeps us from talking to someone else's server.
    return name;
#endif
}

// True if the server at the other end runs as the same user as this process. A name
// can be taken by anyone, so this is what decides whether to hand over a path.
bool ServerIsSameUser(const QLocalSocket& socket) {
#if defined(Q_OS_WIN)
    const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    ULONG serverPid = 0;
    if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE || !GetNamedPipeServerProcessId(pipe, &serverPid)) {
        return false;
    }
    HANDLE server = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid);
    if (!server) {
        return false;
    }
    const std::vector<unsigned char> serverSid = ProcessUserSid(server);
    CloseHandle(server);
    return !serverSid.empty() && serverSid == ProcessUserSid(GetCurrentProcess());
#else
    const int fd = static_cast<int>(socket.socketDescriptor());
    if (fd < 0) {
        return false;
    }
#if defined(Q_OS_LINUX)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        return false;
    }
    return credentials.uid == geteuid();
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (getpeereid(fd, &uid, &gid) != 0) {
        return false;
    }
    return uid == geteuid();
#endif
#endif
}

// Hands the path to a copy of the app that's already running. Returns false if
// there isn't one to hand it to, in which case this copy should carry on.
bool ForwardToRunningInstance(const QString& serverName, const QString& path) {
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        return false;
    }
    if (!ServerIsSameUser(socket)) {
        // Someone else holds the name: send nothing and run on our own.
        socket.abort();
        return false;
    }
    const QByteArray payload = path.toUtf8();
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(kWriteTimeoutMs);
    socket.disconnectFromServer();
    return true;
}

class MoviePlayerApplication : public QApplication {
public:
    using QApplication::QApplication;

    void SetFileOpenHandler(std::function<void(const QString&)> handler) {
        fileOpenHandler_ = std::move(handler);
        for (const QString& pending : pendingPaths_) {
            fileOpenHandler_(pending);
        }
        pendingPaths_.clear();
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            auto* openEvent = static_cast<QFileOpenEvent*>(event);
            const QString path = !openEvent->file().isEmpty()
                                     ? NormalizeInputPath(openEvent->file())
                                     : openEvent->url().toString();
            if (!path.isEmpty()) {
                if (fileOpenHandler_) {
                    fileOpenHandler_(path);
                } else {
                    pendingPaths_.push_back(path);
                }
            }
            return true;
        }
        return QApplication::event(event);
    }

private:
    std::function<void(const QString&)> fileOpenHandler_;
    QStringList pendingPaths_;
};

}  // namespace

int main(int argc, char* argv[]) {
    MoviePlayerApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MoviePlayer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MOVIEPLAYER_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/res/app.ico")));

    const QString path = PathFromArguments(QCoreApplication::arguments());
    const QString serverName = SingleInstanceServerName();
    if (ForwardToRunningInstance(serverName, path)) {
        return 0;
    }

    // Clears a socket file left by a copy that crashed (it does nothing on Windows).
    QLocalServer::removeServer(serverName);
    QLocalServer instanceServer;
    // Only this user may connect: a private socket on Unix, a pipe DACL on Windows.
    instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
    if (!instanceServer.listen(serverName)) {
        // Without the server, files opened later start their own window. Playback still works.
        qWarning("Single-instance server unavailable: %s", qPrintable(instanceServer.errorString()));
    }

    try {
        MainWindow window(LoadSettings());
        QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&] {
            while (QLocalSocket* client = instanceServer.nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                if (client->bytesAvailable() == 0) {
                    client->waitForReadyRead(1000);
                }
                const QString incomingPath = QString::fromUtf8(client->readAll());
                window.ActivateAndRaise();
                if (!incomingPath.isEmpty()) {
                    window.OpenFromExplorer(incomingPath);
                }
                client->disconnectFromServer();
            }
        });

        app.SetFileOpenHandler([&window](const QString& openedPath) {
            window.ActivateAndRaise();
            window.OpenFromExplorer(openedPath);
        });

        window.ShowInitial();
        if (!path.isEmpty()) {
            window.OpenFromExplorer(path);
        }
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("Movie Player"), QString::fromUtf8(error.what()));
        return 1;
    }
}
