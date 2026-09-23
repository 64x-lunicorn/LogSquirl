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

#include "fake_file_watch.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "openlogfile.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QTemporaryDir>
#include <QTextCodec>

#include <algorithm>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// What a Log File changing on disk means is decided by the Open Log File;
// these tests follow real Log Files on disk without any widget. It hears of a
// change through the File Watch Port it is built with (#249): here a fake one,
// which reports the change at once, so no test waits on a watcher.

namespace {

using Phase = SearchSession::Phase;
using AutoRefreshState = SearchAutoRefresh::State;

constexpr auto FirstLineCount = 30;

// Log Lines numbered from firstNumber; every third one says "fizz".
QByteArray logLines( int count, int firstNumber = 0 )
{
    QByteArray lines;
    for ( auto number = firstNumber; number < firstNumber + count; ++number ) {
        lines += QString( "SOURCE line %1 %2\n" )
                     .arg( number, 6, 10, QChar( '0' ) )
                     .arg( number % 3 == 0 ? "fizz" : "buzz" )
                     .toUtf8();
    }
    return lines;
}

bool writeLogFile( const QString& path, int count )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    const auto lines = logLines( count );
    return file.write( lines ) == lines.size();
}

// How many of the first count Log Lines say "fizz".
LinesCount fizzCount( int count )
{
    return LinesCount( static_cast<LinesCount::UnderlyingType>( ( count + 2 ) / 3 ) );
}

// A Catalog whose one Log Format every Log Line written above matches.
std::shared_ptr<LogFormatCatalog> catalogRecognizingTheLogLines()
{
    LogFormatDefinition format;
    format.setName( "openlogfile_test" );
    format.setTitle( "Open Log File test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<source>SOURCE)\s+(?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setBodyField( "body" );

    auto catalog = std::make_shared<LogFormatCatalog>();
    catalog->addFormat( format );
    return catalog;
}

// Follows everything an Open Log File tells about a Log File.
struct Observer {
    explicit Observer( OpenLogFile& openLogFile )
    {
        QObject::connect(
            &openLogFile, &OpenLogFile::loadingFinished,
            [ this ]( const OpenLogFile::LoadFinished& load ) { loads.push_back( load ); } );
        QObject::connect( &openLogFile, &OpenLogFile::truncated,
                          [ this ]( const QString& ) { ++truncations; } );
        QObject::connect(
            &openLogFile, &OpenLogFile::searchUpdated,
            [ this ]( const SearchSession::State& state ) { searchStates.push_back( state ); } );
    }

    bool waitLoads( size_t count )
    {
        return waitUiState( [ this, count ] { return loads.size() >= count; }, 30000 );
    }

    std::vector<OpenLogFile::LoadFinished> loads;
    int truncations = 0;
    std::vector<SearchSession::State> searchStates;
};

struct OpenedLogFile {
    OpenedLogFile( const QString& path, const logsquirl::vector<LineNumber>& savedMarks = {} )
        : openLogFile( policies.indexing, policies.search, policies.fileAccess, policies.decoding,
                       RecognitionPolicy{ .enabled = true }, catalogRecognizingTheLogLines(),
                       fileWatch )
        , observer( openLogFile )
    {
        openLogFile.restoreMarks( savedMarks );
        openLogFile.open( path );
    }

    LinesCount nbLines() const
    {
        return openLogFile.logData()->getNbLine();
    }

    SearchSession::State searchState() const
    {
        return openLogFile.filteredData()->searchState();
    }

    bool waitSearchSettled()
    {
        return waitUiState(
            [ this ] {
                const auto phase = searchState().phase;
                return phase == Phase::Complete || phase == Phase::Failed;
            },
            30000 );
    }

    QList<LineNumber> marks() const
    {
        return openLogFile.filteredData()->getMarks();
    }

    SettingsPolicies policies = testSettingsPolicies();
    std::shared_ptr<FakeFileWatch> fileWatch = std::make_shared<FakeFileWatch>();
    OpenLogFile openLogFile;
    Observer observer;
};

} // namespace

