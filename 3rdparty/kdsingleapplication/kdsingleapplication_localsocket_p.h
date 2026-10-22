/*
  This file is part of KDSingleApplication.

  SPDX-FileCopyrightText: 2019-2023 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>

  SPDX-License-Identifier: MIT

  Contact KDAB at <info@kdab.com> for commercial licensing options.

  Modified for LogSquirl (#827): see README.md in this directory.
*/

#ifndef KDSINGLEAPPLICATION_LOCALSOCKET_P_H
#define KDSINGLEAPPLICATION_LOCALSOCKET_P_H

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QString>

QT_BEGIN_NAMESPACE
class QLockFile;
class QLocalServer;
class QLocalSocket;
class QTimer;
QT_END_NAMESPACE

#include <memory>
#include <vector>

struct QObjectDeleteLater
{
    void operator()(QObject *o)
    {
        o->deleteLater();
    }
};

class QObjectConnectionHolder
{
    Q_DISABLE_COPY(QObjectConnectionHolder)
    QMetaObject::Connection c;

public:
    QObjectConnectionHolder()
    {
    }

    explicit QObjectConnectionHolder(QMetaObject::Connection c)
        : c(std::move(c))
    {
    }

    ~QObjectConnectionHolder()
    {
        QObject::disconnect(c);
    }

    QObjectConnectionHolder(QObjectConnectionHolder &&other) noexcept
        : c(std::exchange(other.c, {}))
    {
    }

    QObjectConnectionHolder &operator=(QObjectConnectionHolder &&other) noexcept
    {
        QObjectConnectionHolder moved(std::move(other));
        swap(moved);
        return *this;
    }

    void swap(QObjectConnectionHolder &other) noexcept
    {
        using std::swap;
        swap(c, other.c);
    }
};

class KDSingleApplicationLocalSocket : public QObject
{
    Q_OBJECT

public:
    // LogSquirl (#827): the lock file and, on Unix, the socket live in
    // `directory`. Empty: no lock and no socket, this instance runs on its own.
    // On Unix the socket is `<directory>/<name>`, the name as given (#841).
    explicit KDSingleApplicationLocalSocket(const QString &name,
                                            const QString &directory,
                                            QObject *parent = nullptr);
    ~KDSingleApplicationLocalSocket();

    bool isPrimaryInstance() const;
    // Whether the primary instance's server is up (LogSquirl, #827).
    bool isListening() const;
    qint64 primaryPid() const;
    QString socketName() const;

    enum class SendResult {
        Sent,
        NotConnected, // nothing listens on the socket (LogSquirl, #827)
        NotSent,
    };

public Q_SLOTS:
    SendResult sendMessage(const QByteArray &message, int timeout);

Q_SIGNALS:
    void messageReceived(const QByteArray &message);

private:
    void handleNewConnection();
    void readDataFromSecondary();
    bool readDataFromSecondarySocket(QLocalSocket *socket);
    void secondaryDisconnected();
    void secondarySocketDisconnected(QLocalSocket *socket);
    void abortConnectionToSecondary();

    QString m_socketName;
    QString m_directory; // LogSquirl (#827): the lock's and socket's folder, removed again when left empty
    qint64 m_primaryPid {-1};
    bool m_unguarded {false};

    std::unique_ptr<QLockFile> m_lockFile; // held by the primary instance
    std::unique_ptr<QLocalServer> m_localServer;

    struct Connection
    {
        explicit Connection(QLocalSocket *s);

        std::unique_ptr<QLocalSocket, QObjectDeleteLater> socket;
        std::unique_ptr<QTimer, QObjectDeleteLater> timeoutTimer;
        QByteArray readData;

        // socket/timeoutTimer are deleted via deleteLater (as we delete them
        // in slots connected to their signals). Before the deleteLater is acted upon,
        // they may emit further signals, triggering logic that it's not supposed
        // to be triggered (as the Connection has already been destroyed).
        // Use this Holder to break the connections.
        QObjectConnectionHolder readDataConnection;
        QObjectConnectionHolder secondaryDisconnectedConnection;
        QObjectConnectionHolder abortConnection;
    };

    std::vector<Connection> m_clients;
};

#endif // KDSINGLEAPPLICATION_LOCALSOCKET_P_H
