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

    // Create a stable temp file path in the app's temp directory
    const auto tempDir = QStandardPaths::writableLocation( QStandardPaths::TempLocation );
    const auto uniqueId = QUuid::createUuid().toString( QUuid::Id128 ).left( 12 );
    mergedFilePath_ = QDir( tempDir ).filePath(
        QString( "logsquirl_merged_%1.log" ).arg( uniqueId ) );

    doMerge();

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

    for ( const auto& path : sourcePaths_ ) {
        QFile srcFile( path );
        if ( !srcFile.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
            LOG_WARNING << "MergeController: cannot read source " << path;
            continue;
        }

        QTextStream in( &srcFile );
        while ( !in.atEnd() ) {
            const auto line = in.readLine();
            if ( dedup_ ) {
                const auto hash = QCryptographicHash::hash( line.toUtf8(),
                                                            QCryptographicHash::Md5 );
                if ( seen.contains( hash ) ) {
                    continue;
                }
                seen.insert( hash );
            }
            out << line << '\n';
        }
    }

    outFile.close();
    Q_EMIT mergedFileUpdated();
}
