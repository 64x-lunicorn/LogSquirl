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

#include <mimalloc.h>

#include "configuration.h"
#include "logdata.h"
#include "settingspolicies.h"
#include "logfiltereddata.h"
#include "dispatch_to.h"
#include "logger.h"
#include "persistentinfo.h"

#include "cli.h"

const bool PersistentInfo::ForcePortable = true;

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

    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
    auto filteredData = logData.getNewFilteredData();

    filteredData->connect(
        filteredData.get(), &LogFilteredData::searchStateChanged,
        [ & ]( SearchSession::State state ) {
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

    logData.connect( &logData, &LogData::loadingFinished, [ & ]() {
        dispatchToMainThread(
            [ & ] { filteredData->request( RegularExpressionPattern( parameters.pattern ) ); } );
    } );

    logData.attachFile( parameters.filenames.front() );
    return app.exec();
}
