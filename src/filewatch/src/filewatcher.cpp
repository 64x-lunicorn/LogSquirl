/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include "filewatcher.h"

#include "dispatch_to.h"
#include "log.h"
#include "synchronization.h"

#include <KDSignalThrottler.h>
#include <efsw/efsw.hpp>

#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace {

struct WatchedFile {
    std::string name;
    int64_t mTime;
    int64_t size;

    bool operator==( const std::string& filename ) const
    {
        return name == filename;
    }

    bool operator!=( const WatchedFile& other ) const
    {
        return name != other.name || mTime != other.mTime || size != other.size;
    }
};

struct WatchedDirectory {
    efsw::WatchID watchId;

    // filenames are in utf8
    std::string name;
    std::vector<WatchedFile> files;
};

bool isOnlyForPolling( const WatchedDirectory& wd )
{
    return wd.watchId < 0;
}

} // namespace

class EfswFileWatcher final : public efsw::FileWatchListener {
public:
    explicit EfswFileWatcher( FileWatcher* parent )
        : parent_{ parent }
    {
    }

    void enableWatch( bool enable )
    {
        ScopedRecursiveLock lock( mutex_ );

        if ( nativeWatchEnabled_ == enable ) {
            return;
        }

        nativeWatchEnabled_ = enable;

        if ( enable ) {
            for ( auto& dir : watchedPaths_ ) {
                LOG_INFO << "Will reenable watch for " << dir.name;
                dir.watchId = watcher_.addWatch( dir.name, this, false );
            }
        }
        else {
            for ( const auto& dir : watchedPaths_ ) {
                LOG_INFO << "Will disable watch for " << dir.name;
                watcher_.removeWatch( dir.watchId );
            }
        }
    }

