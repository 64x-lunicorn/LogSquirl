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

#include "session.h"

#include "log.h"

#include <algorithm>
#include <cassert>

#include "configuration.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
#include "policyfilewatchport.h"
#include "savedsearches.h"
#include "sessioninfo.h"
#include "viewinterface.h"

Session::Session( const SettingsPolicies& policies,
                  std::shared_ptr<LogFormatCatalog> logFormatCatalog,
                  std::shared_ptr<PolicyFileWatchPort> fileWatch )
    : policies_( policies )
    , logFormatCatalog_( std::move( logFormatCatalog ) )
    , fileWatch_( std::move( fileWatch ) )
{
    // Before any Log File is opened, and so before any file is added to it.
    if ( fileWatch_ ) {
        fileWatch_->setWatchPolicy( policies_.watch );
    }

    // Get the global search history (it remains the property
    // of the Persistent)
    savedSearches_ = &SavedSearches::getSynced();
    // Read once, at startup: restoring and opening Log Files afterwards read
    // the in-memory Session info (#301).
    SessionInfo::getSynced();

    quickFindPattern_ = std::make_shared<QuickFindPattern>();
}

Session::~Session()
{
    // FIXME Clean up all the data objects...
}

ViewInterface* Session::getViewIfOpen( const QString& file_name ) const
{
    auto result = std::find_if( openFiles_.begin(), openFiles_.end(),
                                [ & ]( const std::pair<const ViewInterface*, OpenFile>& o ) {
                                    return ( o.second.fileName == file_name );
                                } );

    if ( result != openFiles_.end() )
        return result->second.view;
    else
        return nullptr;
}

ViewInterface* Session::open( const QString& fileName, const ViewFactory& viewFactory,
                              const QString& viewContext )
{
    // The Open Log File: the log data, its Searches, and what they do as the
    // Log File changes on disk
    auto openLogFile = std::make_shared<OpenLogFile>(
        policies_.indexing, policies_.search, policies_.fileAccess, policies_.decoding,
        policies_.recognition, logFormatCatalog_, fileWatch_ );

    // One value, one call: the views need nothing else before they can show
    // the Log File, and nothing arrives in an order they depend on.
    ViewInterface* view = viewFactory( ViewBuild{
        .openLogFile = openLogFile,
        .quickFindPattern = quickFindPattern_,
        .policies = policies_,
        .savedSearches = savedSearches_,
        .viewContext = viewContext,
        // What the views change themselves -- a Highlighter Set ticked in
        // their menu, a zoom -- comes back here, to reach every open Log File.
        .changeReport = [ this ]( Changed change ) { applyChange( change ); },
    } );

    // Insert in the hash
    openFiles_.insert( { view, { fileName, openLogFile, view } } );

    // Start loading the file
    openLogFile->open( fileName );

    return view;
}

void Session::close( const ViewInterface* view )
{
    const auto it = openFiles_.find( view );
    if ( it != openFiles_.end() ) {
        openFiles_.erase( it );
    }
    else {
        LOG_WARNING << "Session::close: view not found in open files";
    }
}

QString Session::getFilename( const ViewInterface* view ) const
{
    const OpenFile* file = findOpenFileFromView( view );

    if ( !file ) {
        LOG_WARNING << "getFilename: view not found in open files";
        return {};
    }

    return file->fileName;
}

void Session::getFileInfo( const ViewInterface* view, uint64_t* fileSize, uint64_t* fileNbLine,
                           QDateTime* lastModified ) const
{
    const OpenFile* file = findOpenFileFromView( view );

    if ( !file ) {
        LOG_WARNING << "getFileInfo: view not found in open files";
        return;
    }

    const auto& logData = file->openLogFile->logData();
    *fileSize = static_cast<uint64_t>( logData->getFileSize() );
    *fileNbLine = logData->getNbLine().get();
    *lastModified = logData->getLastModifiedDate();
}

Session::OpenFile* Session::findOpenFileFromView( const ViewInterface* view )
{
    assert( view );

    OpenFile* file = &( openFiles_.at( view ) );

    // OpenfileMap::at might throw out_of_range but since a view MUST always
    // be attached to a file, we don't handle it!

    return file;
}

const Session::OpenFile* Session::findOpenFileFromView( const ViewInterface* view ) const
{
    assert( view );

    const OpenFile* file = &( openFiles_.at( view ) );

    // OpenfileMap::at might throw out_of_range but since a view MUST always
    // be attached to a file, we don't handle it!

    return file;
}

