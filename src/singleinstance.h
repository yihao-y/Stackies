#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QThread>
#include <functional>
#include <utility>

namespace sticky {
// One active instance per user and database directory. A lock covers startup
// races; a local socket tells the existing application to reveal its windows.
class SingleInstance {
    QString name_;
    QLockFile lock_;
    QLocalServer server_;
    std::function<void()> activate_;
public:
    enum class Result { Primary, Forwarded, Unavailable };

    explicit SingleInstance(const QString &dataDirectory)
        : name_([](const QString &directory){
              const QFileInfo info(directory);
              QString path=info.canonicalFilePath();
              if(path.isEmpty())path=info.absoluteFilePath();
#ifdef Q_OS_WIN
              path=path.toLower();
#endif
              const QByteArray seed=(QDir::homePath()+"\n"+QDir::cleanPath(path)).toUtf8();
              const QByteArray hash=QCryptographicHash::hash(
                  seed,QCryptographicHash::Sha256).toHex().left(24);
              return QStringLiteral("stackies-")+QString::fromLatin1(hash);
          }(dataDirectory)),
          lock_(QDir(QDir::tempPath()).filePath(name_+".lock")) {
        server_.setSocketOptions(QLocalServer::UserAccessOption);
    }

    Result start(std::function<void()> activate){
        if(lock_.tryLock(0)){
            // Only a lock owner may remove an abandoned Unix socket.
            QLocalServer::removeServer(name_);
            if(!server_.listen(name_)){
                lock_.unlock();
                return Result::Unavailable;
            }
            activate_=std::move(activate);
            QObject::connect(&server_,&QLocalServer::newConnection,
                             &server_,[this]{
                while(auto *peer=server_.nextPendingConnection()){
                    peer->disconnectFromServer();
                    peer->deleteLater();
                    if(activate_)activate_();
                }
            });
            return Result::Primary;
        }
        // Primary may still be initializing; do not create a second writer.
        for(int attempt=0;attempt<12;++attempt){
            QLocalSocket peer;
            peer.connectToServer(name_);
            if(peer.waitForConnected(200)){
                peer.disconnectFromServer();
                return Result::Forwarded;
            }
            QThread::msleep(100);
        }
        return Result::Unavailable;
    }
};
} // namespace sticky
