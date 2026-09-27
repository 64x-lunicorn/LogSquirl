/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch_test_macros.hpp>

#include <QTemporaryFile>

#include "fake_file_watch.h"
#include "test_policies.h"
#include "test_utils.h"

#include "configuration.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "session.h"

// Settings arrive as a snapshot, so a changed setting needs an explicit
// path back to whatever is already running (#95). The Watch Policy is held by
// the views and the file watcher, which only an open tab shows. How the
// Indexing, Search and Decoding Policies reach a Log File is the Open Log
// File's, tested without a widget in openlogfile_policies_test.cpp (#556).

namespace {

constexpr int LineCount = 400;

bool generateTestFile( QTemporaryFile& file )
{
    char line[ 120 ];
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < LineCount; i++ ) {
        snprintf( line, sizeof( line ), "POLICY_CHANGE_TEST this is line %06d\n", i );
        file.write( line, static_cast<qint64>( qstrlen( line ) ) );
    }
    file.flush();
    return true;
}

} // namespace

SCENARIO( "A changed Watch Policy reaches the file watcher and the views of a Log File that is "
          "already open",
          "[ui][settings]" )
{
    QTemporaryFile file{ "policy_change_watch_XXXXXX" };
    REQUIRE( generateTestFile( file ) );

    GIVEN( "a Log File opened while nothing is watched" )
    {
        auto policies = testSettingsPolicies();
        policies.watch.nativeWatchEnabled = false;
        policies.watch.pollingEnabled = false;

        const auto fileWatch = std::make_shared<FakeFileWatch>();
        Session session{ policies, std::make_shared<LogFormatCatalog>(), fileWatch };
        // Destroyed before the Session it was opened from: it is declared
        // after it, so it goes first.
        std::unique_ptr<CrawlerWidget> crawler{ static_cast<CrawlerWidget*>(
            session.open( file.fileName(),
                          []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) };

        THEN( "its views were built knowing that following is not possible" )
        {
            REQUIRE_FALSE( crawler->watchPolicy().anyWatchEnabled() );
        }

        THEN( "the file watcher was handed the Watch Policy once, before the Log File was "
              "watched" )
        {
            REQUIRE( fileWatch->watchPolicies() == std::vector{ policies.watch } );
        }

        WHEN( "a Watch Policy that polls arrives" )
        {
            auto changed = policies;
            changed.watch.pollingEnabled = true;
            session.applyPolicies( changed );

            THEN( "the open Log File holds it, without having been opened again" )
            {
                REQUIRE( crawler->watchPolicy().anyWatchEnabled() );
                REQUIRE( crawler->watchPolicy().pollingEnabled );
            }

            THEN( "the file watcher follows it" )
            {
                REQUIRE( fileWatch->watchPolicies()
                         == std::vector{ policies.watch, changed.watch } );
            }
        }

        WHEN( "a Policy that changes some other axis arrives" )
        {
            auto changed = policies;
            changed.search.contextLinesCount = 3;
            session.applyPolicies( changed );

            THEN( "the Watch Policy it holds is left exactly as it was" )
            {
                REQUIRE_FALSE( crawler->watchPolicy().anyWatchEnabled() );
            }

            THEN( "the file watcher is not handed it again" )
            {
                REQUIRE( fileWatch->watchPolicies() == std::vector{ policies.watch } );
            }
        }

        // What an Options Dialog does: write the settings store, and tell the
        // Session that the settings changed. Nothing re-derives the Policies
        // for it.
        WHEN( "polling is ticked in the settings and the Session is told they changed" )
        {
            auto& config = Configuration::get();
            const auto nativeWatch = config.nativeFileWatchEnabled();
            const auto polling = config.pollingEnabled();
            config.setNativeFileWatchEnabled( false );
            config.setPollingEnabled( true );

            session.applyChange( Changed::Settings );

            config.setNativeFileWatchEnabled( nativeWatch );
            config.setPollingEnabled( polling );

            THEN( "the open Log File and the file watcher both follow the re-derived Policy" )
            {
                REQUIRE( crawler->watchPolicy().pollingEnabled );
                REQUIRE( fileWatch->watchPolicies().size() == 2 );
                REQUIRE( fileWatch->watchPolicies().back().pollingEnabled );
            }
        }
    }
}