SCENARIO( "The auto-refresh state machine decides whether a Search follows the Log File",
          "[openlogfile][autorefresh]" )
{
    SearchAutoRefresh autoRefresh;

    THEN( "no Search is active at first, and none is followed" )
    {
        REQUIRE( autoRefresh.state() == AutoRefreshState::NoSearch );
        REQUIRE_FALSE( autoRefresh.isAutoRefreshAllowed() );
        REQUIRE_FALSE( autoRefresh.isFileTruncated() );
    }

    WHEN( "a Search starts without auto-refresh asked for" )
    {
        autoRefresh.startSearch();

        THEN( "it is static" )
        {
            REQUIRE( autoRefresh.state() == AutoRefreshState::Static );
            REQUIRE_FALSE( autoRefresh.isAutoRefreshAllowed() );
        }

        AND_WHEN( "auto-refresh is asked for" )
        {
            autoRefresh.setAutoRefresh( true );

            THEN( "the Search is refreshed" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::Autorefreshing );
                REQUIRE( autoRefresh.isAutoRefreshAllowed() );
            }
        }

        AND_WHEN( "the Log File is truncated" )
        {
            autoRefresh.truncateFile();

            THEN( "it is truncated and not refreshed" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::FileTruncated );
                REQUIRE( autoRefresh.isFileTruncated() );
                REQUIRE_FALSE( autoRefresh.isAutoRefreshAllowed() );
            }

            AND_WHEN( "auto-refresh is asked for afterwards" )
            {
                autoRefresh.setAutoRefresh( true );

                THEN( "only a new Search follows the Log File again" )
                {
                    REQUIRE( autoRefresh.state() == AutoRefreshState::FileTruncated );

                    autoRefresh.startSearch();
                    REQUIRE( autoRefresh.state() == AutoRefreshState::Autorefreshing );
                }
            }
        }
    }

    WHEN( "a Search starts with auto-refresh asked for" )
    {
        autoRefresh.setAutoRefresh( true );
        REQUIRE( autoRefresh.state() == AutoRefreshState::NoSearch );
        autoRefresh.startSearch();

        THEN( "it is refreshed" )
        {
            REQUIRE( autoRefresh.state() == AutoRefreshState::Autorefreshing );
            REQUIRE( autoRefresh.isAutoRefreshRequested() );
        }

        AND_WHEN( "the pattern is changed" )
        {
            autoRefresh.changeExpression();

            THEN( "auto-refresh is suspended until the next Search" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::Static );
                autoRefresh.startSearch();
                REQUIRE( autoRefresh.state() == AutoRefreshState::Autorefreshing );
            }
        }

        AND_WHEN( "the Search is stopped" )
        {
            autoRefresh.stopSearch();

            THEN( "auto-refresh is suspended" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::Static );
            }
        }

        AND_WHEN( "the Log File is truncated" )
        {
            autoRefresh.truncateFile();

            THEN( "the Search is to start again once it has loaded" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::TruncatedAutorefreshing );
                REQUIRE( autoRefresh.isAutoRefreshAllowed() );
                REQUIRE( autoRefresh.isFileTruncated() );
            }

            AND_WHEN( "it is truncated once more" )
            {
                autoRefresh.truncateFile();

                THEN( "it still is" )
                {
                    REQUIRE( autoRefresh.state() == AutoRefreshState::TruncatedAutorefreshing );
                }
            }

            AND_WHEN( "auto-refresh is no longer asked for" )
            {
                autoRefresh.setAutoRefresh( false );

                THEN( "it is only truncated" )
                {
                    REQUIRE( autoRefresh.state() == AutoRefreshState::FileTruncated );
                }
            }

            AND_WHEN( "the pattern is changed" )
            {
                autoRefresh.changeExpression();

                THEN( "the Search still starts again" )
                {
                    REQUIRE( autoRefresh.state() == AutoRefreshState::TruncatedAutorefreshing );
                }
            }
        }

        AND_WHEN( "auto-refresh is no longer asked for" )
        {
            autoRefresh.setAutoRefresh( false );

            THEN( "the Search is static" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::Static );
            }
        }

        AND_WHEN( "the state is reset" )
        {
            autoRefresh.resetState();

            THEN( "no Search is active, and auto-refresh stays asked for" )
            {
                REQUIRE( autoRefresh.state() == AutoRefreshState::NoSearch );
                REQUIRE( autoRefresh.isAutoRefreshRequested() );
            }
        }
    }
}

