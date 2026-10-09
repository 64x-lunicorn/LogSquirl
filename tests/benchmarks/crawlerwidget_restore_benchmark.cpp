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

// Building the views of a restored tab (#786): a Crawler Widget built from
// the view state a Session saved for it, with three Kept Searches and with
// none. The Log File is never opened, so what is counted is what a restore
// does on the main thread for a tab before any Log File loads: the widget,
// the Filtered View of each kept Search, their tabs and their shortcuts.
// With three Kept Searches a restore registered every shortcut of the widget
// and of every view a second time; #786 registers each new view's once.
//
// Only API that origin/master already had is used, so this file builds
// unchanged on both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "configuration.h"
#include "crawlerwidget.h"
#include "datalocation.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
#include "quickfindpattern.h"
#include "savedsearches.h"
#include "test_policies.h"
#include "viewinterface.h"
#include "viewstatecodec.h"

#include <QApplication>
#include <QString>

#include <memory>

#include "instruction_count.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "isolated_settings.h"

// The settings of this process live next to it, as the tests' do.
const bool DataLocation::ForcePortable = true;

namespace {

// The view state of a tab as a Session saves it: three Kept Searches of a
// word each, the second one current, or none.
QString viewContextWithSearches( int searches )
{
    ViewState state;
    const char* words[] = { "error", "warn", "info" };
    for ( auto index = 0; index < searches; ++index ) {
        state.searches.push_back( KeptSearchState{ .pattern = words[ index % 3 ] } );
    }
    state.currentSearch = searches > 1 ? 1 : 0;
    return encodeViewState( state );
}

// What the Session hands the views of a Log File it opens, for a Log File
// that is never opened.
struct TabBuild {
    SettingsPolicies policies = testSettingsPolicies();
    std::shared_ptr<LogFormatCatalog> catalog = std::make_shared<LogFormatCatalog>();
    std::shared_ptr<QuickFindPattern> quickFindPattern = std::make_shared<QuickFindPattern>();

    std::unique_ptr<CrawlerWidget> build( const QString& viewContext ) const
    {
        auto openLogFile = std::make_shared<OpenLogFile>( policies.indexing, policies.search,
                                                          policies.fileAccess, policies.decoding,
                                                          policies.recognition, catalog, nullptr );
        return std::make_unique<CrawlerWidget>( ViewBuild{
            .openLogFile = std::move( openLogFile ),
            .quickFindPattern = quickFindPattern,
            .policies = policies,
            .savedSearches = &SavedSearches::getSynced(),
            .viewContext = viewContext,
        } );
    }
};

} // namespace

TEST_CASE( "Building the views of a restored tab", "[crawlerwidget-restore-benchmark]" )
{
    const TabBuild tab;
    const auto withSearches = viewContextWithSearches( 3 );
    const auto withoutSearches = viewContextWithSearches( 0 );

    REQUIRE( tab.build( withSearches )->context() != nullptr );

    BENCHMARK( "a tab with three Kept Searches: the Crawler Widget and its Filtered Views" )
    {
        return tab.build( withSearches );
    };

    BENCHMARK( "a tab without Kept Searches: the Crawler Widget" )
    {
        return tab.build( withoutSearches );
    };
}

int main( int argc, char* argv[] )
{
    // The test case runs beside a settings file of this process's own, so
    // that no test binary reads or writes the one in the build directory
    // (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    // What the views read, as main() reads them at startup.
    Configuration::getSynced();

    return Catch::Session().run( argc, argv );
}
