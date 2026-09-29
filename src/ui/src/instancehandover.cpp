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

#include <QFile>

#include <cstdlib>
#include <future>
#include <optional>
#include <thread>

#include "log.h"
#include "stdinpump.h"
#include "streamwriter.h"

namespace {

const auto VersionKey = QStringLiteral( "version" );
const auto FilesKey = QStringLiteral( "files" );
const auto StandardInputSpoolKey = QStringLiteral( "stdinSpool" );
const auto StandardInputNameKey = QStringLiteral( "stdinName" );

// The name the primary instance gives the marker of a spool file it took over.
const auto AdoptionMarkerName = QStringLiteral( "adopted" );
// How often a secondary instance looks for the marker while it waits for it.
constexpr std::chrono::milliseconds AdoptionPollInterval{ 20 };

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
        message.insert( StandardInputNameKey, handOver.standardInputName );
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
    handOver.standardInputName = map.value( StandardInputNameKey ).toString();
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

QString spoolAdoptionMarker( const QString& spoolPath )
{
    return QFileInfo( spoolPath ).absoluteDir().filePath( AdoptionMarkerName );
}

bool markSpoolAdopted( const QString& spoolPath )
{
    QFile marker( spoolAdoptionMarker( spoolPath ) );
    return marker.open( QIODevice::WriteOnly );
}

int handOverStandardInput( const std::vector<QString>& files, int fd, const QString& version,
                           const SendToPrimaryInstance& send, std::ostream& errors,
                           const HandOverWaits& waits )
{
    logsquirl::plugins::StreamWriter writer( QStringLiteral( "stdin" ) );
    const auto spool = writer.filePath();
    if ( spool.isEmpty() ) {
        errors << "logsquirl: could not create a file for what arrives on standard input.\n";
        return EXIT_FAILURE;
    }

    constexpr auto CannotHandOver
        = "logsquirl: could not hand standard input over to the running logsquirl. "
          "'logsquirl --multi -' opens it in a window of its own.\n";

    LOG_INFO << "Handing over " << files.size() << " file(s) and standard input, spooled to "
             << spool << ", to the primary instance";
    if ( !send( handOverMessage( HandOver{ files, spool, writer.displayName() }, version ) ) ) {
        // The writer removes the spool file as it goes.
        errors << CannotHandOver;
        return EXIT_FAILURE;
    }

    std::promise<void> closed;
    auto inputClosed = closed.get_future();
    // Read at once, so that the writer is not held up while the primary
    // instance gets to the message.
    std::optional<logsquirl::plugins::StdinPump> pump;
    pump.emplace( fd, writer, [ &closed ] { closed.set_value(); } );

    // The primary instance takes the spool file over, or drops the message.
    const auto marker = spoolAdoptionMarker( spool );
    const auto adoptionDeadline = std::chrono::steady_clock::now() + waits.adoption;
    while ( !QFileInfo::exists( marker ) ) {
        if ( std::chrono::steady_clock::now() >= adoptionDeadline ) {
            LOG_ERROR << "The primary instance did not take " << spool << " over";
            pump.reset();
            // The writer removes the spool file as it goes.
            errors << CannotHandOver;
            return EXIT_FAILURE;
        }
        std::this_thread::sleep_for( AdoptionPollInterval );
    }
    writer.keepFile();

    // Until standard input closes, or its tab does: nobody reads the spool
    // file then, which would only grow.
    while ( inputClosed.wait_for( waits.adoptionCheck ) != std::future_status::ready ) {
        if ( !QFileInfo::exists( marker ) ) {
            LOG_INFO << "The tab of " << spool << " was closed, standard input is no longer read";
            break;
        }
    }
    pump.reset();
    return EXIT_SUCCESS;
}
