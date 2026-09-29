/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MESSAGERECEIVER_H
#define MESSAGERECEIVER_H

#include <QtCore/QCborValue>

#include <QtCore/QJsonDocument>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include "instancehandover.h"
#include "log.h"
#include "logsquirl_version.h"

/*
 * Class receiving messages from another instance of logsquirl.
 * Messages are forwarded to the application by signals.
 */
class MessageReceiver final : public QObject {
    Q_OBJECT

public:
    MessageReceiver()
        : QObject()
    {
    }

Q_SIGNALS:
    void loadFile( const QString& filename );
    // Standard input a secondary instance reads and spools to `spoolPath`,
    // which it hands over to be opened and owned here, in a tab named
    // `displayName` -- empty when it sent none (#623).
    void openStandardInputSpool( const QString& spoolPath, const QString& displayName );

public Q_SLOTS:
    void receiveMessage( const QByteArray& message )
    {
        LOG_INFO
            << "Message "
            << QJsonDocument::fromVariant( QCborValue::fromCbor( message ).toVariant() ).toJson();

        const auto handOver = readHandOverMessage( message, logsquirlVersion() );
        if ( !handOver ) {
            return;
        }

        for ( const auto& f : handOver->files ) {
            Q_EMIT loadFile( f );
        }
        // Last, so that its tab is the one in front.
        if ( !handOver->standardInputSpool.isEmpty() ) {
            Q_EMIT openStandardInputSpool( handOver->standardInputSpool,
                                           handOver->standardInputName );
        }
    }
};

#endif // MESSAGERECEIVER_H