SCENARIO( "An Open Log File follows a Log File that grows", "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "growing.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );
    REQUIRE( logFile.nbLines() == LinesCount( FirstLineCount ) );

    THEN( "the first load is loaded from its start and recognizes the Log Format" )
    {
        const auto& load = logFile.observer.loads.front();
        REQUIRE( load.status == LoadingStatus::Successful );
        REQUIRE( load.fromStart );
        REQUIRE_FALSE( load.onlyAppended );
        REQUIRE( load.formatRecognized );
        REQUIRE_FALSE( load.searchRestarted );
        REQUIRE( logFile.openLogFile.formatRecognitionCount() == 1 );
        REQUIRE( logFile.openLogFile.logFormat() != nullptr );
        REQUIRE( logFile.openLogFile.logFormat()->name() == "openlogfile_test" );
    }

    THEN( "the Search Limits are the whole Log File" )
    {
        REQUIRE( logFile.openLogFile.searchStartLine() == 0_lnum );
        REQUIRE( logFile.openLogFile.searchEndLine() == LineNumber( FirstLineCount ) );
    }

    GIVEN( "an auto-refreshed Search and a Mark" )
    {
        logFile.openLogFile.setAutoRefresh( true );
        const auto requested
            = logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
        REQUIRE( requested.phase != Phase::InvalidPattern );
        REQUIRE( logFile.waitSearchSettled() );
        REQUIRE( logFile.searchState().matchCount == fizzCount( FirstLineCount ) );
        REQUIRE( logFile.openLogFile.searchAutoRefresh().state()
                 == AutoRefreshState::Autorefreshing );
        REQUIRE( waitUiState( [ & ] { return !logFile.observer.searchStates.empty(); } ) );

        logFile.openLogFile.filteredData()->addMark( 1_lnum );

        WHEN( "Log Lines are added to the Log File" )
        {
            REQUIRE( logFile.fileWatch->grow( path, logLines( FirstLineCount, FirstLineCount ) ) );
            REQUIRE( logFile.observer.waitLoads( 2 ) );
            REQUIRE( logFile.nbLines() == LinesCount( 2 * FirstLineCount ) );
            REQUIRE( logFile.waitSearchSettled() );

            THEN( "the Search continues over the added Log Lines" )
            {
                const auto state = logFile.searchState();
                REQUIRE( state.isContinuation );
                REQUIRE( state.startLine == 0_lnum );
                REQUIRE( state.endLine == LineNumber( 2 * FirstLineCount ) );
                REQUIRE( state.matchCount == fizzCount( 2 * FirstLineCount ) );
                REQUIRE_FALSE( logFile.observer.loads.back().searchRestarted );
            }

            THEN( "the load is not from the start, and the Marks are kept" )
            {
                REQUIRE_FALSE( logFile.observer.loads.back().fromStart );
                REQUIRE( logFile.observer.loads.back().onlyAppended );
                REQUIRE( logFile.marks() == QList<LineNumber>{ 1_lnum } );
                REQUIRE( logFile.observer.truncations == 0 );
            }

            THEN( "Format Recognition is not taken again" )
            {
                REQUIRE_FALSE( logFile.observer.loads.back().formatRecognized );
                REQUIRE( logFile.openLogFile.formatRecognitionCount() == 1 );
                REQUIRE( logFile.openLogFile.logFormat() != nullptr );
            }

            THEN( "the Search Limits are the whole Log File again" )
            {
                REQUIRE( logFile.openLogFile.searchEndLine() == LineNumber( 2 * FirstLineCount ) );
            }
        }
    }

    GIVEN( "a Search that is not auto-refreshed" )
    {
        logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
        REQUIRE( logFile.waitSearchSettled() );

        WHEN( "Log Lines are added to the Log File" )
        {
            REQUIRE( logFile.fileWatch->grow( path, logLines( FirstLineCount, FirstLineCount ) ) );
            REQUIRE( logFile.observer.waitLoads( 2 ) );

            THEN( "the Search stays over the Log Lines it ran over" )
            {
                const auto state = logFile.searchState();
                REQUIRE( state.endLine == LineNumber( FirstLineCount ) );
                REQUIRE( state.matchCount == fizzCount( FirstLineCount ) );
            }
        }
    }
}

