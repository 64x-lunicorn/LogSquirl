/*
  This file is part of KDSingleApplication.

  SPDX-FileCopyrightText: 2019-2023 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>

  SPDX-License-Identifier: MIT

  Contact KDAB at <info@kdab.com> for commercial licensing options.

  Modified for LogSquirl (#827): see README.md in this directory.
*/
#ifndef KDSINGLEAPPLICATION_H
#define KDSINGLEAPPLICATION_H

#include <QtCore/QObject>

#include <memory>

#include "kdsingleapplication_lib.h"

class KDSingleApplicationPrivate;

class KDSINGLEAPPLICATION_EXPORT KDSingleApplication : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool isPrimaryInstance READ isPrimaryInstance CONSTANT)

public:
    explicit KDSingleApplication(QObject *parent = nullptr);
    explicit KDSingleApplication(const QString &name, QObject *parent = nullptr);
    // LogSquirl (#827): the lock file and, on Unix, the local socket are made
    // in `directory`, which the caller keeps reachable by the user alone. The
    // other constructors use the temporary directory, as upstream does. An
    // empty directory makes an instance that takes no lock and listens
    // nowhere: it is primary, on its own.
    explicit KDSingleApplication(const QString &name, const QString &directory, QObject *parent = nullptr);
    ~KDSingleApplication();

    QString name() const;
    // Whether this instance holds the lock (or runs unguarded). LogSquirl
    // (#827): a primary instance that failed to listen is still primary.
    bool isPrimaryInstance() const;
    // Whether this primary instance receives messages (LogSquirl, #827).
    bool isListening() const;
    qint64 primaryPid() const;
    // The socket's name: on Unix its full path (LogSquirl, #827).
    QString socketName() const;

    enum class SendResult {
        Sent,
        NotConnected, // nothing listens on the socket (LogSquirl, #827)
        NotSent,
    };

    // LogSquirl (#827): like sendMessageWithTimeout, telling a primary
    // instance that could not be connected to from a send that failed.
    SendResult sendMessageWithResult(const QByteArray &message, int timeout);

public Q_SLOTS:
    // avoid default arguments and overloads, as they don't mix with connections
    bool sendMessage(const QByteArray &message);
    bool sendMessageWithTimeout(const QByteArray &message, int timeout);

Q_SIGNALS:
    void messageReceived(const QByteArray &message);

private:
    Q_DECLARE_PRIVATE(KDSingleApplication)
    KDSingleApplicationPrivate* d_ptr;
};

#endif // KDSINGLEAPPLICATION_H
