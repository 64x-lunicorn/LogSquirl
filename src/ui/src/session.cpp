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

#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "savedsearches.h"
#include "sessioninfo.h"
#include "viewinterface.h"

Session::Session( const SettingsPolicies& policies,
                  std::shared_ptr<LogFormatCatalog> logFormatCatalog )
    : policies_( policies )
    , logFormatCatalog_( std::move( logFormatCatalog ) )
{
    // Get the global search history (it remains the property
    // of the Persistent)
    savedSearches_ = &SavedSearches::getSynced();
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

ViewInterface* Session::open( const QString& file_name,
                              const std::function<ViewInterface*()>& view_factory )
{
    return openAlways( file_name, view_factory, nullptr );
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

    *fileSize = static_cast<uint64_t>( file->logData->getFileSize() );
    *fileNbLine = file->logData->getNbLine().get();
    *lastModified = file->logData->getLastModifiedDate();
}

ViewInterface* Session::openAlways( const QString& file_name,
                                    const std::function<ViewInterface*()>& view_factory,
                                    const QString& view_context )
{
    // Create the data objects
    auto log_data = std::make_shared<LogData>( policies_.indexing, policies_.search,
                                               policies_.fileAccess, policies_.decoding );
    auto log_filtered_data = std::shared_ptr<LogFilteredData>( log_data->getNewFilteredData() );

    ViewInterface* view = view_factory();
    view->setData( log_data, log_filtered_data );
    view->setQuickFindPattern( quickFindPattern_ );
    view->setFormatRecognition( policies_.recognition, logFormatCatalog_ );
    view->setDecorationPolicy( policies_.decoration );
    view->setPresentationPolicy( policies_.presentation );
    view->setQuickFindPolicy( policies_.quickFind );
    view->setWatchPolicy( policies_.watch );
    view->setFileAccessPolicy( policies_.fileAccess );
    // Last: the view builds itself when it is handed these, so every Policy
    // above has to be in its hands before it does.
    view->setSavedSearches( savedSearches_ );

    if ( !view_context.isEmpty() )
        view->setViewContext( view_context );

    // Insert in the hash
    openFiles_.insert( { view, { file_name, log_data, log_filtered_data, view } } );

    // Start loading the file
    log_data->attachFile( file_name );

    return view;
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

void Session::applyPolicies( const SettingsPolicies& policies )
{
    const auto indexingChanged = policies.indexing != policies_.indexing;
    const auto searchChanged = policies.search != policies_.search;
    const auto recognitionChanged = policies.recognition != policies_.recognition;
    const auto decodingChanged = policies.decoding != policies_.decoding;
    const auto decorationChanged = policies.decoration != policies_.decoration;
    const auto presentationChanged = policies.presentation != policies_.presentation;
    const auto quickFindChanged = policies.quickFind != policies_.quickFind;
    const auto watchChanged = policies.watch != policies_.watch;

    // Every time, changed Policies or not: the user's Log Formats are read
    // again. Log Formats handed out before stay valid for whoever holds them.
    if ( logFormatCatalog_ ) {
        logFormatCatalog_->rebuild();
    }

    // Stored whether or not anything is open: the File Access Policy in
    // particular reaches a Log File only when one is built, so this is the
    // only thing a change to it can do.
    policies_ = policies;

    if ( !indexingChanged && !searchChanged && !recognitionChanged && !decodingChanged
         && !decorationChanged && !presentationChanged && !quickFindChanged && !watchChanged ) {
        return;
    }

    for ( auto& [ view, openFile ] : openFiles_ ) {
        Q_UNUSED( view );

        if ( recognitionChanged ) {
            // Takes effect at the view's next Format Recognition; an open
            // Table View is not torn down.
            openFile.view->setRecognitionPolicy( policies_.recognition );
        }

        if ( decorationChanged ) {
            // Re-colors the views of every open Log File, not only the one
            // the active tab shows, and without a new Search.
            openFile.view->setDecorationPolicy( policies_.decoration );
        }

        if ( presentationChanged ) {
            // Reaches the views of every open Log File, not only the one the
            // active tab shows.
            openFile.view->setPresentationPolicy( policies_.presentation );
        }

        if ( quickFindChanged ) {
            // The views' own use of it: how the Search line reads its pattern
            // and what the Table View hands a QuickFind. The window's QuickFind
            // bar and mux read it from the Session instead.
            openFile.view->setQuickFindPolicy( policies_.quickFind );
        }

        if ( watchChanged ) {
            // Takes following away from the views of a Log File that is
            // already open, or gives it back, without it being reopened.
            openFile.view->setWatchPolicy( policies_.watch );
        }

        if ( indexingChanged ) {
            openFile.logData->setIndexingPolicy( policies_.indexing );
        }

        if ( searchChanged ) {
            // The Log File hands it on to every LogFilteredData built from
            // it, which is more than the one this Session holds: a tab that
            // kept an earlier Search has its own.
            openFile.logData->setSearchPolicy( policies_.search );
        }

        if ( decodingChanged ) {
            // Log Lines read from now on are decoded under it, and the Log
            // File tells its views to read what they show again. Search
            // results already found stay as they were.
            openFile.logData->setDecodingPolicy( policies_.decoding );
        }
    }
}

void Session::applyHighlighterSetChange()
{
    for ( auto& [ view, openFile ] : openFiles_ ) {
        Q_UNUSED( view );
        openFile.view->applyHighlighterSetChange();
    }
}

std::vector<WindowSession> Session::windowSessions()
{
    const auto& session = SessionInfo::getSynced();
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

std::vector<std::pair<QString, ViewInterface*>>
WindowSession::restore( const std::function<ViewInterface*()>& view_factory,
                        int* current_file_index )
{
    const auto& session = SessionInfo::getSynced();

    std::vector<SessionInfo::OpenFile> session_files = session.openFiles( windowId_ );
    LOG_DEBUG << "Session returned " << session_files.size();
    std::vector<std::pair<QString, ViewInterface*>> result;

    for ( const auto& file : session_files ) {
        LOG_DEBUG << "Create view for " << file.fileName;
        ViewInterface* view
            = appSession_->openAlways( file.fileName, view_factory, file.viewContext );
        result.emplace_back( file.fileName, view );
        openedFiles_.emplace_back( file.fileName );
    }

    *current_file_index = logsquirl::isize( result ) - 1;

    return result;
}

WindowSession::WindowSession( std::shared_ptr<Session> appSession, const QString& id, size_t index )
    : appSession_{ std::move( appSession ) }
    , windowId_{ id }
    , windowIndex_{ index }
{
    LOG_INFO << "created session for " << id;
    auto sessionInfo = SessionInfo::getSynced();
    sessionInfo.add( id );
    sessionInfo.save();
}

void WindowSession::restoreGeometry( QByteArray* geometry ) const
{
    const auto& session = SessionInfo::getSynced();
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