#include <QApplication>
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

#include <exception>
#include <functional>

#include "MainWindow.h"

namespace {

constexpr const char* kSingleInstanceServerName = "MoviePlayer.SingleInstance";
constexpr int kConnectTimeoutMs = 1500;
constexpr int kWriteTimeoutMs = 3000;

QString NormalizeInputPath(const QString& rawPath) {
    if (rawPath.isEmpty() || rawPath.contains(QStringLiteral("://"))) {
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

// Hands the path to a copy of the app that's already running. Returns false if
// there isn't one to hand it to, in which case this copy should carry on.
bool ForwardToRunningInstance(const QString& path) {
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSingleInstanceServerName), QIODevice::WriteOnly);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/res/app.ico")));

    const QString path = PathFromArguments(QCoreApplication::arguments());
    if (ForwardToRunningInstance(path)) {
        return 0;
    }

    QLocalServer::removeServer(QString::fromLatin1(kSingleInstanceServerName));
    QLocalServer instanceServer;
    instanceServer.listen(QString::fromLatin1(kSingleInstanceServerName));

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
