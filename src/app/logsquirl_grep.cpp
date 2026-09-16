/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <mimalloc.h>

#include "configuration.h"
#include "loadingstatus.h"
#include "logfiltereddata.h"
#include "logger.h"
#include "openlogfile.h"
#include "persistentinfo.h"
#include "settingspolicies.h"

#include "cli.h"

const bool PersistentInfo::ForcePortable = true;

namespace {

// The command line tool's answer to a failure the engine reports: it is
// printed, and the tool exits with a non-zero code.
void printFailure( const QString& failure )
{
    std::cerr << "logsquirl_grep: " << failure.toStdString() << std::endl;
}

void printMatches( LogFilteredData& search, LinesCount nbMatches )
{
    LOG_INFO << "Searched finished, got " << nbMatches.get() << " matches";

    const auto defaultChunkSize = 1000_lcount;
    for ( auto chunkStart = 0_lnum; chunkStart < nbMatches;
          chunkStart = chunkStart + defaultChunkSize ) {
        auto chunkSize = std::min( defaultChunkSize.get(), nbMatches.get() - chunkStart.get() );
        auto lines = search.getLines( chunkStart, LinesCount( chunkSize ) );
        for ( const auto& l : lines ) {
            std::cout << l.toStdString() << "\n";
        }
    }
}

} // namespace

int main( int argc, char* argv[] )
{
#ifdef LOGSQUIRL_USE_MIMALLOC
    mi_stats_reset();
#endif
    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );
    qRegisterMetaType<SearchId>( "SearchId" );
    qRegisterMetaType<SearchSession::State>( "SearchSession::State" );

    QCoreApplication app( argc, argv );
    CliParameters parameters( app, true );

    logging::enableLogging( true, static_cast<logging::LogLevel>( parameters.log_level ) );

    if ( parameters.filenames.empty() ) {
        printFailure( "no Log File given" );
        return EXIT_FAILURE;
    }

    auto configuration = Configuration::getSynced();

    // The one place in this tool that touches the settings store: the log
    // data library reads none itself, it is handed what it may know (#94).
    const auto policies = deriveSettingsPolicies( configuration );

    // The tool follows its Log File as an Open Log File, the way the desktop
    // application does (#247). It hands over no File Watch Port: the Log File
    // is not followed on disk, and is searched once, as it was when it
    // loaded. Nor a Log Format Catalog: no Log Format is recognized.
    //
    // Hiding ANSI color sequences is not applied here: this tool has always
    // matched the Log Lines as they are in the file, and still does.
    OpenLogFile openLogFile{ policies.indexing,
                             policies.search,
                             policies.fileAccess,
                             DecodingPolicy{},
                             RecognitionPolicy{},
                             nullptr,
                             nullptr };

    // The first outcome is the one the tool exits with: the event loop is
    // left, and nothing told after it counts.
    bool finished = false;
    const auto finish = [ & ]( int exitCode ) {
        if ( !finished ) {
            finished = true;
            app.exit( exitCode );
        }
    };

    QObject::connect( &openLogFile, &OpenLogFile::loadingFinished,
                      [ & ]( const OpenLogFile::LoadFinished& load ) {
                          if ( !finished && load.status != LoadingStatus::Successful ) {
                              printFailure( load.failure.isEmpty()
                                                ? QString( "loading the Log File did not finish" )
                                                : load.failure );
                              finish( EXIT_FAILURE );
                          }
                      } );

    QObject::connect( &openLogFile, &OpenLogFile::searchUpdated,
                      [ & ]( const SearchSession::State& state ) {
                          if ( finished ) {
                              return;
                          }
                          switch ( state.phase ) {
                          case SearchSession::Phase::Failed:
                          case SearchSession::Phase::InvalidPattern:
                              printFailure( state.errorString );
                              finish( EXIT_FAILURE );
                              break;
                          case SearchSession::Phase::Complete:
                              printMatches( *openLogFile.filteredData(), state.matchCount );
                              finish( EXIT_SUCCESS );
                              break;
                          default:
                              break;
                          }
                      } );

    // The Search is requested with the Log File opened: the Open Log File
    // runs it once the Log File has loaded.
    openLogFile.open( parameters.filenames.front() );
    openLogFile.requestSearch( RegularExpressionPattern( parameters.pattern ) );

    return app.exec();
}
