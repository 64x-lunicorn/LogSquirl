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

#include "teamfoldergit.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include <utility>

#include "log.h"

namespace logsquirl::teamfolder {

namespace {

// How often a waiting run looks at its stop flag.
constexpr int StopPollMs = 100;

// A run that was killed while it changed the index leaves its lock behind, and
// every later run in the clone then fails on it. The clone is the Team
// Folder's alone and its runs follow one another, so once the killed one is
// gone no lock can belong to anybody else.
void removeOrphanedIndexLock( const QString& workingDirectory )
{
    if ( workingDirectory.isEmpty() ) {
        return;
    }
    const QFileInfo lock(
        QDir( workingDirectory ).filePath( QStringLiteral( ".git/index.lock" ) ) );
    if ( lock.isFile() && lock.lastModified() <= QDateTime::currentDateTime() ) {
        LOG_WARNING << "Team Folder removes the index.lock a stopped run left behind";
        QFile::remove( lock.filePath() );
    }
}

} // namespace

QString GitResult::message() const
{
    return error.trimmed();
}

Git::Git( QString program, StopFlag stop )
    : program_( std::move( program ) )
    , stop_( std::move( stop ) )
{
}

GitResult Git::run( const QStringList& arguments, const QString& workingDirectory ) const
{
    GitResult result;

    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    // LogSquirl has no terminal to answer on. Without this, Git would wait for
    // a user name or password on one, and the sync would never finish; with
    // it, Git fails with its own message. A credential helper, an SSH agent
    // and the rest of the user's configuration still apply.
    environment.insert( QStringLiteral( "GIT_TERMINAL_PROMPT" ), QStringLiteral( "0" ) );
    // The sync reads the tool's output; a translated one would say the same in
    // other words. Ask for the untranslated text.
    environment.insert( QStringLiteral( "LC_ALL" ), QStringLiteral( "C" ) );
    environment.insert( QStringLiteral( "LANGUAGE" ), QStringLiteral( "C" ) );
    process.setProcessEnvironment( environment );
    if ( !workingDirectory.isEmpty() ) {
        process.setWorkingDirectory( workingDirectory );
    }
    process.setProgram( program_ );
    process.setArguments( arguments );

    LOG_DEBUG << "Team Folder runs git " << arguments.join( ' ' );
    process.start( QIODevice::ReadOnly );
    if ( !process.waitForStarted() ) {
        result.error = QCoreApplication::translate(
                           "TeamFolder", "Git could not be started (%1). Install Git, or put it on "
                                         "the PATH, to use a Team Folder." )
                           .arg( process.errorString() );
        LOG_WARNING << "Team Folder: " << result.error;
        return result;
    }
    result.started = true;

    QElapsedTimer elapsed;
    elapsed.start();
    const auto timeoutMs = std::chrono::milliseconds( RunTimeout ).count();
    while ( !process.waitForFinished( StopPollMs ) ) {
        if ( process.state() == QProcess::NotRunning ) {
            break;
        }
        const bool stopped = stop_ && stop_->load();
        if ( stopped || elapsed.elapsed() > timeoutMs ) {
            process.kill();
            process.waitForFinished();
            removeOrphanedIndexLock( workingDirectory );
            result.output = QString::fromUtf8( process.readAllStandardOutput() );
            result.error = stopped ? QCoreApplication::translate( "TeamFolder", "Stopped." )
                                   : QCoreApplication::translate( "TeamFolder",
                                                                  "Git did not finish in time." );
            return result;
        }
    }

    result.output = QString::fromUtf8( process.readAllStandardOutput() );
    result.error = QString::fromUtf8( process.readAllStandardError() );
    result.succeeded = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if ( !result.succeeded && result.error.trimmed().isEmpty() ) {
        result.error = QCoreApplication::translate( "TeamFolder", "Git ended with exit code %1." )
                           .arg( process.exitCode() );
    }
    return result;
}

} // namespace logsquirl::teamfolder