    void addFile( const QString& fullFileName )
    {
        ScopedRecursiveLock lock( mutex_ );

        LOG_DEBUG << fullFileName.toStdString();

        const QFileInfo fileInfo = QFileInfo( fullFileName );

        auto watchedFile
            = WatchedFile{ fileInfo.fileName().toStdString(),
                           fileInfo.lastModified().toMSecsSinceEpoch(), fileInfo.size() };

        const auto directory = fileInfo.absolutePath().toStdString();

        const auto wasEmpty = watchedPaths_.empty();

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &directory ]( const auto& wd ) { return wd.name == directory; } );

        const auto tryWatchDirectory = [ this ]( const std::string& path ) {
            auto watchId = nativeWatchEnabled_ ? watcher_.addWatch( path, this, false ) : -111;

            if ( watchId < 0 ) {
                LOG_WARNING << "failed to add watch " << path << " error " << watchId;
            }

            return watchId;
        };

        if ( watchedDirectory == watchedPaths_.end() ) {
            watchedPaths_.push_back(
                { tryWatchDirectory( directory ), directory, { std::move( watchedFile ) } } );
        }
        else {

            if ( isOnlyForPolling( *watchedDirectory ) ) {
                watchedDirectory->watchId = tryWatchDirectory( directory );
            }

            if ( std::find( watchedDirectory->files.begin(), watchedDirectory->files.end(),
                            watchedFile.name )
                 != watchedDirectory->files.end() ) {
                LOG_DEBUG << "already watching " << watchedFile.name << " in " << directory;
                return;
            }

            watchedDirectory->files.emplace_back( std::move( watchedFile ) );
        }

        if ( wasEmpty ) {
            watcher_.watch();
        }
    }

    void removeFile( const QString& fullFileName )
    {
        ScopedRecursiveLock lock( mutex_ );

        LOG_DEBUG << fullFileName.toStdString();

        const QFileInfo fileInfo = QFileInfo( fullFileName );

        const auto directory = fileInfo.absolutePath().toStdString();

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &directory ]( const auto& wd ) { return wd.name == directory; } );

        if ( watchedDirectory != watchedPaths_.end() ) {
            const auto filename = fileInfo.fileName().toStdString();

            auto& files = watchedDirectory->files;

            auto watchedFile = std::find( files.begin(), files.end(), filename );

            if ( watchedFile != files.end() ) {
                files.erase( watchedFile );
            }

            if ( files.empty() ) {

                if ( !isOnlyForPolling( *watchedDirectory ) ) {
                    watcher_.removeWatch( watchedDirectory->watchId );
                }

                watchedPaths_.erase( watchedDirectory );
            }
        }
        else {
            LOG_WARNING << "The file is not watched";
        }

        for ( const auto& d : watcher_.directories() ) {
            LOG_INFO << "Directories still watched: " << d;
        }
    }

    // Callable from any thread, including a dedicated polling thread (#322):
    // the watch list is copied out under the lock, every file is stat'd with
    // no lock held at all, and the lock is taken again only to write back
    // each file's new size and modification time and to collect the paths
    // that changed. A file added or removed from the UI thread while a poll
    // is in flight is safe either way: the write-back looks the file back up
    // by name and skips it if it is no longer watched.
    void checkWatches()
    {
        struct WatchedEntry {
            std::string directory;
            WatchedFile file;
        };

        std::vector<WatchedEntry> snapshot;
        {
            ScopedRecursiveLock lock( mutex_ );

            for ( const auto& dir : watchedPaths_ ) {
                for ( const auto& file : dir.files ) {
                    snapshot.push_back( { dir.name, file } );
                }
            }
        }

        struct StatResult {
            std::string directory;
            std::string previousName;
            WatchedFile updated;
            bool changed;
        };

        std::vector<StatResult> results;
        results.reserve( snapshot.size() );

        for ( const auto& entry : snapshot ) {
            const auto path
                = QDir::cleanPath( QString::fromStdString( entry.directory ) + QDir::separator()
                                   + QString::fromStdString( entry.file.name ) );

            const auto fileInfo = QFileInfo{ path };

            auto updated
                = WatchedFile{ fileInfo.fileName().toStdString(),
                               fileInfo.lastModified().toMSecsSinceEpoch(), fileInfo.size() };

            const auto changed = entry.file != updated;

            results.push_back(
                { entry.directory, entry.file.name, std::move( updated ), changed } );
        }

        std::vector<QString> changedFiles;
        {
            ScopedRecursiveLock lock( mutex_ );

            for ( auto& result : results ) {
                auto watchedDirectory = std::find_if(
                    watchedPaths_.begin(), watchedPaths_.end(),
                    [ &result ]( const auto& wd ) { return wd.name == result.directory; } );

                if ( watchedDirectory == watchedPaths_.end() ) {
                    continue;
                }

                auto file = std::find( watchedDirectory->files.begin(),
                                       watchedDirectory->files.end(), result.previousName );

                if ( file == watchedDirectory->files.end() ) {
                    continue;
                }

                if ( result.changed ) {
                    const auto path = QDir::cleanPath(
                        QString::fromStdString( result.directory ) + QDir::separator()
                        + QString::fromStdString( result.previousName ) );

                    changedFiles.push_back( path );
                    LOG_INFO << "will notify for " << path.toStdString();
                }

                *file = std::move( result.updated );
            }
        }

        for ( const auto& changedFile : changedFiles ) {
            dispatchToMainThread( [ watcher = parent_, changedFile ]() {
                watcher->fileChangedOnDisk( changedFile );
            } );
        }
    }

    void handleFileAction( efsw::WatchID watchid, const std::string& dir,
                           const std::string& filename, efsw::Action action,
                           const std::string& oldFilename ) override
    {
        Q_UNUSED( watchid );
        Q_UNUSED( action );

        LOG_DEBUG << "Notification from esfw for " << dir;

        // post to other thread to avoid deadlock between internal esfw lock and our mutex_
        dispatchToThread( [ =, this ]() { notifyOnFileAction( dir, filename, oldFilename ); },
                          parent_->thread() );
    }

    void notifyOnFileAction( const std::string& dir, const std::string& filename,
                             const std::string& oldFilename )
    {
        auto qtDir = QString::fromStdString( dir );
        if ( qtDir.endsWith( QDir::separator() ) ) {
            qtDir.chop( 1 );
        }

        const auto& directory = qtDir.toStdString();

        LOG_DEBUG << "fileChangedOnDisk " << directory << " " << filename << ", old name "
                  << oldFilename;

        const auto& fullChangedFilename = findChangedFilename( directory, filename, oldFilename );

        if ( !fullChangedFilename.isEmpty() ) {
            dispatchToMainThread( [ watcher = parent_, fullChangedFilename ]() {
                watcher->fileChangedOnDisk( fullChangedFilename );
            } );
        }
    }

    QString findChangedFilename( const std::string& directory, const std::string& filename,
                                 const std::string& oldFilename )
    {
        ScopedRecursiveLock lock( mutex_ );

        auto watchedDirectory
            = std::find_if( watchedPaths_.begin(), watchedPaths_.end(),
                            [ &directory ]( const auto& wd ) { return wd.name == directory; } );

        if ( watchedDirectory != watchedPaths_.end() ) {
            std::string changedFilename;

            const auto isFileWatched
                = std::any_of( watchedDirectory->files.begin(), watchedDirectory->files.end(),
                               [ &filename, &oldFilename, &changedFilename ]( const auto& f ) {
                                   if ( f.name == filename || f.name == oldFilename ) {
                                       changedFilename = f.name;
                                       return true;
                                   }

                                   return false;
                               } );

            if ( isFileWatched ) {
                LOG_DEBUG << "fileChangedOnDisk - will notify for " << filename << ", old name "
                          << oldFilename;

                return QDir::cleanPath( QString::fromStdString( directory ) + QDir::separator()
                                        + QString::fromStdString( changedFilename ) );
            }
            else {
                LOG_DEBUG << "fileChangedOnDisk - call but no file monitored";
            }
        }
        else {
            LOG_DEBUG << "fileChangedOnDisk - call but no dir monitored";
        }

        return QString{};
    }

