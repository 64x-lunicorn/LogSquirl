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
#include <utility>

#include <mimalloc.h>

#include "configuration.h"
#include "displayedlines.h"
#include "loadingstatus.h"
#include "logdata.h"
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

void printMatches( const LogFilteredData& search, LinesCount nbMatches )
{
    LOG_INFO << "Searched finished, got " << nbMatches.get() << " matches";

    // The Displayed Lines are walked once and their text is read in chunks
    // through the sparse read, as UTF-8 and without a QString in between.
    const auto displayedLines = search.copyDisplayedLines();
    const auto& logFile = search.sourceLogData();
    DisplayedLinesCursor cursor( displayedLines, 0_lnum );

    constexpr auto ChunkSize = 1000_lcount;
    for ( auto printed = 0_lcount; printed < nbMatches && cursor.hasLine(); ) {
        const auto chunkSize
            = LinesCount( std::min( ChunkSize.get(), nbMatches.get() - printed.get() ) );
        const auto lines = cursor.takeForward( chunkSize );
        const auto text = logFile.getUtf8LinesSparse( lines );
        std::cout.write( text.data(), static_cast<std::streamsize>( text.size() ) );
        printed = printed + LinesCount( static_cast<LinesCount::UnderlyingType>( lines.size() ) );
    }
    std::cout.flush();
}

} // namespace

int main( int argc, char* argv[] )
{
#ifdef LOGSQUIRL_USE_MIMALLOC
    mi_stats_reset();
#endif
    // The types the engine's signals carry across threads, as the desktop
    // application registers them.
    qRegisterMetaType<LoadingStatus>( "LoadingStatus" );
    qRegisterMetaType<LinesCount>( "LinesCount" );
    qRegisterMetaType<LineNumber>( "LineNumber" );
    qRegisterMetaType<SearchId>( "SearchId" );
    qRegisterMetaType<SearchSession::State>( "SearchSession::State" );

    QCoreApplication app( argc, argv );
    CliParameters parameters( app, true );

    // Every log message goes to stderr, the way printFailure() reports a
    // failure: stdout carries only the Log Lines the Search matched, so the
    // output can be piped into another tool without warnings such as "Non LF
    // terminated file" mixed into the matches (#327). The -d/--debug flag
    // still raises the level, and its messages land on stderr too.
    logging::enableLogging( true, static_cast<logging::LogLevel>( parameters.log_level ),
                            logging::ConsoleStream::StdErr );

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

    // The Search is requested once the Log File has loaded, and not before:
    // only then is its Encoding known. The Open Log File settles it before it
    // tells the load, the way it does for the desktop application (#326,
    // #393): the Encoding the settings force, else the one detected, else the
    // locale's. So the Search matches the Log Lines as a user reads them, and
    // the matches print as they are in the Log File.
    bool searchRequested = false;

    QObject::connect( &openLogFile, &OpenLogFile::loadingFinished,
                      [ & ]( const OpenLogFile::LoadFinished& load ) {
                          if ( finished ) {
                              return;
                          }
                          if ( load.status != LoadingStatus::Successful ) {
                              printFailure( load.failure.isEmpty()
                                                ? QString( "loading the Log File did not finish" )
                                                : load.failure );
                              finish( EXIT_FAILURE );
                              return;
                          }
                          if ( std::exchange( searchRequested, true ) ) {
                              return;
                          }
                          openLogFile.requestSearch(
                              RegularExpressionPattern( parameters.pattern ) );
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

    openLogFile.open( parameters.filenames.front() );

    return app.exec();
}
