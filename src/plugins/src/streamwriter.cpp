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

#include "streamwriter.h"

#include "log.h"

#include <QDir>
#include <QMutexLocker>

namespace logsquirl::plugins {

StreamWriter::StreamWriter( const QString& displayName )
    : displayName_( displayName )
{
    if ( !tempDir_.isValid() ) {
        LOG_ERROR << "Failed to create temp directory for stream: " << displayName;
        return;
    }

    // Use a .log extension so FileWatcher treats it normally
    const auto path = tempDir_.path() + "/stream.log";
    file_.setFileName( path );

    if ( !file_.open( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text ) ) {
        LOG_ERROR << "Failed to create stream file: " << path;
    }
    else {
        LOG_INFO << "StreamWriter created: " << path << " for '" << displayName << "'";
    }
}

StreamWriter::~StreamWriter()
{
    QMutexLocker lock( &mutex_ );
    if ( file_.isOpen() ) {
        file_.close();
    }
    // QTemporaryDir cleans up automatically
}

QString StreamWriter::filePath() const
{
    return file_.fileName();
}

void StreamWriter::pushLine( const char* data, size_t len )
{
    QMutexLocker lock( &mutex_ );
    if ( !file_.isOpen() || finished_ ) {
        return;
    }

    file_.write( data, static_cast<qint64>( len ) );
    file_.write( "\n", 1 );
    file_.flush();
}

void StreamWriter::pushLines( const char* const* data, const size_t* lens, size_t count )
{
    QMutexLocker lock( &mutex_ );
    if ( !file_.isOpen() || finished_ ) {
        return;
    }

    for ( size_t i = 0; i < count; ++i ) {
        file_.write( data[ i ], static_cast<qint64>( lens[ i ] ) );
        file_.write( "\n", 1 );
    }
    file_.flush();
}

void StreamWriter::signalEos()
{
    QMutexLocker lock( &mutex_ );
    finished_ = true;
    if ( file_.isOpen() ) {
        file_.flush();
    }
    LOG_INFO << "StreamWriter EOS for '" << displayName_ << "'";
}

} // namespace logsquirl::plugins