private:
    efsw::FileWatcher watcher_;
    std::vector<WatchedDirectory> watchedPaths_;
    FileWatcher* parent_;

    bool nativeWatchEnabled_ = true;

    RecursiveMutex mutex_;
};

void EfswFileWatcherDeleter::operator()( EfswFileWatcher* watcher ) const
{
    delete watcher;
}

// Runs the poll timer on its own thread (#322), so a tick's QFileInfo stats
// never stall the UI's event loop. Lives on pollThread_ for its whole life:
// created before the thread starts and moved to it, so its QTimer is also
// created on that thread and every call into it -- setPolling() included --
// is queued to run there.
class FileWatcherPollWorker final : public QObject {
    Q_OBJECT
public:
    explicit FileWatcherPollWorker( EfswFileWatcher* watcher )
        : watcher_{ watcher }
    {
    }

public Q_SLOTS:
    void setPolling( bool enabled, int intervalMs )
    {
        if ( !timer_ ) {
            timer_ = new QTimer( this );
            connect( timer_, &QTimer::timeout, this, &FileWatcherPollWorker::poll );
        }

        if ( enabled ) {
            timer_->start( intervalMs );
        }
        else {
            timer_->stop();
        }
    }

private Q_SLOTS:
    void poll()
    {
        watcher_->checkWatches();
    }

private:
    EfswFileWatcher* watcher_;
    QTimer* timer_ = nullptr;
};

FileWatcher::FileWatcher()
    : throttler_{ new KDToolBox::KDSignalThrottler( this ) }
    , efswWatcher_{ new EfswFileWatcher( this ) }
    , pollThread_{ new QThread }
    , pollWorker_{ new FileWatcherPollWorker( efswWatcher_.get() ) }
{
    throttler_->setTimeout( 250 );
    connect( this, &FileWatcher::notifyFileChangedOnDisk, throttler_,
             &KDToolBox::KDGenericSignalThrottler::throttle );
    connect( throttler_, &KDToolBox::KDGenericSignalThrottler::triggered, this,
             &FileWatcher::sendChangesNotifications );

    pollThread_->setObjectName( QStringLiteral( "FileWatcherPoll" ) );
    pollWorker_->moveToThread( pollThread_ );
    connect( pollThread_, &QThread::finished, pollWorker_, &QObject::deleteLater );
    pollThread_->start();
}

FileWatcher::~FileWatcher()
{
    pollThread_->quit();
    pollThread_->wait();
    delete pollThread_;
}

FileWatcher& FileWatcher::getFileWatcher()
{
    static auto* const instance = new FileWatcher;
    return *instance;
}

std::shared_ptr<FileWatcher> FileWatcher::sharedFileWatcher()
{
    // Never deletes: the watcher outlives every holder of this pointer.
    return std::shared_ptr<FileWatcher>( &getFileWatcher(), []( FileWatcher* ) {} );
}

void FileWatcher::addFile( const QString& fileName )
{
    efswWatcher_->addFile( fileName );
    applyWatchPolicy();
}

void FileWatcher::removeFile( const QString& fileName )
{
    efswWatcher_->removeFile( fileName );
    applyWatchPolicy();
}

void FileWatcher::fileChangedOnDisk( const QString& fileName )
{
    if ( std::find( changes_.begin(), changes_.end(), fileName ) == changes_.end() ) {
        changes_.push_back( fileName );
    }

    Q_EMIT notifyFileChangedOnDisk();
}

void FileWatcher::sendChangesNotifications()
{
    for ( const auto& fileName : changes_ ) {
        Q_EMIT fileChanged( fileName );
    }

    changes_.clear();
}

void FileWatcher::setWatchPolicy( const WatchPolicy& policy )
{
    watchPolicy_ = policy;
    applyWatchPolicy();
}

void FileWatcher::applyWatchPolicy()
{
    if ( watchPolicy_.pollingEnabled ) {
        LOG_INFO << "Polling files enabled";
    }
    else {
        LOG_INFO << "Polling files disabled";
    }

    const auto pollingEnabled = watchPolicy_.pollingEnabled;
    const auto pollIntervalMs = watchPolicy_.pollIntervalMs;

    QMetaObject::invokeMethod(
        pollWorker_,
        [ this, pollingEnabled, pollIntervalMs ]() {
            pollWorker_->setPolling( pollingEnabled, pollIntervalMs );
        },
        Qt::QueuedConnection );

    efswWatcher_->enableWatch( watchPolicy_.nativeWatchEnabled );
}

#include "filewatcher.moc"
