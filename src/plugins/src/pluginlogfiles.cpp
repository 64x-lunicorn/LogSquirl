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

#include "pluginlogfiles.h"

#include <QFileInfo>

#include <algorithm>
#include <ranges>
#include <utility>

namespace logsquirl::plugins {

namespace {

// The same path, or the same file on disk by another spelling: two files
// that are not on disk -- a Log File deleted while it is open, say -- are the
// same only by their path, as the file system has no other name for them.
bool sameFile( const QFileInfo& one, const QFileInfo& other )
{
    if ( one.absoluteFilePath() == other.absoluteFilePath() ) {
        return true;
    }
    return one.exists() && other.exists() && one == other;
}

} // namespace

PluginLogFiles::Id PluginLogFiles::add( const QString& path,
                                        std::shared_ptr<const PluginLogFileSource> source )
{
    const std::scoped_lock lock( mutex_ );
    const auto id = ++idsSoFar_;
    logFiles_.push_back( std::make_shared<LogFile>( id, path, std::move( source ) ) );
    return id;
}

void PluginLogFiles::remove( Id id )
{
    std::shared_ptr<LogFile> removed;
    {
        const std::scoped_lock lock( mutex_ );
        const auto it = std::ranges::find( logFiles_, id, &LogFile::id_ );
        if ( it == logFiles_.end() ) {
            return;
        }
        removed = std::move( *it );
        logFiles_.erase( it );
        removed->open_.store( false );
    }
    // A read under way keeps its own hold; the last one lets the data go.
}

std::shared_ptr<const PluginLogFiles::LogFile> PluginLogFiles::find( const QString& path ) const
{
    if ( path.isEmpty() ) {
        return nullptr;
    }
    // Compared outside the lock: comparing as the file system names a file
    // may look at the disk.
    std::vector<std::shared_ptr<LogFile>> open;
    {
        const std::scoped_lock lock( mutex_ );
        open = logFiles_;
    }
    const QFileInfo wanted( path );
    for ( const auto& logFile : open | std::views::reverse ) {
        if ( sameFile( QFileInfo( logFile->path() ), wanted ) ) {
            return logFile;
        }
    }
    return nullptr;
}

} // namespace logsquirl::plugins