SCENARIO( "An Open Log File follows a Log File that is truncated", "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "truncated.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );

    GIVEN( "an auto-refreshed Search and Marks" )
    {
        logFile.openLogFile.setAutoRefresh( true );
        logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
        REQUIRE( logFile.waitSearchSettled() );
        logFile.openLogFile.filteredData()->addMark( 1_lnum );
        logFile.openLogFile.filteredData()->addMark( 4_lnum );

        // What the Open Log File holds when it tells of the truncation: the
        // Log File may have loaded again by the time the test looks.
        struct {
            bool told = false;
            bool marksCleared = false;
            bool logFormatForgotten = false;
            bool fileTruncated = false;
            Phase searchPhase = Phase::Running;
        } whenTruncated;
        // Gone before whenTruncated is, and the connection with it.
        QObject connectionContext;
        QObject::connect( &logFile.openLogFile, &OpenLogFile::truncated, &connectionContext,
                          [ & ]( const QString& ) {
                              whenTruncated.told = true;
                              whenTruncated.marksCleared = logFile.marks().isEmpty();
                              whenTruncated.logFormatForgotten
                                  = logFile.openLogFile.logFormat() == nullptr;
                              whenTruncated.fileTruncated
                                  = logFile.openLogFile.searchAutoRefresh().isFileTruncated();
                              whenTruncated.searchPhase = logFile.searchState().phase;
                          } );

        WHEN( "the Log File is truncated to fewer Log Lines" )
        {
            constexpr auto TruncatedLineCount = 10;
            REQUIRE( logFile.fileWatch->truncate( path, logLines( TruncatedLineCount ) ) );
            REQUIRE( waitUiState( [ & ] { return whenTruncated.told; }, 30000 ) );

            THEN( "the Marks are cleared, the Search dropped and the Log Format forgotten at once" )
            {
                REQUIRE( whenTruncated.marksCleared );
                REQUIRE( whenTruncated.logFormatForgotten );
                REQUIRE( whenTruncated.fileTruncated );
                REQUIRE( whenTruncated.searchPhase == Phase::Idle );
            }

            AND_WHEN( "it has loaded again" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return !logFile.observer.loads.empty()
                               && logFile.observer.loads.back().searchRestarted;
                    },
                    30000 ) );
                REQUIRE( logFile.nbLines() == LinesCount( TruncatedLineCount ) );
                REQUIRE( logFile.waitSearchSettled() );

                THEN( "the Search started again over the whole Log File" )
                {
                    const auto state = logFile.searchState();
                    REQUIRE_FALSE( state.isContinuation );
                    REQUIRE_FALSE( state.fromCache );
                    REQUIRE( state.pattern.pattern == "fizz" );
                    REQUIRE( state.startLine == 0_lnum );
                    REQUIRE( state.endLine == LineNumber( TruncatedLineCount ) );
                    REQUIRE( state.matchCount == fizzCount( TruncatedLineCount ) );
                    REQUIRE( logFile.openLogFile.searchAutoRefresh().state()
                             == AutoRefreshState::Autorefreshing );
                }

                THEN( "Format Recognition was taken again" )
                {
                    REQUIRE_FALSE( logFile.observer.loads.back().onlyAppended );
                    REQUIRE( logFile.observer.loads.back().formatRecognized );
                    REQUIRE( logFile.openLogFile.formatRecognitionCount() == 2 );
                    REQUIRE( logFile.openLogFile.logFormat() != nullptr );
                }

                THEN( "the Marks stay cleared" )
                {
                    REQUIRE( logFile.marks().isEmpty() );
                }
            }
        }
    }

    GIVEN( "no Search" )
    {
        WHEN( "the Log File is truncated" )
        {
            REQUIRE( logFile.fileWatch->truncate( path, logLines( 10 ) ) );
            REQUIRE( waitUiState( [ & ] { return logFile.observer.truncations > 0; }, 30000 ) );

            THEN( "no Search is active still" )
            {
                REQUIRE( logFile.openLogFile.searchAutoRefresh().state()
                         == AutoRefreshState::NoSearch );
            }
        }
    }
}

SCENARIO( "An Open Log File reloaded by hand starts over", "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "reloaded.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );

    GIVEN( "an auto-refreshed Search and a Mark" )
    {
        logFile.openLogFile.setAutoRefresh( true );
        logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
        REQUIRE( logFile.waitSearchSettled() );
        logFile.openLogFile.filteredData()->addMark( 2_lnum );

        WHEN( "the Log File is reloaded" )
        {
            logFile.openLogFile.reload();

            THEN( "the Search is dropped and the Marks are cleared at once" )
            {
                REQUIRE( logFile.searchState().phase == Phase::Idle );
                REQUIRE( logFile.openLogFile.searchAutoRefresh().state()
                         == AutoRefreshState::NoSearch );
                REQUIRE( logFile.marks().isEmpty() );
            }

            AND_WHEN( "it has loaded" )
            {
                REQUIRE( logFile.observer.waitLoads( 2 ) );

                THEN( "it was loaded from its start and recognized again" )
                {
                    const auto& load = logFile.observer.loads.back();
                    REQUIRE( load.fromStart );
                    REQUIRE_FALSE( load.onlyAppended );
                    REQUIRE( load.formatRecognized );
                    REQUIRE_FALSE( load.searchRestarted );
                    REQUIRE( logFile.openLogFile.formatRecognitionCount() == 2 );
                }

                THEN( "no Search runs and no Mark is back" )
                {
                    REQUIRE( logFile.searchState().phase == Phase::Idle );
                    REQUIRE( logFile.marks().isEmpty() );
                }
            }
        }
    }
}

