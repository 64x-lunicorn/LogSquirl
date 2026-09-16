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

#include <cstdlib>
#include <iostream>

#include <mimalloc.h>

#include "configuration.h"
#include "dispatch_to.h"
#include "loadingstatus.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logger.h"
#include "persistentinfo.h"
#include "settingspolicies.h"

#include "cli.h"

const bool PersistentInfo::ForcePortable = true;

namespace {

// The command line tool's answer to a failure the engine reports: it is
// printed, and the tool exits with a non-zero code.
[[noreturn]] void exitWithFailure( const QString& failure )
{
    std::cerr << "logsquirl_grep: " << failure.toStdString() << std::endl;
    exit( EXIT_FAILURE );
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

    auto configuration = Configuration::getSynced();

    // The one place in this tool that touches the settings store: the log
    // data library reads none itself, it is handed what it may know (#94).
    const auto policies = deriveSettingsPolicies( configuration );

    // Nothing here watches the Log File: the log data follows no change on
    // disk by itself (#249), and this tool searches the Log File once, as it
    // was when it loaded, and exits.

    // Hiding ANSI color sequences is not applied here: this tool has always
    // matched the Log Lines as they are in the file, and still does.
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, DecodingPolicy{} };
    auto filteredData = logData.getNewFilteredData();

    filteredData->connect(
        filteredData.get(), &LogFilteredData::searchStateChanged,
        [ & ]( SearchSession::State state ) {
            if ( state.phase == SearchSession::Phase::Failed
                 || state.phase == SearchSession::Phase::InvalidPattern ) {
                exitWithFailure( state.errorString );
            }

            if ( state.phase == SearchSession::Phase::Complete ) {

                const auto nbMatches = state.matchCount;
                LOG_INFO << "Searched finished, got " << nbMatches.get() << " matches";

                const auto defaultChunkSize = 1000_lcount;
                for ( auto chunkStart = 0_lnum; chunkStart < nbMatches;
                      chunkStart = chunkStart + defaultChunkSize ) {
                    auto chunkSize
                        = std::min( defaultChunkSize.get(), nbMatches.get() - chunkStart.get() );
                    auto lines = filteredData->getLines( chunkStart, LinesCount( chunkSize ) );
                    for ( const auto& l : lines ) {
                        std::cout << l.toStdString() << "\n";
                    }
                }

                exit( EXIT_SUCCESS );
            }
        } );

    logData.connect(
        &logData, &LogData::loadingFinished, [ & ]( LoadingStatus status, const QString& failure ) {
            if ( status != LoadingStatus::Successful ) {
                exitWithFailure( failure.isEmpty()
                                     ? QString( "loading the Log File did not finish" )
                                     : failure );
            }
            dispatchToMainThread( [ & ] {
                filteredData->request( RegularExpressionPattern( parameters.pattern ) );
            } );
        } );

    logData.attachFile( parameters.filenames.front() );
    return app.exec();
}
