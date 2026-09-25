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

#include "mergecontroller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

#include "log.h"

MergeController::MergeController( QObject* parent )
    : QObject( parent )
{
    rebuildTimer_.setSingleShot( true );
    rebuildTimer_.setInterval( 300 );
    connect( &rebuildTimer_, &QTimer::timeout, this, &MergeController::doMerge );
    // Well past a modification time tick, so a write missed in the tick a
    // watch began in shows as a different time or size by then.
    recheckTimer_.setSingleShot( true );
    recheckTimer_.setInterval( 300 );
    connect( &recheckTimer_, &QTimer::timeout, this, &MergeController::recheckSources );
    connect( &sourceWatcher_, &QFileSystemWatcher::fileChanged, this,
             &MergeController::onSourceChanged );
}

MergeController::~MergeController()
{
    // Clean up the temp file
    if ( !mergedFilePath_.isEmpty() ) {
        QFile::remove( mergedFilePath_ );
    }
}

QString MergeController::merge( const QStringList& sourcePaths, bool dedup )
{
    sourcePaths_ = sourcePaths;
    dedup_ = dedup;

    if ( !sourceWatcher_.files().isEmpty() ) {
        sourceWatcher_.removePaths( sourceWatcher_.files() );
    }

    // Create a stable temp file path in the app's temp directory
    const auto tempDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation );
    const auto uniqueId = QUuid::createUuid().toString( QUuid::Id128 ).left( 12 );
    mergedFilePath_
        = QDir( tempDir ).filePath( QString( "logsquirl_merged_%1.log" ).arg( uniqueId ) );

    doMerge();
    watchSources();

    return mergedFilePath_;
}

QString MergeController::mergedFilePath() const
{
    return mergedFilePath_;
}

const QStringList& MergeController::sourcePaths() const
{
    return sourcePaths_;
}

void MergeController::scheduleRebuild()
{
    rebuildTimer_.start();
}

MergeController::SourceState MergeController::sourceStateOnDisk( const QString& path )
{
    const QFileInfo info( path );
    if ( !info.exists() ) {
        return {};
    }
    return { true, info.size(), info.lastModified() };
}

void MergeController::watchSources()
{
    const auto watched = sourceWatcher_.files();
    bool startedWatching = false;
    for ( const auto& path : sourcePaths_ ) {
        if ( !watched.contains( path ) && QFile::exists( path ) ) {
            startedWatching = sourceWatcher_.addPath( path ) || startedWatching;
        }
    }
    if ( startedWatching ) {
        recheckTimer_.start();
    }
}

void MergeController::recheckSources()
{
    for ( const auto& path : sourcePaths_ ) {
        if ( sourceStateOnDisk( path ) != mergedSourceStates_.value( path ) ) {
            doMerge();
            return;
        }
    }
}

void MergeController::onSourceChanged()
{
    scheduleRebuild();
}

void MergeController::doMerge()
{
    if ( mergedFilePath_.isEmpty() ) {
        return;
    }

    QFile outFile( mergedFilePath_ );
    if ( !outFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) ) {
        LOG_ERROR << "MergeController: cannot open " << mergedFilePath_;
        return;
    }

    QSet<QByteArray> seen;
    QTextStream out( &outFile );

    // Taken before a source is read, so a write during the read shows as a
    // difference on the next check.
    mergedSourceStates_.clear();
    for ( const auto& path : sourcePaths_ ) {
        mergedSourceStates_.insert( path, sourceStateOnDisk( path ) );
        QFile srcFile( path );
        if ( !srcFile.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
            LOG_WARNING << "MergeController: cannot read source " << path;
            continue;
        }

        QTextStream in( &srcFile );
        while ( !in.atEnd() ) {
            const auto line = in.readLine();
            if ( dedup_ ) {
                const auto hash
                    = QCryptographicHash::hash( line.toUtf8(), QCryptographicHash::Md5 );
                if ( seen.contains( hash ) ) {
                    continue;
                }
                seen.insert( hash );
            }
            out << line << '\n';
        }
    }

    outFile.close();

    // A source replaced by a new file (rotation, editors saving atomically)
    // drops out of the watcher; pick it up again.
    watchSources();

    Q_EMIT mergedFileUpdated();
}