SCENARIO( "The Marks saved with the Session are applied once, after the first load",
          "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "marks.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    GIVEN( "a Log File opened with saved Marks" )
    {
        OpenedLogFile logFile( path, { 3_lnum, 7_lnum } );

        THEN( "they are applied once it has loaded" )
        {
            REQUIRE( logFile.marks().isEmpty() );
            REQUIRE( logFile.observer.waitLoads( 1 ) );
            REQUIRE( logFile.marks() == QList<LineNumber>{ 3_lnum, 7_lnum } );
        }

        THEN( "until then, they are the Marks saving the Session again keeps" )
        {
            REQUIRE( logFile.openLogFile.marks() == QList<LineNumber>{ 3_lnum, 7_lnum } );
            REQUIRE( logFile.observer.waitLoads( 1 ) );
            logFile.openLogFile.filteredData()->deleteMark( 3_lnum );
            REQUIRE( logFile.openLogFile.marks() == QList<LineNumber>{ 7_lnum } );
        }

        WHEN( "a Mark is removed and Log Lines are added" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );
            logFile.openLogFile.filteredData()->deleteMark( 3_lnum );
            REQUIRE( logFile.fileWatch->grow( path, logLines( 5, FirstLineCount ) ) );
            REQUIRE( logFile.observer.waitLoads( 2 ) );

            THEN( "the saved Marks are not applied again" )
            {
                REQUIRE( logFile.marks() == QList<LineNumber>{ 7_lnum } );
            }
        }

        WHEN( "the Log File is reloaded" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );
            logFile.openLogFile.reload();
            REQUIRE( logFile.observer.waitLoads( 2 ) );

            THEN( "the saved Marks are not applied again" )
            {
                REQUIRE( logFile.observer.loads.back().fromStart );
                REQUIRE( logFile.marks().isEmpty() );
            }
        }
    }
}

SCENARIO( "An Open Log File keeps Searches and follows the current one", "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "searches.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );

    logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
    REQUIRE( logFile.waitSearchSettled() );
    const auto first = logFile.openLogFile.filteredData();

    WHEN( "another Search is started" )
    {
        const auto second = logFile.openLogFile.startAnotherSearch();

        THEN( "it is current and the first one keeps its results" )
        {
            REQUIRE( logFile.openLogFile.filteredData() == second );
            REQUIRE( second != first );
            REQUIRE( second->searchState().phase == Phase::Idle );
            REQUIRE( first->searchState().matchCount == fizzCount( FirstLineCount ) );
        }

        AND_WHEN( "the second one is requested" )
        {
            logFile.observer.searchStates.clear();
            logFile.openLogFile.requestSearch( RegularExpressionPattern( "buzz" ) );
            REQUIRE( logFile.waitSearchSettled() );
            REQUIRE( waitUiState( [ & ] {
                return !logFile.observer.searchStates.empty()
                       && logFile.observer.searchStates.back().phase == Phase::Complete;
            } ) );

            THEN( "its updates are the ones told" )
            {
                REQUIRE( logFile.observer.searchStates.back().pattern.pattern == "buzz" );
            }

            AND_WHEN( "the first one is made current again" )
            {
                logFile.openLogFile.makeSearchCurrent( first );

                THEN( "it is the current Search" )
                {
                    REQUIRE( logFile.openLogFile.filteredData() == first );
                }
            }
        }
    }
}

SCENARIO( "An Open Log File follows a Log File replaced under its name", "[openlogfile]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "rotated.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );
    logFile.openLogFile.filteredData()->addMark( 1_lnum );

    WHEN( "another Log File is put under its name" )
    {
        constexpr auto ReplacementLineCount = 10;
        constexpr auto ReplacementFirstNumber = 1000;
        REQUIRE( logFile.fileWatch->replace(
            path, logLines( ReplacementLineCount, ReplacementFirstNumber ) ) );
        REQUIRE( waitUiState( [ & ] { return logFile.observer.truncations > 0; }, 30000 ) );
        REQUIRE( waitUiState(
            [ & ] {
                return logFile.observer.loads.size() >= 2
                       && logFile.nbLines() == LinesCount( ReplacementLineCount );
            },
            30000 ) );

        THEN( "it is taken as truncated and the new Log File is loaded" )
        {
            REQUIRE( logFile.marks().isEmpty() );
            REQUIRE(
                logFile.openLogFile.logData()->getLineString( 0_lnum ).contains( "line 001000" ) );
        }

        AND_WHEN( "the new Log File grows" )
        {
            const auto loadsBefore = logFile.observer.loads.size();
            REQUIRE( logFile.fileWatch->grow(
                path, logLines( 5, ReplacementFirstNumber + ReplacementLineCount ) ) );
            REQUIRE( logFile.observer.waitLoads( loadsBefore + 1 ) );

            THEN( "the Log Lines added to it are loaded" )
            {
                REQUIRE( logFile.nbLines() == LinesCount( ReplacementLineCount + 5 ) );
                REQUIRE( logFile.openLogFile.logData()->getLineString( 14_lnum ).contains(
                    "line 001014" ) );
            }
        }
    }
}

