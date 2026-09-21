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
// A second case restores a Session of several large Log Files (#300) and
// measures the time until the current tab's Log File has loaded, and until
// every tab's has. Before #300 every Log File started loading at once; after
// it the current tab's loads first and the others one after another.
//
// Only API that origin/master already had is used, so this file builds
// unchanged on both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "configuration.h"
#include "generated_log_file.h"
#include "logformatcatalog.h"
#include "openlogfile.h"
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
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include "isolated_settings.h"

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

// The number in an environment variable, or `fallback` when it holds none.
std::uint64_t numberFromEnvironment( const char* name, std::uint64_t fallback )
{
    bool isNumber = false;
    const auto requested = qgetenv( name ).toULongLong( &isNumber );
    return ( isNumber && requested > 0 ) ? requested : fallback;
}

// Several large Log Files saved as the one window of the last Session, the
// last one its current tab: 4 Log Files of 32 MiB each by default, or
// LOGSQUIRL_BENCHMARK_SESSION_LOG_FILES of LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB
// MiB each.
class LargeLogFileSession {
public:
    LargeLogFileSession()
        : savedSession_( SessionInfo::getSynced() )
    {
        REQUIRE( dir_.isValid() );

        const auto count = numberFromEnvironment( "LOGSQUIRL_BENCHMARK_SESSION_LOG_FILES", 4 );
        const auto bytes
            = numberFromEnvironment( "LOGSQUIRL_BENCHMARK_SESSION_LOG_FILE_MB", 32 ) * 1024 * 1024;

        std::vector<SessionInfo::OpenFile> openFiles;
        for ( std::uint64_t i = 0; i < count; ++i ) {
            const auto path = dir_.filePath( QStringLiteral( "large-%1.log" ).arg( i ) );
            bool written = false;
            logdatabenchmark::writeGeneratedLogFile(
                path, logdatabenchmark::LogFileShape::ShortLines, bytes, written );
            REQUIRE( written );
            openFiles.emplace_back( path, 0, QString{} );
        }
        fileCount_ = static_cast<int>( count );

        SessionInfo session;
        session.add( WindowId );
        session.setOpenFiles( WindowId, openFiles );
        session.save();

        PersistentInfo::getSettings( session_settings{} ).sync();
        SessionInfo::getSynced();
    }

    ~LargeLogFileSession()
    {
        savedSession_.save();
        PersistentInfo::getSettings( session_settings{} ).sync();
        SessionInfo::getSynced();
    }

    LargeLogFileSession( const LargeLogFileSession& ) = delete;
    LargeLogFileSession& operator=( const LargeLogFileSession& ) = delete;

    int fileCount() const
    {
        return fileCount_;
    }

    static constexpr const char* WindowId = "session_restore_benchmark_large";

private:
    SessionInfo savedSession_;
    QTemporaryDir dir_;
    int fileCount_ = 0;
};

// A restored Session and the views of its Log Files, which it outlives.
struct RestoredSession {
    std::shared_ptr<Session> appSession;
    std::vector<RecordingViews*> built;

    RestoredSession() = default;
    RestoredSession( const RestoredSession& ) = delete;
    RestoredSession& operator=( const RestoredSession& ) = delete;

    ~RestoredSession()
    {
        for ( auto* views : built ) {
            delete views;
        }
    }
};

enum class Loaded { CurrentTab, EveryTab };

// Restores the large Log File Session, as the application does at startup,
// and returns once the current tab's Log File, or every tab's, has loaded.
std::unique_ptr<RestoredSession>
restoreUntilLoaded( const std::shared_ptr<LogFormatCatalog>& catalog, Loaded loaded )
{
    auto restored = std::make_unique<RestoredSession>();
    restored->appSession = std::make_shared<Session>( testSettingsPolicies(), catalog );

    auto currentFileIndex = -1;
    for ( auto& window : restored->appSession->windowSessions() ) {
        if ( window.windowId() == LargeLogFileSession::WindowId ) {
            window.restore( RecordingViews::factory( restored->built ), &currentFileIndex );
        }
    }
    REQUIRE( currentFileIndex >= 0 );

    // The connections go with this object.
    QObject waiting;
    QEventLoop loop;
    auto remaining = 0;
    for ( auto tab = 0; tab < static_cast<int>( restored->built.size() ); ++tab ) {
        if ( loaded == Loaded::CurrentTab && tab != currentFileIndex ) {
            continue;
        }
        ++remaining;
        QObject::connect( restored->built[ static_cast<size_t>( tab ) ]->build().openLogFile.get(),
                          &OpenLogFile::loadingFinished, &waiting, [ &remaining, &loop ] {
                              if ( --remaining == 0 ) {
                                  loop.quit();
                              }
                          } );
    }
    loop.exec();

    return restored;
}

} // namespace

TEST_CASE( "Restoring a Session of several large Log Files", "[session-restore-benchmark]" )
{
    LargeLogFileSession stored;
    const auto catalog = std::make_shared<LogFormatCatalog>();

    REQUIRE( restoreUntilLoaded( catalog, Loaded::EveryTab )->built.size()
             == static_cast<size_t>( stored.fileCount() ) );

    // Restores the Session once per run. The last run's Session is torn down
    // after the measurement; with more than one run per sample, a run tears
    // down the one before it first, so that Log Files still loading in the
    // background do not compete with it. Log Files this large take one run
    // per sample.
    const auto measureRestore = [ &catalog ]( Catch::Benchmark::Chronometer& meter,
                                              Loaded loaded ) {
        std::vector<std::unique_ptr<RestoredSession>> runs( static_cast<size_t>( meter.runs() ) );
        meter.measure( [ &runs, &catalog, loaded ]( int run ) {
            const auto index = static_cast<size_t>( run );
            if ( index > 0 ) {
                runs[ index - 1 ].reset();
            }
            runs[ index ] = restoreUntilLoaded( catalog, loaded );
            return runs[ index ]->built.size();
        } );
    };

    BENCHMARK_ADVANCED( "large Log Files: restore until the current tab has loaded" )(
        Catch::Benchmark::Chronometer meter )
    {
        measureRestore( meter, Loaded::CurrentTab );
    };

    BENCHMARK_ADVANCED( "large Log Files: restore until every tab has loaded" )(
        Catch::Benchmark::Chronometer meter )
    {
        measureRestore( meter, Loaded::EveryTab );
    };
}

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
    // The test cases run beside a settings file of this process's own, so
    // that no test binary reads or writes the one in the build directory and
    // no case inherits what an earlier one left behind (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

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
