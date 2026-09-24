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

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QMetaObject>

#include "changed.h"
#include "log.h"
#include "quickfindpattern.h"
#include "settingspolicies.h"
#include "viewinterface.h"

class PolicyFileWatchPort;
class LogFormatCatalog;
class OpenLogFile;
class SavedSearches;
class TeamFolder;

// File unreadable error
class FileUnreadableErr {};

// The session is responsible for maintaining the list of open log files
// and their association with Views.
// It also maintains the domain objects which are common to all log files
// (SavedSearches, FileHistory, QFPattern...)

class WindowSession;

// A window showing Log Files of the Session. A window outlives every Log File
// it shows, and some of what it shows answers to the settings on its own --
// its QuickFind bar, its menus and actions, its shortcuts -- so the Session
// tells every window of a settings change, once the Policies it asks for have
// been re-derived.
class SessionWindow {
public:
    // The settings changed and the Session holds the Policies re-derived from
    // them: take what the window shows from them again.
    virtual void applySettingsChange() = 0;

protected:
    SessionWindow() = default;
    ~SessionWindow() = default;
    SessionWindow( const SessionWindow& ) = default;
    SessionWindow& operator=( const SessionWindow& ) = default;
};

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
    // The file watcher is how every Log File it opens hears of changes on
    // disk; each Open Log File is handed it, as its File Watch Port, when it
    // is built. The watcher itself is handed the Watch Policy here, before any
    // file is added to it, and again whenever a settings change alters it.
    // Without one no Log File is followed on disk, which is what a test that
    // does not care wants.
    //
    // The Team Folder is the application's one, handed the Team Folder Policy
    // here, which sets it up, and again whenever a settings change alters it.
    // Without one there are no Team groups.
    Session( const SettingsPolicies& policies, std::shared_ptr<LogFormatCatalog> logFormatCatalog,
             std::shared_ptr<PolicyFileWatchPort> fileWatch = {},
             std::shared_ptr<TeamFolder> teamFolder = {} );
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
    //
    // The factory is called once, with everything the views are built from
    // (#248): the Open Log File, the QuickFind pattern, the Policies, the
    // saved Searches, whom to report a change to and, when one is given, the
    // view context to restore. Opening a file and restoring a Session both
    // come here.
    //
    // A Log File opened with Loading::Queued is not loaded yet: it waits for
    // its turn behind the Log Files queued before it, and starts loading once
    // no Log File the Session opened is loading any longer -- at once, if none
    // is. Restoring a Session queues every Log File but the current tab's, so
    // that they do not compete with it for the disk and the threads (#300).
    // Its views are built all the same, and what they are asked before it
    // loads -- a Search, the Marks saved with the Session -- waits for that
    // load, as it waits for any first load.
    enum class Loading { Now, Queued };
    ViewInterface* open( const QString& fileName, const ViewFactory& viewFactory,
                         const QString& viewContext = {}, Loading loading = Loading::Now );

    // Starts loading the Log File of these views now if it is still queued,
    // ahead of the Log Files queued before it: its tab was activated. Does
    // nothing for a Log File that is loading or has loaded.
    //
    // A Log File asked to reload before it was ever loaded comes here too,
    // through OpenLogFile::loadRequested(): reloading a tab that has not
    // loaded yet loads it, and it takes its turn no differently than an
    // activated tab -- no Log File still queued behind it starts with it
    // (#332).
    void startLoading( const ViewInterface* view );

    // Whether the Log File of these views is still waiting in the queue for
    // its first load, as a restored tab that is not the current one does
    // until its turn comes or its tab is activated (#300). False for a Log
    // File that is loading, has loaded, or is not open here.
    bool isLoadQueued( const ViewInterface* view ) const;

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

    // The application's Team Folder; null when there is none, as in a test.
    std::shared_ptr<TeamFolder> teamFolder() const
    {
        return teamFolder_;
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

    // The one entry for every change of settings or coloring (#245). A writer
    // says what it changed, and nothing else, in whatever order it likes; the
    // Session works out what follows and who has to hear of it -- every open
    // Log File, in every window, not only the one the current tab shows.
    //
    // Changed::Settings re-derives the Policies from the settings store and
    // applies them (applyPolicies() below), tells every open Log File to read
    // its font and shortcuts again -- they have no Policy -- and then tells
    // every window, which by then reads the re-derived Policies.
    //
    // Changed::Font tells every open Log File to read the font again, and
    // nothing else: no Policy is re-derived, the Log Format Catalog is not
    // rebuilt and no window is told. A zoom is all it is.
    //
    // Changed::HighlighterSets tells every open Log File that the Highlighter
    // Set Collection changed. Each re-reads the colors of its Color Labels and
    // repaints; nothing is re-derived and no window is told.
    void applyChange( Changed change );

    // The windows told of a settings change. A window adds itself when it is
    // built and removes itself before it is destroyed.
    void addWindow( SessionWindow* window );
    void removeWindow( SessionWindow* window );

    // Takes the Policies re-derived after a settings change: stores them
    // for the Log Files opened from now on, and hands the axes that
    // actually changed to the Log Files already open -- every one of them,
    // not only the one the active tab is showing -- and a changed Watch
    // Policy to the file watcher. An axis that did not change is not handed
    // to anybody, so changing (say) a Highlighter Set does not make every
    // open file rebuild its Context Lines, nor restart file watching.
    //
    // applyChange( Changed::Settings ) comes here with what it re-derived;
    // a test that has no settings store to write hands Policies here itself.
    //
    // The Log Format Catalog is rebuilt on every call, whether or not any
    // Policy changed: a user's Log Format can change on disk without any
    // setting changing, and applying the settings is how it is picked up.
    // Open Log Files keep the Log Format they were recognized with.
    void applyPolicies( const SettingsPolicies& policies );

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
    // Where the first load of an open Log File stands. Only the first load
    // counts: a Log File growing or reloaded later holds nobody up.
    enum class FirstLoad { Queued, Loading, Finished };

    struct OpenFile {
        QString fileName;
        std::shared_ptr<OpenLogFile> openLogFile;
        ViewInterface* view;
        FirstLoad firstLoad = FirstLoad::Queued;
        // Hears of the end of the first load; disconnected once it did.
        QMetaObject::Connection firstLoadFinished;
        // Hears a Log File with none attached yet ask to be loaded, because
        // it was reloaded (#332); held for as long as this entry is.
        QMetaObject::Connection loadRequested;
    };

    // Starts the first load of an open Log File.
    void startFirstLoad( OpenFile& file );
    // The first load of an open Log File finished.
    void finishFirstLoad( const ViewInterface* view );
    // Starts the Log File first in the queue, if no first load is running
    // and the queue is not held.
    void startNextQueuedLoad();

    void applySettingsChange();
    void applyFontChange();
    void applyHighlighterSetChange();

    // Applies the Policies as applyPolicies() does, and hands every open Log
    // File what changed together with `change`, in one call each -- nothing
    // when nothing did.
    void applyPolicies( const SettingsPolicies& policies, ViewChange change );

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

    // Handed to every Open Log File, and handed the Watch Policy.
    std::shared_ptr<PolicyFileWatchPort> fileWatch_;

    // Handed the Team Folder Policy.
    std::shared_ptr<TeamFolder> teamFolder_;

    // Told of every settings change.
    std::vector<SessionWindow*> windows_;

    // The views of the Log Files opened with Loading::Queued that have not
    // started loading, in the order they were opened.
    std::deque<const ViewInterface*> queuedLoads_;
    // While a window restores its Log Files, none of them starts loading from
    // the queue: the current tab's has to start first, whatever its place in
    // the window.
    int queueHolds_ = 0;

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

    // Opens a Log File in this window, restoring the view context saved for
    // it in any window of the stored Session, the way restore() does.
    ViewInterface* open( const QString& fileName, const ViewFactory& viewFactory );

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

    // The application's Team Folder, or null.
    std::shared_ptr<TeamFolder> teamFolder() const
    {
        return appSession_->teamFolder();
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

    // A change reaches every open Log File of the application, and every
    // window, not only this window's. See the Session's own.
    void applyChange( Changed change )
    {
        appSession_->applyChange( change );
    }

    void addWindow( SessionWindow* window )
    {
        appSession_->addWindow( window );
    }

    void removeWindow( SessionWindow* window )
    {
        appSession_->removeWindow( window );
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
    //
    // Only the current file starts loading; the others are queued and load
    // one after another once it has loaded, unless startLoading() is called
    // for one first (#300).
    OpenedFilesList restore( const ViewFactory& viewFactory, int* currentFileIndex );

    // Starts loading a restored Log File that is still queued, now: its tab
    // was activated. See the Session's own.
    void startLoading( const ViewInterface* view )
    {
        appSession_->startLoading( view );
    }

    // Get the geometry string from persistent storage for this session.
    void restoreGeometry( QByteArray* geometry ) const;

    // The width the sidebar was left at in this window, from persistent
    // storage; 0 when none was saved.
    int sidebarWidth() const;

    // Save the session to persistent storage. An ordered list of
    // (view, topline, ViewContextInterface) is passed, this is because only
    // the main window know the order in which the views are presented to
    // the user (it might have changed since file were opened).
    // Also, the geometry information is passed as an opaque string, and the
    // width of the sidebar beside it (0 when there is none to keep).
    void save( const std::vector<SaveFileInfo>& view_list, const QByteArray& geometry,
               int sidebarWidth );

    // returns true if caller needs to save settings
    bool close();

private:
    std::shared_ptr<Session> appSession_;
    QString windowId_;
    size_t windowIndex_;

    std::vector<QString> openedFiles_;
};

#endif