SCENARIO( "An Open Log File has its Log File watched from its first load until it is closed",
          "[openlogfile][filewatch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "watched.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    const auto policies = testSettingsPolicies();
    const auto fileWatch = std::make_shared<FakeFileWatch>();
    auto openLogFile = std::make_unique<OpenLogFile>(
        policies.indexing, policies.search, policies.fileAccess, policies.decoding,
        RecognitionPolicy{ .enabled = true }, catalogRecognizingTheLogLines(), fileWatch );
    Observer observer( *openLogFile );

    THEN( "nothing is watched before it has loaded" )
    {
        openLogFile->open( path );
        REQUIRE( fileWatch->watchedFiles().empty() );
    }

    GIVEN( "it has loaded" )
    {
        openLogFile->open( path );
        REQUIRE( observer.waitLoads( 1 ) );

        THEN( "its Log File is watched" )
        {
            REQUIRE( fileWatch->watchedFiles() == std::vector<QString>{ path } );
        }

        WHEN( "it is closed" )
        {
            openLogFile.reset();

            THEN( "its Log File is no longer watched" )
            {
                REQUIRE( fileWatch->watchedFiles().empty() );
            }
        }

        WHEN( "it is closed while a change it was told of is still on its way" )
        {
            REQUIRE( fileWatch->grow( path, logLines( 5, FirstLineCount ) ) );
            openLogFile.reset();

            THEN( "the change reaches nothing, and its Log File is no longer watched" )
            {
                QCoreApplication::processEvents();
                REQUIRE_FALSE( waitUiState( [ & ] { return observer.loads.size() > 1; }, 500 ) );
                REQUIRE( fileWatch->watchedFiles().empty() );
            }
        }

        WHEN( "a change to another watched file is reported" )
        {
            const auto otherPath = directory.filePath( "other.log" );
            REQUIRE( writeLogFile( otherPath, 5 ) );
            fileWatch->addFile( otherPath );
            REQUIRE( fileWatch->reportChange( otherPath ) );

            THEN( "its Log File is not loaded again" )
            {
                REQUIRE_FALSE( waitUiState( [ & ] { return observer.loads.size() > 1; }, 500 ) );
                REQUIRE( openLogFile->logData()->getNbLine() == LinesCount( FirstLineCount ) );
            }
        }
    }
}

SCENARIO( "A Search requested while the Log File loads runs once it has loaded",
          "[openlogfile][pendingsearch]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "loading.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    // Opened here, and the Search requested right after: nothing has been
    // loaded yet, as a load is only followed from the event loop.
    OpenedLogFile logFile( path );

    GIVEN( "a Search requested before the first load has finished" )
    {
        const auto requested
            = logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );

        THEN( "it is not run over the Log Lines not loaded yet" )
        {
            REQUIRE( requested.phase != Phase::InvalidPattern );
            REQUIRE( logFile.observer.searchStates.empty() );
            REQUIRE( logFile.searchState().phase == Phase::Idle );
        }

        WHEN( "the Log File has loaded" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );
            REQUIRE( logFile.waitSearchSettled() );

            THEN( "the Search ran over the whole Log File" )
            {
                const auto state = logFile.searchState();
                REQUIRE( state.phase == Phase::Complete );
                REQUIRE( state.startLine == 0_lnum );
                REQUIRE( state.endLine == LineNumber( FirstLineCount ) );
                REQUIRE( state.matchCount == fizzCount( FirstLineCount ) );
                REQUIRE( waitUiState( [ & ] {
                    return !logFile.observer.searchStates.empty()
                           && logFile.observer.searchStates.back().phase == Phase::Complete;
                } ) );
            }
        }
    }

    GIVEN( "an invalid pattern requested before the first load has finished" )
    {
        logFile.openLogFile.requestSearch( RegularExpressionPattern( "(" ) );

        WHEN( "the Log File has loaded" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );

            THEN( "the invalid pattern is told, and no Search is active" )
            {
                REQUIRE( waitUiState( [ & ] {
                    return !logFile.observer.searchStates.empty()
                           && logFile.observer.searchStates.back().phase == Phase::InvalidPattern;
                } ) );
                REQUIRE_FALSE( logFile.observer.searchStates.back().errorString.isEmpty() );
                REQUIRE( logFile.openLogFile.searchAutoRefresh().state()
                         == AutoRefreshState::NoSearch );
            }
        }
    }

    GIVEN( "a Search requested and cleared before the first load has finished" )
    {
        logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );
        logFile.openLogFile.clearSearch();

        WHEN( "the Log File has loaded" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );

            THEN( "no Search runs" )
            {
                // Clearing it is told; a Search running is not.
                REQUIRE_FALSE( waitUiState(
                    [ & ] {
                        const auto& states = logFile.observer.searchStates;
                        return std::any_of( states.begin(), states.end(), []( const auto& state ) {
                            return state.phase != Phase::Idle;
                        } );
                    },
                    500 ) );
                REQUIRE( logFile.searchState().phase == Phase::Idle );
            }
        }
    }
}

