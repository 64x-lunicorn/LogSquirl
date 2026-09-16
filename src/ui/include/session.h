/*
 * Copyright (C) 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SESSION_H
#define SESSION_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDateTime>

#include "log.h"
#include "quickfindpattern.h"
#include "settingspolicies.h"

class FileWatchPort;
class ViewInterface;
class ViewContextInterface;
class LogFormatCatalog;
class OpenLogFile;
class SavedSearches;

// File unreadable error
class FileUnreadableErr {};

// The session is responsible for maintaining the list of open log files
// and their association with Views.
// It also maintains the domain objects which are common to all log files
// (SavedSearches, FileHistory, QFPattern...)

class WindowSession;

class Session : public std::enable_shared_from_this<Session> {
public:
    // The Policies the application derived. The Session does not consume
    // them itself: it is the place that builds a Log File's data objects,
    // so it is the place that has to hand each one what it is allowed to
    // know about the settings.
    //
    // The Log Format Catalog is the application's one Catalog, already
    // built. Every view is handed this same instance, and the Session
    // rebuilds it whenever settings are applied.
    //
    // The File Watch Port is how every Log File it opens hears of changes on
    // disk; each Open Log File is handed it when it is built. Without one no
    // Log File is followed on disk, which is what a test that does not care
    // wants. The Watch Policy is not the Session's to hand to it: whoever
    // built the watcher does that.
    Session( const SettingsPolicies& policies, std::shared_ptr<LogFormatCatalog> logFormatCatalog,
             std::shared_ptr<FileWatchPort> fileWatch = {} );
    ~Session();

    // No copy/assignment please
    Session( const Session& ) = delete;
    Session& operator=( const Session& ) = delete;

    // Return the view associated to a file if it is open
    // The filename must be strictly identical to trigger a match
    // (no match in case of e.g. relative vs. absolute pathname.
    ViewInterface* getViewIfOpen( const QString& file_name ) const;

    // Open a new file, starts its asynchronous loading, and construct a new
    // view for it (the caller passes a factory to build the concrete view)
    // The ownership of the view is given to the caller
    // Throw exceptions if the file is already open or if it cannot be open.
    ViewInterface* open( const QString& file_name,
                         const std::function<ViewInterface*()>& view_factory );

    // Close the file identified by the view passed
    // Throw an exception if it does not exist.
    void close( const ViewInterface* view );

    // Get the file name for the passed view.
    QString getFilename( const ViewInterface* view ) const;

    // Get the size (in bytes) and number of lines in the current file.
    // The file is identified by the view attached to it.
    void getFileInfo( const ViewInterface* view, uint64_t* fileSize, uint64_t* fileNbLine,
                      QDateTime* lastModified ) const;

    // Get a (non-const) reference to the QuickFind pattern.
    std::shared_ptr<QuickFindPattern> quickFindPattern() const
    {
        return quickFindPattern_;
    }

    SavedSearches& savedSearches() const
    {
        return *savedSearches_;
    }

    // The application's Log Format Catalog, the one every view is handed.
    std::shared_ptr<const LogFormatCatalog> logFormatCatalog() const
    {
        return logFormatCatalog_;
    }

    // The axes a window consumes, read at the point of use.
    //
    // A window is not a Log File: it outlives every one of them, and its
    // menus and actions have to answer for whatever is configured now --
    // whether the follow action is enabled, whether an archive is extracted,
    // where the Index Cache keeps its files. So it asks the Session, which
    // already stores the Policies and has them replaced on every settings
    // change, instead of keeping a snapshot of its own that would have to be
    // refreshed in step. Only the axes a window actually consumes are exposed,
    // so it still cannot reach a setting it did not declare.
    //
    // The QuickFind Policy is among them because the QuickFind bar and the mux
    // that dispatches QuickFind belong to the window, not to a Log File: the
    // Policy is the same whichever tab or Filtered View is in front.
    const WatchPolicy& watchPolicy() const
    {
        return policies_.watch;
    }

    const FileAccessPolicy& fileAccessPolicy() const
    {
        return policies_.fileAccess;
    }

    const IndexingPolicy& indexingPolicy() const
    {
        return policies_.indexing;
    }

    const QuickFindPolicy& quickFindPolicy() const
    {
        return policies_.quickFind;
    }

    // Takes the Policies re-derived after a settings change: stores them
    // for the Log Files opened from now on, and hands the axes that
    // actually changed to the Log Files already open -- every one of them,
    // not only the one the active tab is showing. An axis that did not
    // change is not handed to anybody, so changing (say) a Highlighter Set
    // does not make every open file rebuild its Context Lines.
    //
    // The Log Format Catalog is rebuilt on every call, whether or not any
    // Policy changed: a user's Log Format can change on disk without any
    // setting changing, and applying the settings is how it is picked up.
    // Open Log Files keep the Log Format they were recognized with.
    void applyPolicies( const SettingsPolicies& policies );

    // Tells every open Log File that the Highlighter Set Collection changed:
    // a Highlighter Set was edited, imported, activated or deactivated, or a
    // Color Label was given another color. Highlighter Sets are not a
    // Setting, so this is not a Policy, but it reaches the same Log Files:
    // every one open, in every window, not only the one the current tab
    // shows. Each re-reads the colors of its Color Labels and repaints.
    void applyHighlighterSetChange();

    std::vector<WindowSession> windowSessions();

    bool exitRequested() const
    {
        return exitRequested_;
    }

    void setExitRequested( bool isRequested )
    {
        exitRequested_ = isRequested;
    }

private:
    struct OpenFile {
        QString fileName;
        std::shared_ptr<OpenLogFile> openLogFile;
        ViewInterface* view;
    };

    // Open a file without checking if it is existing/readable
    ViewInterface* openAlways( const QString& file_name,
                               const std::function<ViewInterface*()>& view_factory,
                               const QString& view_context );

    // Find an open file from its associated view
    OpenFile* findOpenFileFromView( const ViewInterface* view );
    const OpenFile* findOpenFileFromView( const ViewInterface* view ) const;

    // List of open files
    typedef std::unordered_map<const ViewInterface*, OpenFile> OpenFileMap;
    OpenFileMap openFiles_;

    // Global search history
    SavedSearches* savedSearches_;

    // Global quickfind pattern
    std::shared_ptr<QuickFindPattern> quickFindPattern_;

    // Handed to every Log File opened from now on.
    SettingsPolicies policies_;

    // Handed to every view, for Format Recognition.
    std::shared_ptr<LogFormatCatalog> logFormatCatalog_;

    // Handed to every Open Log File.
    std::shared_ptr<FileWatchPort> fileWatch_;

    bool exitRequested_ = false;

    friend class WindowSession;
};

using OpenedFilesList = std::vector<std::pair<QString, ViewInterface*>>;
using SaveFileInfo
    = std::tuple<const ViewInterface*, uint64_t, std::shared_ptr<const ViewContextInterface>>;

class WindowSession {
public:
    WindowSession( std::shared_ptr<Session> appSession, const QString& id, size_t index );

    ViewInterface* getViewIfOpen( const QString& file_name ) const
    {
        return appSession_->getViewIfOpen( file_name );
    }

    ViewInterface* open( const QString& file_name,
                         const std::function<ViewInterface*()>& view_factory )
    {
        openedFiles_.push_back( file_name );
        return appSession_->open( file_name, view_factory );
    }

    void close( const ViewInterface* view )
    {
        auto it = std::find( openedFiles_.begin(), openedFiles_.end(), getFilename( view ) );
        if ( it != openedFiles_.end() ) {
            openedFiles_.erase( it );
        }

        appSession_->close( view );
    }

    QString getFilename( const ViewInterface* view ) const
    {
        return appSession_->getFilename( view );
    }

    void getFileInfo( const ViewInterface* view, uint64_t* fileSize, uint64_t* fileNbLine,
                      QDateTime* lastModified ) const
    {
        return appSession_->getFileInfo( view, fileSize, fileNbLine, lastModified );
    }

    std::vector<QString> openedFiles() const
    {
        return openedFiles_;
    }

    // Get a (non-const) reference to the QuickFind pattern.
    std::shared_ptr<QuickFindPattern> getQuickFindPattern() const
    {
        return appSession_->quickFindPattern();
    }

    // The application's Log Format Catalog.
    std::shared_ptr<const LogFormatCatalog> logFormatCatalog() const
    {
        return appSession_->logFormatCatalog();
    }

    // The axes the window this session belongs to consumes. See the
    // Session's own accessors for why a window asks rather than holds.
    const WatchPolicy& watchPolicy() const
    {
        return appSession_->watchPolicy();
    }

    const FileAccessPolicy& fileAccessPolicy() const
    {
        return appSession_->fileAccessPolicy();
    }

    const IndexingPolicy& indexingPolicy() const
    {
        return appSession_->indexingPolicy();
    }

    const QuickFindPolicy& quickFindPolicy() const
    {
        return appSession_->quickFindPolicy();
    }

    // A Highlighter Set change reaches every open Log File of the
    // application, not only this window's. See the Session's own.
    void applyHighlighterSetChange()
    {
        appSession_->applyHighlighterSetChange();
    }

    QString windowId() const
    {
        return windowId_;
    }

    size_t windowIndex() const
    {
        return windowIndex_;
    }

    // Open all the files listed in the stored session
    // (see ::open)
    // returns a vector of pairs (file_name, view) and the index of the
    // current file (or -1 if none).
    OpenedFilesList restore( const std::function<ViewInterface*()>& view_factory,
                             int* current_file_index );

    // Get the geometry string from persistent storage for this session.
    void restoreGeometry( QByteArray* geometry ) const;

    // Save the session to persistent storage. An ordered list of
    // (view, topline, ViewContextInterface) is passed, this is because only
    // the main window know the order in which the views are presented to
    // the user (it might have changed since file were opened).
    // Also, the geometry information is passed as an opaque string.
    void save( const std::vector<SaveFileInfo>& view_list, const QByteArray& geometry );

    // returns true if caller needs to save settings
    bool close();

private:
    std::shared_ptr<Session> appSession_;
    QString windowId_;
    size_t windowIndex_;

    std::vector<QString> openedFiles_;
};

#endif
