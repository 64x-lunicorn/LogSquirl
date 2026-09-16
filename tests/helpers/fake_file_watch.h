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

#ifndef FAKE_FILE_WATCH_H
#define FAKE_FILE_WATCH_H

#include <algorithm>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>

#include "policyfilewatchport.h"
#include "settingspolicies.h"

// A File Watch Port that watches nothing on its own: it reports a change to a
// watched file when a test says so, at once, with no watcher, no polling and
// no waiting. The Log File on disk is still what the log data checks, so
// growing, truncating and replacing it change the file first and then report.
//
// Like a real watcher it reports changes only to the files it was asked to
// watch; each call returns whether it reported one. It keeps the Watch Policies
// it is handed, in order, and follows none of them.
class FakeFileWatch final : public PolicyFileWatchPort {
public:
    void setWatchPolicy( const WatchPolicy& policy ) override
    {
        watchPolicies_.push_back( policy );
    }

    // Every Watch Policy handed over so far, the latest last.
    const std::vector<WatchPolicy>& watchPolicies() const
    {
        return watchPolicies_;
    }

    void addFile( const QString& fileName ) override
    {
        if ( !isWatched( fileName ) ) {
            watchedFiles_.push_back( fileName );
        }
    }

    void removeFile( const QString& fileName ) override
    {
        watchedFiles_.erase( std::remove( watchedFiles_.begin(), watchedFiles_.end(), fileName ),
                             watchedFiles_.end() );
    }

    bool isWatched( const QString& fileName ) const
    {
        return std::find( watchedFiles_.begin(), watchedFiles_.end(), fileName )
               != watchedFiles_.end();
    }

    const std::vector<QString>& watchedFiles() const
    {
        return watchedFiles_;
    }

    // Reports a change to the file, without touching it.
    bool reportChange( const QString& fileName )
    {
        if ( !isWatched( fileName ) ) {
            return false;
        }
        Q_EMIT fileChanged( fileName );
        return true;
    }

    // Appends added to the file and reports the change.
    bool grow( const QString& fileName, const QByteArray& added )
    {
        return write( fileName, added, QIODevice::Append ) && reportChange( fileName );
    }

    // Rewrites the file, in place, with content -- shorter than it was -- and
    // reports the change.
    bool truncate( const QString& fileName, const QByteArray& content )
    {
        return write( fileName, content, QIODevice::Truncate ) && reportChange( fileName );
    }

    // Puts a new file with content under the file's name, the way a log is
    // rotated -- the old file moved aside, still open to whoever reads it --
    // and reports the change.
    bool replace( const QString& fileName, const QByteArray& content )
    {
        ++replacements_;
        const auto replacement = QStringLiteral( "%1.new.%2" ).arg( fileName ).arg( replacements_ );
        const auto rotated = QStringLiteral( "%1.%2" ).arg( fileName ).arg( replacements_ );
        return write( replacement, content, QIODevice::Truncate )
               && QFile::rename( fileName, rotated ) && QFile::rename( replacement, fileName )
               && reportChange( fileName );
    }

private:
    static bool write( const QString& fileName, const QByteArray& content,
                       QIODevice::OpenModeFlag mode )
    {
        QFile file( fileName );
        if ( !file.open( QIODevice::WriteOnly | mode ) ) {
            return false;
        }
        return file.write( content ) == content.size();
    }

    std::vector<QString> watchedFiles_;
    std::vector<WatchPolicy> watchPolicies_;
    int replacements_ = 0;
};

#endif