namespace {

// Log Lines with non-ASCII text, written as UTF-8: read as UTF-8 they say
// "Grüße", read as ISO-8859-1 they do not. Every other one says "hit".
constexpr auto EncodedLineCount = 40;

QString encodedLine( int number )
{
    return QString( "%1: Grüße aus München, line %2" )
        .arg( number % 2 == 0 ? "hit" : "miss" )
        .arg( number, 6, 10, QChar( '0' ) );
}

bool writeUtf8LogFile( const QString& path )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    QByteArray lines;
    for ( auto number = 0; number < EncodedLineCount; ++number ) {
        lines += encodedLine( number ).toUtf8() + '\n';
    }
    return file.write( lines ) == lines.size();
}

int mibOf( const char* encoding )
{
    return QTextCodec::codecForName( encoding )->mibEnum();
}

// The first Log Line as it reads in ISO-8859-1.
QString firstLineAsLatin1()
{
    return QString::fromLatin1( encodedLine( 0 ).toUtf8() );
}

struct EncodingObserver {
    explicit EncodingObserver( OpenLogFile& openLogFile )
    {
        QObject::connect( &openLogFile, &OpenLogFile::encodingChanged, [ this ] { ++changes; } );
    }

    int changes = 0;
};

} // namespace

SCENARIO( "An Open Log File reads its Log File in the Encoding detected unless one is chosen",
          "[openlogfile][encoding]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "encoded.log" );
    REQUIRE( writeUtf8LogFile( path ) );

    GIVEN( "no Encoding forced by the settings" )
    {
        OpenedLogFile logFile( path );
        EncodingObserver encoding( logFile.openLogFile );

        THEN( "none is chosen" )
        {
            REQUIRE_FALSE( logFile.openLogFile.chosenEncoding().has_value() );
        }

        WHEN( "the Log File has loaded" )
        {
            REQUIRE( logFile.observer.waitLoads( 1 ) );

            THEN( "it is read in the Encoding detected" )
            {
                REQUIRE( logFile.openLogFile.logData()->getDetectedEncoding() != nullptr );
                REQUIRE( logFile.openLogFile.encoding()->mibEnum()
                         == logFile.openLogFile.logData()->getDetectedEncoding()->mibEnum() );
                REQUIRE( logFile.openLogFile.encoding()->mibEnum() == mibOf( "UTF-8" ) );
                REQUIRE( logFile.openLogFile.logData()->getDisplayEncoding()->mibEnum()
                         == mibOf( "UTF-8" ) );
                REQUIRE( logFile.openLogFile.logData()->getLineString( 0_lnum )
                         == encodedLine( 0 ) );
                REQUIRE( encoding.changes == 1 );
            }

            AND_WHEN( "an Encoding is chosen" )
            {
                logFile.openLogFile.setEncoding( mibOf( "ISO-8859-1" ) );

                THEN( "it overrides the one detected" )
                {
                    REQUIRE( logFile.openLogFile.chosenEncoding() == mibOf( "ISO-8859-1" ) );
                    REQUIRE( logFile.openLogFile.encoding()->mibEnum() == mibOf( "ISO-8859-1" ) );
                    REQUIRE( logFile.openLogFile.logData()->getLineString( 0_lnum )
                             == firstLineAsLatin1() );
                    REQUIRE( encoding.changes == 2 );
                }

                AND_WHEN( "the one detected is asked for again" )
                {
                    logFile.openLogFile.setEncoding( std::nullopt );

                    THEN( "the Log File reads as it was detected" )
                    {
                        REQUIRE_FALSE( logFile.openLogFile.chosenEncoding().has_value() );
                        REQUIRE( logFile.openLogFile.logData()->getLineString( 0_lnum )
                                 == encodedLine( 0 ) );
                        REQUIRE( encoding.changes == 3 );
                    }
                }
            }

            AND_WHEN( "the Encoding it is read in already is chosen" )
            {
                logFile.openLogFile.setEncoding( mibOf( "UTF-8" ) );

                THEN( "nothing reads differently, and nothing is told" )
                {
                    REQUIRE( logFile.openLogFile.chosenEncoding() == mibOf( "UTF-8" ) );
                    REQUIRE( encoding.changes == 1 );
                }
            }
        }
    }

    GIVEN( "an Encoding forced by the settings" )
    {
        auto policies = testSettingsPolicies();
        policies.fileAccess.defaultEncodingMib = mibOf( "ISO-8859-1" );
        OpenLogFile openLogFile( policies.indexing, policies.search, policies.fileAccess,
                                 policies.decoding, RecognitionPolicy{}, nullptr, nullptr );
        Observer observer( openLogFile );
        openLogFile.open( path );

        THEN( "it is the one chosen from the start" )
        {
            REQUIRE( openLogFile.chosenEncoding() == mibOf( "ISO-8859-1" ) );
        }

        WHEN( "the Log File has loaded" )
        {
            REQUIRE( observer.waitLoads( 1 ) );

            THEN( "it is read in the Encoding forced, not in the one detected" )
            {
                REQUIRE( openLogFile.encoding()->mibEnum() == mibOf( "ISO-8859-1" ) );
                REQUIRE( openLogFile.logData()->getLineString( 0_lnum ) == firstLineAsLatin1() );
            }
        }
    }
}

