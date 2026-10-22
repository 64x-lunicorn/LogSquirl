/*
  This file is part of KDSingleApplication.

  SPDX-FileCopyrightText: 2019-2023 Klarälvdalens Datakonsult AB, a KDAB Group company <info@kdab.com>

  SPDX-License-Identifier: MIT

  Contact KDAB at <info@kdab.com> for commercial licensing options.

  Modified for LogSquirl (#827): see README.md in this directory.
*/

#include "kdsingleapplication.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

// TODO: make this pluggable.
#include "kdsingleapplication_localsocket_p.h"

// Avoiding dragging in Qt private APIs for now, so this does not inherit
// from QObjectPrivate.
class KDSingleApplicationPrivate
{
public:
    explicit KDSingleApplicationPrivate(const QString &name, const QString &directory, KDSingleApplication *q);

    void initialize();

    QString name() const
    {
        return m_name;
    }

    bool isPrimaryInstance() const
    {
        return m_impl.isPrimaryInstance();
    }

    bool isListening() const
    {
        return m_impl.isListening();
    }

    qint64 primaryPid() const
    {
        return m_impl.primaryPid();
    }

    QString socketName() const
    {
        return m_impl.socketName();
    }

    KDSingleApplication::SendResult sendMessage(const QByteArray &message, int timeout)
    {
        switch (m_impl.sendMessage(message, timeout)) {
        case KDSingleApplicationLocalSocket::SendResult::Sent:
            return KDSingleApplication::SendResult::Sent;
        case KDSingleApplicationLocalSocket::SendResult::NotConnected:
            return KDSingleApplication::SendResult::NotConnected;
        case KDSingleApplicationLocalSocket::SendResult::NotSent:
            break;
        }
        return KDSingleApplication::SendResult::NotSent;
    }

private:
    Q_DECLARE_PUBLIC(KDSingleApplication)

    KDSingleApplication *q_ptr;
    QString m_name;

    KDSingleApplicationLocalSocket m_impl;
};

KDSingleApplicationPrivate::KDSingleApplicationPrivate(const QString &name, const QString &directory, KDSingleApplication *q)
    : q_ptr(q)
    , m_name(name)
    , m_impl(name, directory)
{
    if (Q_UNLIKELY(name.isEmpty()))
        qFatal("KDSingleApplication requires a non-empty application name");

    if (isListening()) {
        QObject::connect(&m_impl, &KDSingleApplicationLocalSocket::messageReceived,
                         q, &KDSingleApplication::messageReceived);
    }
}

static QString extractExecutableName(const QString &applicationFilePath)
{
    return QFileInfo(applicationFilePath).fileName();
}

KDSingleApplication::KDSingleApplication(QObject *parent)
    : KDSingleApplication(extractExecutableName(QCoreApplication::applicationFilePath()), parent)
{
}

KDSingleApplication::KDSingleApplication(const QString &name, QObject *parent)
    : KDSingleApplication(name, QDir::tempPath(), parent)
{
}

KDSingleApplication::KDSingleApplication(const QString &name, const QString &directory, QObject *parent)
    : QObject(parent)
    , d_ptr(new KDSingleApplicationPrivate(name, directory, this))
{
}

QString KDSingleApplication::name() const
{
    Q_D(const KDSingleApplication);
    return d->name();
}

bool KDSingleApplication::isPrimaryInstance() const
{
    Q_D(const KDSingleApplication);
    return d->isPrimaryInstance();
}

bool KDSingleApplication::isListening() const
{
    Q_D(const KDSingleApplication);
    return d->isListening();
}

qint64 KDSingleApplication::primaryPid() const
{
    Q_D(const KDSingleApplication);
    return d->primaryPid();
}

QString KDSingleApplication::socketName() const
{
    Q_D(const KDSingleApplication);
    return d->socketName();
}

bool KDSingleApplication::sendMessage(const QByteArray &message)
{
    return sendMessageWithTimeout(message, 5000);
}

bool KDSingleApplication::sendMessageWithTimeout(const QByteArray &message, int timeout)
{
    return sendMessageWithResult(message, timeout) == SendResult::Sent;
}

KDSingleApplication::SendResult KDSingleApplication::sendMessageWithResult(const QByteArray &message, int timeout)
{
    Q_ASSERT(!isPrimaryInstance());

    Q_D(KDSingleApplication);
    return d->sendMessage(message, timeout);
}


KDSingleApplication::~KDSingleApplication() {
    delete d_ptr;
}
