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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QMetaObject>

#include "archivemember.h"
#include "changed.h"
#include "log.h"
#include "quickfindpattern.h"
#include "sessioninfo.h"
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

// Whether a Log File outlives the run of the application it was opened in. A
// Transient Log File -- the spool of standard input, the file of a merged tab,
// what a data source writes, the text pasted from the clipboard -- exists only
// while the application runs, so the Session does not save it: a restart
// neither opens a file that is gone nor reports an error for it (#570).
// Whoever opens a Log File knows which it is and says so; the Session only
// keeps it.
enum class LogFileLifetime { Ordinary, Transient };

// Where a Log File open in the Session came from, as far as the Session keeps
// it beside its path: whether it is Transient, the archive and member it was
// decompressed from (#596), and the Ordinary Log File a converter plugin
// converted it from (#615). Whoever opens the Log File knows it and says so,
// and what follows from it -- saved or not, which recent file, what its tab's
// name and group are stored by -- is answered here (#643).
struct LogFileOrigin {
    LogFileLifetime lifetime = LogFileLifetime::Ordinary;
    // Empty for a Log File not decompressed from an archive.
    ArchiveMember archiveMember;
    // Empty for a Log File not converted, or converted from a Transient one.
    QString convertedFrom;
    // The archive and member the Log File it was converted from was
    // decompressed from, if it was.
    ArchiveMember convertedFromArchive;

    static LogFileOrigin transient()
    {
        LogFileOrigin origin;
        origin.lifetime = LogFileLifetime::Transient;
        return origin;
    }

    static LogFileOrigin fromArchive( const ArchiveMember& member )
    {
        LogFileOrigin origin;
        origin.archiveMember = member;
        return origin;
    }

    // What a converter plugin wrote for the Log File at `path`, which came
    // from `source`: a Transient Log File, as the temporary file it is read
    // from is gone after a restart (#605). Only one converted from an
    // Ordinary Log File is found by that one, and kept by it in the recent
    // files.
    static LogFileOrigin conversionOf( const QString& path, const LogFileOrigin& source )
    {
        auto origin = transient();
        if ( source.lifetime == LogFileLifetime::Ordinary ) {
            origin.convertedFrom = path;
            origin.convertedFromArchive = source.archiveMember;
        }
        return origin;
    }

    // Whether the Session saves it and restores it on the next start (#570).
    bool savedWithSession() const
    {
        return lifetime == LogFileLifetime::Ordinary;
    }

    // The file the recent files keep for the Log File at `fileName`: the Log
    // File itself, the archive a decompressed Log File came from (#609), the
    // one a converted Log File was converted from (#605), or none for any
    // other Transient Log File (#597).
    QString recentFile( const QString& fileName ) const
    {
        if ( lifetime == LogFileLifetime::Ordinary ) {
            // Opened from the recent files, the archive asks for its member
            // again.
            return archiveMember.isEmpty() ? fileName : archiveMember.archive;
        }
        if ( convertedFrom.isEmpty() ) {
            return {};
        }
        return convertedFromArchive.isEmpty() ? convertedFrom : convertedFromArchive.archive;
    }

    // What the name and group of the tab of the Log File at `fileName` are
    // stored by: its path, or its archive and member for one decompressed
    // from an archive (#609). None for a Transient Log File: its path is gone
    // after a restart, so its tab's name and group last until the tab closes
    // (#597).
    QString storedKey( const QString& fileName ) const
    {
        if ( lifetime == LogFileLifetime::Transient ) {
            return {};
        }
        return archiveMember.isEmpty() ? fileName : archiveMember.key();
    }

