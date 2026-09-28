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

#include "archivemember.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtConcurrent/QtConcurrentRun>

#include "atomicflag.h"
#include "decompressor.h"
#include "log.h"

namespace {

// Decompresses one level: the one file of a compressed single file, or the
// member of an archive. Empty when it cannot.
QString decompressLevel( const QString& fileName, const QString& member, const QString& directory,
                         AtomicFlag& interrupt )
{
    Decompressor decompressor;
    const auto action = Decompressor::action( fileName );

    if ( action == DecompressAction::Decompress && member.isEmpty() ) {
        // Named as when the user opened it; the directory takes it away.
        QTemporaryFile output{ QDir{ directory }.filePath( QFileInfo( fileName ).fileName() ) };
        output.setAutoRemove( false );
        if ( !output.open() || !decompressor.decompress( fileName, &output, interrupt )
             || !decompressor.waitForResult() ) {
            output.remove();
            return {};
        }
        return output.fileName();
    }

    if ( action == DecompressAction::Extract && !member.isEmpty() ) {
        QTemporaryDir extracted{ QDir{ directory }.filePath( QFileInfo( fileName ).fileName() ) };
        extracted.setAutoRemove( false );
        if ( !extracted.isValid() || !decompressor.extract( fileName, extracted.path(), interrupt )
             || !decompressor.waitForResult() ) {
            extracted.remove();
            return {};
        }

        // A member names a file inside the archive, never one beside it.
        const auto path = QDir::cleanPath( extracted.filePath( member ) );
        if ( !path.startsWith( QDir::cleanPath( extracted.path() ) + '/' )
             || !QFileInfo{ path }.isFile() ) {
            LOG_WARNING << "No member " << member << " in " << fileName;
            return {};
        }
        return path;
    }

    LOG_WARNING << "Cannot take member '" << member << "' from " << fileName;
    return {};
}

} // namespace

QString decompressArchiveMember( const ArchiveMember& member, const QString& directory )
{
    AtomicFlag interrupt;
    return decompressArchiveMember( member, directory, interrupt );
}

QString decompressArchiveMember( const ArchiveMember& member, const QString& directory,
                                 AtomicFlag& interrupt )
{
    if ( member.isEmpty() || member.members.isEmpty() ) {
        return {};
    }

    // Gone like any missing Log File: nothing to report (#596).
    if ( !QFileInfo{ member.archive }.isFile() ) {
        LOG_INFO << "The archive " << member.archive << " is gone";
        return {};
    }

    auto current = member.archive;
    for ( const auto& level : member.members ) {
        if ( interrupt ) {
            LOG_INFO << "Interrupted decompressing " << member.archive;
            return {};
        }
        current = decompressLevel( current, level, directory, interrupt );
        if ( current.isEmpty() ) {
            return {};
        }
    }
    return current;
}

ArchiveMemberDecompression::ArchiveMemberDecompression( QString directory, QObject* parent )
    : QObject( parent )
    , directory_( std::move( directory ) )
{
    connect( &watcher_, &QFutureWatcher<QString>::finished, this,
             &ArchiveMemberDecompression::finishCurrent );
}

ArchiveMemberDecompression::~ArchiveMemberDecompression()
{
    queued_.clear();
    if ( interrupt_ ) {
        interrupt_->set();
    }
    // Nothing may be written into the directory once its owner removes it.
    watcher_.disconnect( this );
    watcher_.waitForFinished();
}

void ArchiveMemberDecompression::decompress( const ArchiveMember& member, Answer answer )
{
    queued_.push_back( Request{ member, std::move( answer ) } );
    if ( !current_ ) {
        startNext();
    }
}

void ArchiveMemberDecompression::cancelCurrent()
{
    if ( current_ && interrupt_ ) {
        interrupt_->set();
    }
}

void ArchiveMemberDecompression::cancelAll()
{
    queued_.clear();
    if ( current_ ) {
        current_->answer = {};
        cancelCurrent();
    }
    Q_EMIT idle();
}

bool ArchiveMemberDecompression::isIdle() const
{
    return !current_ && queued_.empty();
}

void ArchiveMemberDecompression::startNext()
{
    if ( queued_.empty() ) {
        return;
    }

    current_ = std::move( queued_.front() );
    queued_.pop_front();

    // Each request its own flag: the thread keeps the one it was started
    // with, whatever is cancelled after it.
    interrupt_ = std::make_shared<AtomicFlag>();
    watcher_.setFuture( QtConcurrent::run(
        [ member = current_->member, directory = directory_, interrupt = interrupt_ ] {
            return decompressArchiveMember( member, directory, *interrupt );
        } ) );

    Q_EMIT decompressing( current_->member.archive );
}

void ArchiveMemberDecompression::finishCurrent()
{
    if ( !current_ ) {
        return;
    }

    const auto interrupted = static_cast<bool>( *interrupt_ );
    const auto fileName = interrupted ? QString{} : watcher_.result();
    auto answer = std::move( current_->answer );
    current_.reset();

    const auto wasCancelledAll = !answer && queued_.empty();
    startNext();
    if ( isIdle() && !wasCancelledAll ) {
        Q_EMIT idle();
    }

    // Last: the answer may do anything, even delete this.
    if ( answer ) {
        answer( fileName );
    }
}