void Session::applyChange( Changed change )
{
    switch ( change ) {
    case Changed::Settings:
        applySettingsChange();
        break;
    case Changed::Font:
        applyFontChange();
        break;
    case Changed::HighlighterSets:
        applyHighlighterSetChange();
        break;
    }
}

void Session::addWindow( SessionWindow* window )
{
    if ( std::find( windows_.begin(), windows_.end(), window ) == windows_.end() ) {
        windows_.push_back( window );
    }
}

void Session::removeWindow( SessionWindow* window )
{
    windows_.erase( std::remove( windows_.begin(), windows_.end(), window ), windows_.end() );
}

void Session::applySettingsChange()
{
    // The Session is where the Policies are handed out, so it is where they
    // are re-derived: no writer, and not the application, has to do it first.
    //
    // The font and the shortcuts have no Policy and no diff: every open Log
    // File reads them again, in the same change as the Axes that changed.
    ViewChange reread;
    reread.rereadSettingsWithoutPolicy = true;
    applyPolicies( deriveSettingsPolicies( Configuration::get() ), reread );

    // Last, so that a window reads the Policies already re-derived.
    for ( auto* window : windows_ ) {
        window->applySettingsChange();
    }
}

void Session::applyPolicies( const SettingsPolicies& policies )
{
    applyPolicies( policies, ViewChange{} );
}

void Session::applyPolicies( const SettingsPolicies& policies, ViewChange change )
{
    const auto indexingChanged = policies.indexing != policies_.indexing;
    const auto searchChanged = policies.search != policies_.search;
    const auto recognitionChanged = policies.recognition != policies_.recognition;
    const auto decodingChanged = policies.decoding != policies_.decoding;
    const auto watchChanged = policies.watch != policies_.watch;

    // The Axes the views hold. One that did not change is left empty, and so
    // is not handed to anybody.
    if ( policies.decoration != policies_.decoration ) {
        // Re-colors the views of every open Log File, not only the one the
        // active tab shows, and without a new Search.
        change.decoration = policies.decoration;
    }
    if ( policies.presentation != policies_.presentation ) {
        change.presentation = policies.presentation;
    }
    if ( policies.quickFind != policies_.quickFind ) {
        // The views' own use of it: how the Search line reads its pattern and
        // what the Table View hands a QuickFind. The window's QuickFind bar
        // and mux read it from the Session instead.
        change.quickFind = policies.quickFind;
    }
    if ( watchChanged ) {
        // Takes following away from the views of a Log File that is already
        // open, or gives it back, without it being reopened.
        change.watch = policies.watch;
    }

    // Every time, changed Policies or not: the user's Log Formats are read
    // again. Log Formats handed out before stay valid for whoever holds them.
    if ( logFormatCatalog_ ) {
        logFormatCatalog_->rebuild();
    }

    // Stored whether or not anything is open: the File Access Policy in
    // particular reaches a Log File only when one is built, so this is the
    // only thing a change to it can do.
    policies_ = policies;

    if ( watchChanged && fileWatch_ ) {
        // Once for the whole application: the watcher is shared by every
        // Log File, so changing the poll interval restarts nothing else.
        fileWatch_->setWatchPolicy( policies_.watch );
    }

    for ( auto& [ view, openFile ] : openFiles_ ) {
        Q_UNUSED( view );

        if ( recognitionChanged ) {
            // Takes effect at the Log File's next Format Recognition; an
            // open Table View is not torn down.
            openFile.openLogFile->setRecognitionPolicy( policies_.recognition );
        }

        if ( indexingChanged ) {
            openFile.openLogFile->logData()->setIndexingPolicy( policies_.indexing );
        }

        if ( searchChanged ) {
            // The Log File hands it on to every LogFilteredData built from
            // it, which is more than the one this Session holds: a tab that
            // kept an earlier Search has its own.
            openFile.openLogFile->logData()->setSearchPolicy( policies_.search );
        }

        if ( decodingChanged ) {
            // Log Lines read from now on are decoded under it, and the Log
            // File tells its views to read what they show again. Search
            // results already found stay as they were.
            openFile.openLogFile->logData()->setDecodingPolicy( policies_.decoding );
        }

        if ( !change.isEmpty() ) {
            // Every open Log File, not only the one the active tab shows.
            openFile.view->applyChange( change );
        }
    }
}

