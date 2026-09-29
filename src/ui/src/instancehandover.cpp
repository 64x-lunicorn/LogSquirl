/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "instancehandover.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QDir>
#include <QFileInfo>

#include <cstdlib>
#include <future>

#include "log.h"
#include "stdinpump.h"
#include "streamwriter.h"

namespace {

const auto VersionKey = QStringLiteral( "version" );
const auto FilesKey = QStringLiteral( "files" );
const auto StandardInputSpoolKey = QStringLiteral( "stdinSpool" );

// The name StreamWriter gives the file in its folder.
const auto SpoolFileName = QStringLiteral( "stream.log" );

} // namespace

QByteArray handOverMessage( const HandOver& handOver, const QString& version )
{
    QCborArray files;
    for ( const auto& file : handOver.files ) {
        files.append( file );
    }

    QCborMap message;
    message.insert( VersionKey, version );
    message.insert( FilesKey, files );
    if ( !handOver.standardInputSpool.isEmpty() ) {
        message.insert( StandardInputSpoolKey, handOver.standardInputSpool );
    }
    return message.toCborValue().toCbor();
}

std::optional<HandOver> readHandOverMessage( const QByteArray& message, const QString& version )
{
    const auto value = QCborValue::fromCbor( message );
    if ( !value.isMap() ) {
        return std::nullopt;
    }
    const auto map = value.toMap();
    if ( map.value( VersionKey ).toString() != version ) {
        return std::nullopt;
    }

    HandOver handOver;
    for ( const auto& file : map.value( FilesKey ).toArray() ) {
        handOver.files.push_back( file.toString() );
    }
    handOver.standardInputSpool = map.value( StandardInputSpoolKey ).toString();
    return handOver;
}

bool isStandardInputSpool( const QString& path )
{
    if ( path.isEmpty() ) {
        return false;
    }
    const QFileInfo file( path );
    if ( file.fileName() != SpoolFileName || !file.isFile() ) {
        return false;
    }
    const QFileInfo folder( file.absolutePath() );
    const QFileInfo temporaryFolder( QDir::tempPath() );
    return QFileInfo( folder.absolutePath() ).canonicalFilePath()
           == temporaryFolder.canonicalFilePath();
}

int handOverStandardInput( const std::vector<QString>& files, int fd, const QString& version,
                           const SendToPrimaryInstance& send, std::ostream& errors )
{
    logsquirl::plugins::StreamWriter writer( QStringLiteral( "stdin" ) );
    const auto spool = writer.filePath();
    if ( spool.isEmpty() ) {
        errors << "logsquirl: could not create a file for what arrives on standard input.\n";
        return EXIT_FAILURE;
    }

    LOG_INFO << "Handing over " << files.size() << " file(s) and standard input, spooled to "
             << spool << ", to the primary instance";
    if ( !send( handOverMessage( HandOver{ files, spool }, version ) ) ) {
        // The writer removes the spool file as it goes.
        errors << "logsquirl: could not hand standard input over to the running logsquirl. "
                  "'logsquirl --multi -' opens it in a window of its own.\n";
        return EXIT_FAILURE;
    }
    writer.keepFile();

    std::promise<void> closed;
    auto inputClosed = closed.get_future();
    {
        const logsquirl::plugins::StdinPump pump( fd, writer, [ &closed ] { closed.set_value(); } );
        inputClosed.wait();
    }
    return EXIT_SUCCESS;
}