SCENARIO( "Choosing another Encoding while a Search is shown reads its Log Lines anew",
          "[openlogfile][encoding]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "searched.log" );
    REQUIRE( writeUtf8LogFile( path ) );

    OpenedLogFile logFile( path );
    REQUIRE( logFile.observer.waitLoads( 1 ) );
    EncodingObserver encoding( logFile.openLogFile );

    logFile.openLogFile.requestSearch( RegularExpressionPattern( "^hit" ) );
    REQUIRE( logFile.waitSearchSettled() );
    REQUIRE( logFile.searchState().matchCount == LinesCount( EncodedLineCount / 2 ) );
    REQUIRE( logFile.openLogFile.filteredData()->getLineString( 0_lnum ) == encodedLine( 0 ) );

    WHEN( "another Encoding is chosen" )
    {
        logFile.openLogFile.setEncoding( mibOf( "ISO-8859-1" ) );

        THEN( "the Search's Log Lines read in it, and that they read differently is told" )
        {
            REQUIRE( logFile.openLogFile.filteredData()->getLineString( 0_lnum )
                     == firstLineAsLatin1() );
            REQUIRE( encoding.changes == 1 );
        }
    }

    WHEN( "the Search is kept, another one made current, and another Encoding chosen" )
    {
        const auto kept = logFile.openLogFile.filteredData();
        logFile.openLogFile.startAnotherSearch();
        logFile.openLogFile.setEncoding( mibOf( "ISO-8859-1" ) );

        THEN( "the kept Search's Log Lines read in it too" )
        {
            REQUIRE( kept->getLineString( 0_lnum ) == firstLineAsLatin1() );
            REQUIRE( encoding.changes == 1 );
        }
    }
}

SCENARIO( "An Open Log File is heard by a host that registers no meta types",
          "[openlogfile][metatypes]" )
{
    // This runner registers none of the types the Open Log File and its log
    // data carry through their queued signals: the library does (#394).
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "unregistered.log" );
    REQUIRE( writeLogFile( path, FirstLineCount ) );

    OpenedLogFile logFile( path );

    THEN( "the types those signals carry are known to the meta type system by name" )
    {
        for ( const auto* name :
              { "LoadingStatus", "MonitoredFileStatus", "LinesCount", "LineNumber", "LineLength",
                "SearchId", "SearchSession::State", "OpenLogFile::LoadFinished" } ) {
            INFO( name );
            REQUIRE( QMetaType::fromName( name ).isValid() );
        }
    }

    WHEN( "the Log File has loaded and a Search runs over it" )
    {
        REQUIRE( logFile.observer.waitLoads( 1 ) );
        REQUIRE( logFile.observer.loads.front().status == LoadingStatus::Successful );

        logFile.openLogFile.requestSearch( RegularExpressionPattern( "fizz" ) );

        THEN( "the Search's progress is heard until it is complete" )
        {
            REQUIRE( waitUiState(
                [ & ] {
                    return !logFile.observer.searchStates.empty()
                           && logFile.observer.searchStates.back().phase == Phase::Complete;
                },
                30000 ) );
            const auto& complete = logFile.observer.searchStates.back();
            REQUIRE( complete.progress == 100 );
            REQUIRE( complete.matchCount == fizzCount( FirstLineCount ) );
        }
    }
}