void Session::applyFontChange()
{
    ViewChange change;
    change.font = true;
    for ( auto& [ view, openFile ] : openFiles_ ) {
        Q_UNUSED( view );
        openFile.view->applyChange( change );
    }
}

void Session::applyHighlighterSetChange()
{
    ViewChange change;
    change.highlighterSets = true;
    for ( auto& [ view, openFile ] : openFiles_ ) {
        Q_UNUSED( view );
        openFile.view->applyChange( change );
    }
}

std::vector<WindowSession> Session::windowSessions()
{
    const auto& session = SessionInfo::get();
    const auto& sessionWindows = session.windows();

    std::vector<WindowSession> windows;
    for ( auto i = 0; i < sessionWindows.size(); ++i ) {
        windows.emplace_back( shared_from_this(), sessionWindows.at( i ), i );
    }

    return windows;
}

void WindowSession::save(
    const std::vector<std::tuple<const ViewInterface*, uint64_t,
                                 std::shared_ptr<const ViewContextInterface>>>& view_list,
    const QByteArray& geometry )
{
    LOG_DEBUG << "Session::save";

    std::vector<SessionInfo::OpenFile> session_files;
    for ( const auto& view : view_list ) {
        const ViewInterface* view_object;
        uint64_t top_line;
        std::shared_ptr<const ViewContextInterface> view_context;

        std::tie( view_object, top_line, view_context ) = view;

        const Session::OpenFile* file = appSession_->findOpenFileFromView( view_object );
        if ( !file ) {
            LOG_WARNING << "save: view not found in open files, skipping";
            continue;
        }

        LOG_DEBUG << "Saving " << file->fileName.toLocal8Bit().data() << " in session.";
        session_files.emplace_back( file->fileName, top_line, view_context->toString() );
    }

    auto& session = SessionInfo::getSynced();
    session.setOpenFiles( windowId_, session_files );
    session.setGeometry( windowId_, geometry );
    session.save();
}

ViewInterface* WindowSession::open( const QString& fileName, const ViewFactory& viewFactory )
{
    // The view context saved for this Log File in any window, if it was
    // open when the Session was last saved.
    const auto savedViewContext = [ &fileName ]() {
        const auto& session = SessionInfo::get();
        for ( const auto& windowId : session.windows() ) {
            const auto openedFiles = session.openFiles( windowId );
            const auto saved = std::find_if(
                openedFiles.begin(), openedFiles.end(),
                [ &fileName ]( const auto& openFile ) { return openFile.fileName == fileName; } );
            if ( saved != openedFiles.end() ) {
                return saved->viewContext;
            }
        }
        return QString{};
    }();

    auto* view = appSession_->open( fileName, viewFactory, savedViewContext );
    openedFiles_.push_back( fileName );
    return view;
}

OpenedFilesList WindowSession::restore( const ViewFactory& viewFactory, int* currentFileIndex )
{
    const auto& session = SessionInfo::get();

    std::vector<SessionInfo::OpenFile> session_files = session.openFiles( windowId_ );
    LOG_DEBUG << "Session returned " << session_files.size();
    OpenedFilesList result;

    for ( const auto& file : session_files ) {
        LOG_DEBUG << "Create view for " << file.fileName;
        // The same path as opening a Log File by hand.
        ViewInterface* view = appSession_->open( file.fileName, viewFactory, file.viewContext );
        result.emplace_back( file.fileName, view );
        openedFiles_.emplace_back( file.fileName );
    }

    *currentFileIndex = logsquirl::isize( result ) - 1;

    return result;
}

WindowSession::WindowSession( std::shared_ptr<Session> appSession, const QString& id, size_t index )
    : appSession_{ std::move( appSession ) }
    , windowId_{ id }
    , windowIndex_{ index }
{
    LOG_INFO << "created session for " << id;
    // A window restored from the Session is already in it; only a new window
    // is added, and saved.
    if ( SessionInfo::get().windows().contains( id ) ) {
        return;
    }

    auto& sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( id );
    sessionInfo.save();
}

void WindowSession::restoreGeometry( QByteArray* geometry ) const
{
    const auto& session = SessionInfo::get();
    *geometry = session.geometry( windowId_ );
}

bool WindowSession::close()
{
    LOG_INFO << "close window session " << windowId_;

    if ( appSession_->exitRequested() ) {
        return true;
    }

    auto& session = SessionInfo::getSynced();
    auto isRemoved = session.remove( windowId_ );
    session.save();

    LOG_INFO << "session is removed " << isRemoved;

    return !isRemoved;
}