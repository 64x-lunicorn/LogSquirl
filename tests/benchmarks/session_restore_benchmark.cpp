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

// Benchmarks for restoring a Session of 20 tabs at startup (#301): the
// Session is built, which reads the Session info, each of its windows is
// restored (window list, geometry, Log Files) and every Log File gets its tab,
// named and styled from the tab names and tab groups. Before #301 each tab
// synced and re-read the settings store several times; after it, the store is
// read once at startup.
//
// The settings store is the portable one next to this binary (the tests'
// store), not the macOS preferences daemon the application uses, so these
// numbers show the re-parse part of the cost and a lower bound of the sync
// part. What was in the store before is written back at the end.
//
// Only API that origin/master already had is used, so this file builds
// unchanged on both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "configuration.h"
#include "logformatcatalog.h"
#include "persistentinfo.h"
#include "recording_views.h"
#include "session.h"
#include "sessioninfo.h"
#include "tabbedcrawlerwidget.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
#include "test_policies.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QTemporaryDir>
#include <QWidget>

#include <memory>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

constexpr int TabCount = 20;

// A Log File's widget as the tab area sees it: it only reports its data status.
class StubCrawler final : public QWidget {
    Q_OBJECT

public:
    Q_SIGNAL void dataStatusChanged( DataStatus status );
};

// Twenty small Log Files, saved in the settings store as the one window of
// the last Session, each with a view context, most with a custom tab name and
// some in tab groups -- as a user's store looks after working with them.
class TwentyTabSession {
public:
    TwentyTabSession()
        : savedSession_( SessionInfo::getSynced() )
        , savedTabNames_( TabNameMapping::getSynced() )
        , savedTabGroups_( TabGroupInfo::getSynced() )
    {
        REQUIRE( dir_.isValid() );

        std::vector<SessionInfo::OpenFile> openFiles;
        auto& tabNames = TabNameMapping::getSynced();
        auto& tabGroups = TabGroupInfo::getSynced();
        const auto groupIds = std::vector<QString>{
            tabGroups.addGroup( QStringLiteral( "Backend" ), QColor( 0x2a, 0x82, 0xda ) ),
            tabGroups.addGroup( QStringLiteral( "Frontend" ), QColor( 0xda, 0x82, 0x2a ) ),
        };

        for ( auto i = 0; i < TabCount; ++i ) {
            const auto path = dir_.filePath( QStringLiteral( "service-%1.log" ).arg( i ) );
            QFile file( path );
            REQUIRE( file.open( QIODevice::WriteOnly ) );
            file.write( "2026-09-17 10:00:00.000 INFO started\n"
                        "2026-09-17 10:00:01.000 WARN slow response\n" );
            paths_.push_back( path );

            openFiles.emplace_back(
                path, 0,
                QStringLiteral( "S0:1:IC0:AR0:LC1:LTS0:CNT0:SP[400,100]:FV0:FT0:ENC0:MRK[]" ) );
            if ( i % 4 != 0 ) {
                tabNames.setTabName( path, QStringLiteral( "Service %1" ).arg( i ) );
            }
            if ( i % 3 == 0 ) {
                tabGroups.addTabToGroup( groupIds[ static_cast<size_t>( i ) % groupIds.size() ],
                                         path );
            }
        }

        // The only window of the stored Session.
        SessionInfo session;
        session.add( WindowId );
        session.setOpenFiles( WindowId, openFiles );
        session.setGeometry( WindowId, QByteArray( 64, 'g' ) );
        session.save();
        tabNames.save();
        tabGroups.save();

        PersistentInfo::getSettings( session_settings{} ).sync();
        SessionInfo::getSynced();
    }

    ~TwentyTabSession()
    {
        savedSession_.save();
        savedTabNames_.save();
        savedTabGroups_.save();
        PersistentInfo::getSettings( session_settings{} ).sync();
        SessionInfo::getSynced();
        TabNameMapping::getSynced();
        TabGroupInfo::getSynced();
    }

    TwentyTabSession( const TwentyTabSession& ) = delete;
    TwentyTabSession& operator=( const TwentyTabSession& ) = delete;

    static constexpr const char* WindowId = "session_restore_benchmark";

private:
    SessionInfo savedSession_;
    TabNameMapping savedTabNames_;
    TabGroupInfo savedTabGroups_;
    QTemporaryDir dir_;
    std::vector<QString> paths_;
};

// Restores the Session into a tab area, as the application does at startup.
// Returns the number of tabs added.
int restoreSession( const std::shared_ptr<LogFormatCatalog>& catalog )
{
    const auto appSession = std::make_shared<Session>( testSettingsPolicies(), catalog );
    std::vector<RecordingViews*> built;
    auto tabs = 0;

    {
        TabbedCrawlerWidget tabArea;
        for ( auto& window : appSession->windowSessions() ) {
            QByteArray geometry;
            window.restoreGeometry( &geometry );

            auto currentFileIndex = -1;
            const auto opened
                = window.restore( RecordingViews::factory( built ), &currentFileIndex );
            for ( const auto& [ fileName, view ] : opened ) {
                tabArea.addCrawler( new StubCrawler, fileName );
                ++tabs;
            }
        }
    }

    for ( auto* views : built ) {
        delete views;
    }
    return tabs;
}

} // namespace

TEST_CASE( "Restoring a Session of 20 tabs at startup", "[session-restore-benchmark]" )
{
    TwentyTabSession stored;
    const auto catalog = std::make_shared<LogFormatCatalog>();

    REQUIRE( restoreSession( catalog ) == TabCount );

    BENCHMARK( "20-tab Session: build, restore windows and Log Files, add tabs" )
    {
        return restoreSession( catalog );
    };

    BENCHMARK_ADVANCED( "20-tab Session: add and style the tabs only" )(
        Catch::Benchmark::Chronometer meter )
    {
        const auto appSession = std::make_shared<Session>( testSettingsPolicies(), catalog );
        auto windows = appSession->windowSessions();
        REQUIRE( windows.size() == 1 );
        std::vector<RecordingViews*> built;
        auto currentFileIndex = -1;
        const auto opened
            = windows.front().restore( RecordingViews::factory( built ), &currentFileIndex );

        std::vector<std::unique_ptr<TabbedCrawlerWidget>> tabAreas;
        for ( auto run = 0; run < meter.runs(); ++run ) {
            tabAreas.push_back( std::make_unique<TabbedCrawlerWidget>() );
        }

        meter.measure( [ &tabAreas, &opened ]( int run ) {
            auto& tabArea = *tabAreas[ static_cast<size_t>( run ) ];
            for ( const auto& [ fileName, view ] : opened ) {
                tabArea.addCrawler( new StubCrawler, fileName );
            }
            return tabArea.count();
        } );

        tabAreas.clear();
        for ( auto* views : built ) {
            delete views;
        }
    };
}

int main( int argc, char* argv[] )
{
    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    // What the Session and the views read, as main() reads them at startup.
    Configuration::getSynced();

    return Catch::Session().run( argc, argv );
}

#include "session_restore_benchmark.moc"