    bool operator==( const LogFileOrigin& ) const = default;
};

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

    // Brings the tab showing these views to the front, and the window with
    // it, if the window has one; returns whether it has (#642).
    virtual bool showView( const ViewInterface* view ) = 0;

    // The Value Names Collection changed, in this window or another: show
    // its groups and checks again (#647).
    virtual void applyValueNamesChange() {}

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

    // The views showing the Log File at `fileName` in any window: those of
    // the Log File open by that path, else those of the Log File a converter
    // plugin converted it into (#615). Null while neither is open. Only an
    // Ordinary Log File is found by what it was converted into: a Transient
    // one is a temporary file written anew each time it is fetched, so the
    // same path is never asked for again.
    ViewInterface* viewShowing( const QString& fileName ) const;

    // Brings the tab showing the Log File at `fileName`, as viewShowing()
    // finds it, to the front of whichever window shows it (#642). Returns
    // whether the Log File is showing.
    bool showOpen( const QString& fileName ) const;

    // Where the Log File of these views came from; an Ordinary Log File's
    // origin for views not open here.
    LogFileOrigin originOf( const ViewInterface* view ) const;

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
    //
    // A Transient Log File is open like any other, but is never saved with the
    // Session (see WindowSession::save()). A Log File decompressed from an
    // archive is saved with the archive and its member, not with `fileName`,
    // the temporary file it is read from (#596). A converted one is found by
    // the Log File it was converted from (see viewShowing()).
    enum class Loading { Now, Queued };
    ViewInterface* open( const QString& fileName, const ViewFactory& viewFactory,
                         const QString& viewContext = {}, Loading loading = Loading::Now,
                         const LogFileOrigin& origin = {} );

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

    // The Regex Lab matches with the engine a Search runs on (#659).
    const SearchPolicy& searchPolicy() const
    {
        return policies_.search;
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
    //
    // Changed::ValueNames tells every open Log File that the Value Names
    // Collection changed. Each view showing Value Names reads its Log Lines
    // again; nothing is re-derived and no window is told.
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
        // A Transient Log File is not saved with the Session; a decompressed
        // one is saved with its archive and member.
        LogFileOrigin origin;
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
    void applyValueNamesChange();

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
// Decompresses a saved archive member again and returns the file to open, or
// an empty string to leave its tab out (#596).
using ArchiveMemberDecompressor = std::function<QString( const ArchiveMember& )>;
// A view and its view state, which holds where it stands (#559).
using SaveFileInfo = std::tuple<const ViewInterface*, std::shared_ptr<const ViewContextInterface>>;

// What a window's Session is: its Log Files in tab order, each with its view
// state, and which of them was in front. The automatic Session saves and
// restores a window through it, and so does a Session File (#576), so that
// the two cannot drift apart.
struct WindowSnapshot {
    // In tab order. A Transient Log File is never in it (#570); one
    // decompressed from an archive is, with its archive and member (#596).
    std::vector<SessionInfo::OpenFile> files;
    // The index in `files` of the Log File whose tab was in front, -1 for
    // none.
    int currentFile = -1;

    // What the tabs of these Log Files are named and grouped in. The
    // automatic Session leaves these empty: tab names and groups live in
    // their own stores, keyed by each Log File's path or archive key (#609).
    // A Session File carries them, as the tabs had them when it was saved.
    struct Tab {
        // The custom tab name; empty for none.
        QString name;
        // The name of its tab group; empty for none.
        QString group;
        bool operator==( const Tab& ) const = default;
    };
    // Parallel to `files` when taken, else empty.
    std::vector<Tab> tabs;

    struct Group {
        QString name;
        QColor color;
        bool operator==( const Group& ) const = default;
    };
    // The groups some tab is in, each once.
    std::vector<Group> groups;

    bool operator==( const WindowSnapshot& ) const = default;
};

class WindowSession {
public:
    WindowSession( std::shared_ptr<Session> appSession, const QString& id, size_t index );

    ViewInterface* getViewIfOpen( const QString& file_name ) const
    {
        return appSession_->getViewIfOpen( file_name );
    }

    // See the Session's own.
    bool showOpen( const QString& fileName ) const
    {
        return appSession_->showOpen( fileName );
    }

    LogFileOrigin originOf( const ViewInterface* view ) const
    {
        return appSession_->originOf( view );
    }

    // Opens a Log File in this window, restoring the view context saved for
    // it in any window of the stored Session, the way restore() does. A
    // Transient Log File was never saved, so it has none to restore, and it is
    // left out whenever this window is saved (#570). A Log File decompressed
    // from an archive is saved with its archive and member (#596).
    ViewInterface* open( const QString& fileName, const ViewFactory& viewFactory,
                         const LogFileOrigin& origin = {} );

    void close( const ViewInterface* view )
    {
        auto it = std::find( openedFiles_.begin(), openedFiles_.end(), getFilename( view ) );
        if ( it != openedFiles_.end() ) {
            openedFiles_.erase( it );
        }

        // A closed tab no longer places a deferred one, nor holds the front.
        for ( auto& tab : restoredTabs_ ) {
            if ( tab.view == view ) {
                tab.view = nullptr;
            }
        }
        if ( restoredFront_ == view ) {
            restoredFront_ = nullptr;
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

    const SearchPolicy& searchPolicy() const
    {
        return appSession_->searchPolicy();
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
    // current file (or -1 if none): the one whose tab was in front when the
    // Session was saved, else the last one (#542).
    //
    // Only the current file starts loading; the others are queued and load
    // one after another once it has loaded, unless startLoading() is called
    // for one first (#300).
    //
    // A Log File saved with the archive it was decompressed from is handed
    // to `decompressor`, and the file it returns is opened in its place, with
    // the view state saved for it. When it returns none -- the archive is
    // gone -- or there is no decompressor, its tab is left out without an
    // error, and the current file is counted without it (#596).
    //
    // Given `deferred`, it is not decompressed here: its tab is left out as
    // above, and it is added to `deferred` to be decompressed after the
    // restore returns, then opened with openDeferred() or left out for good
    // with dropDeferred() (#610). Until then it keeps its place in the saved
    // window: save() saves it where it stood among the tabs that remain.
    struct DeferredArchiveFile {
        int id;
        ArchiveMember archiveMember;
    };
    OpenedFilesList restore( const ViewFactory& viewFactory, int* currentFileIndex,
                             const ArchiveMemberDecompressor& decompressor = {},
                             std::vector<DeferredArchiveFile>* deferred = nullptr );

    // The same for the Log Files of `snapshot` rather than the stored
    // Session's: what restore() above does with the window stored for this
    // one, and what opening a Session File does in a new window (#576). Its
    // tab names and groups are not touched here.
    OpenedFilesList restore( const WindowSnapshot& snapshot, const ViewFactory& viewFactory,
                             int* currentFileIndex,
                             const ArchiveMemberDecompressor& decompressor = {},
                             std::vector<DeferredArchiveFile>* deferred = nullptr );

    // This window as the stored Session holds it.
    WindowSnapshot storedSnapshot() const;

    // Whether this Log File is open in any window of the application: by its
    // path, or, decompressed from an archive, by its archive and member,
    // whatever temporary file it was decompressed to.
    bool isOpen( const SessionInfo::OpenFile& file ) const;

    // Opens `fileName`, decompressed for the deferred Log File `id`, with the
    // view state saved for it, as restore() would have. `tabs` are the views
    // of the window's Log Files in tab order, and `currentView` the ones in
    // front, if any.
    //
    // Its tab goes at `position` among `tabs`: before the first of the tabs
    // that came after it in the saved window and are still open, else after
    // the last of those before it, else last. It is `inFront` when it was the
    // tab in front and the window still shows the tab the restore put in
    // front, or it is the window's only tab; else its Log File waits in the
    // queue for its first load, like every restored Log File but the current
    // one (#300). A tab that arrives never takes the front from one the user
    // chose.
    struct DeferredOpen {
        ViewInterface* view = nullptr;
        size_t position = 0;
        bool inFront = false;
    };
    DeferredOpen openDeferred( int id, const QString& fileName, const ViewFactory& viewFactory,
                               const std::vector<const ViewInterface*>& tabs,
                               const ViewInterface* currentView );

    // The deferred Log File `id` cannot be decompressed: its tab is left out
    // without an error, as restore() leaves one out, and is no longer saved.
    void dropDeferred( int id );

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
    // (view, ViewContextInterface) is passed, this is because only
    // the main window know the order in which the views are presented to
    // the user (it might have changed since file were opened).
    // The views of the tab in front are saved as the current ones, so that a
    // restore opens on that tab (#542); null when no Log File's tab is.
    // A Transient Log File is left out (#570); when its tab is in front, no
    // tab is saved as the one in front, and a restore opens on the last one.
    // Also, the geometry information is passed as an opaque string, and the
    // width of the sidebar beside it (0 when there is none to keep).
    void save( const std::vector<SaveFileInfo>& view_list, const ViewInterface* currentView,
               const QByteArray& geometry, int sidebarWidth );

    // What save() saves of the Log Files, without saving it: the window's
    // snapshot, as a Session File writes it too (#576). Its tab names and
    // groups are not taken here.
    WindowSnapshot snapshot( const std::vector<SaveFileInfo>& view_list,
                             const ViewInterface* currentView ) const;

    // returns true if caller needs to save settings
    bool close();

private:
    std::shared_ptr<Session> appSession_;
    QString windowId_;
    size_t windowIndex_;

    std::vector<QString> openedFiles_;

    // The tabs of the saved window a restore left deferred, and the ones
    // around them, in saved order, for as long as one is deferred (#610).
    struct RestoredTab {
        // Which restore it was: the tabs of one are placed among each other.
        int run = 0;
        // Null while deferred, or once the tab is closed.
        const ViewInterface* view = nullptr;
        struct Deferred {
            int id;
            // As the Session saved it.
            QString fileName;
            QString viewContext;
            ArchiveMember archiveMember;
            bool inFront;
        };
        std::optional<Deferred> deferred;
    };
    std::vector<RestoredTab> restoredTabs_;
    int restoreRuns_ = 0;
    int nextDeferredId_ = 0;
    // The views a restore put in front, or null for none.
    const ViewInterface* restoredFront_ = nullptr;

    // Where the restored tab at `slot` goes among `tabs`.
    size_t positionAmong( size_t slot, const std::vector<const ViewInterface*>& tabs ) const;
    // Forgets the tabs of a restore none of whose tabs is deferred any longer.
    void forgetFinishedRestores();
};

#endif
