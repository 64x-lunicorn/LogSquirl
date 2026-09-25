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

// Scrolling a Table View over a Log File of 10 million Log Lines (#462): the
// Rows of a viewport read, extracted and painted, one page after another
// (scrolling down) and after a jump to somewhere else in the Log File. The
// Log Format has a timestamp field, so with the elapsed-time column the Rows
// also look back for the Timestamp of the previous Log Line.
//
// Only what the Table View offered before #462 is used, so this file builds
// unchanged on both sides of an A/B comparison (see README.md). Run it in an
// optimized build. Writes the Log File at run time into a temporary file.
// LOGSQUIRL_BENCHMARK_LOG_LINES writes fewer Log Lines, for a quick run.

#include "configuration.h"
#include "highlighterset.h"
#include "isolated_settings.h"
#include "logdata.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "persistentinfo.h"
#include "test_policies.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScrollBar>
#include <QTemporaryFile>
#include <QTimer>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

constexpr int VisibleRows = 50;

qint64 lineCount()
{
    bool isNumber = false;
    const auto requested = qgetenv( "LOGSQUIRL_BENCHMARK_LOG_LINES" ).toLongLong( &isNumber );
    return ( isNumber && requested > 0 ) ? requested : 10'000'000;
}

// Every twentieth Log Line is a stack trace line without a Timestamp.
std::string logLine( qint64 index )
{
    if ( index % 20 == 19 ) {
        return "    at com.example.Handler.run(Handler.java:42)\n";
    }
    const auto ms = index * 25;
    const auto inDay = ms % 86'400'000;
    char buffer[ 128 ];
    std::snprintf( buffer, sizeof( buffer ),
                   "2026-09-%02d %02d:%02d:%02d.%03d %s request %lld handled in time\n",
                   static_cast<int>( 1 + ms / 86'400'000 % 28 ),
                   static_cast<int>( inDay / 3'600'000 ), static_cast<int>( inDay / 60'000 % 60 ),
                   static_cast<int>( inDay / 1000 % 60 ), static_cast<int>( inDay % 1000 ),
                   index % 13 == 0 ? "WARN" : "INFO", static_cast<long long>( index ) );
    return buffer;
}

LogFormatDefinition benchmarkFormat()
{
    LogFormatDefinition format;
    format.setName( "tableview_scroll_benchmark" );
    format.setTitle( "Table View scroll benchmark" );
    QHash<QString, QString> regex;
    regex[ "std" ] = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) )"
                     R"((?<level>[A-Z]+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setLevelField( "level" );
    format.setBodyField( "body" );
    return format;
}

double median( std::vector<double> values )
{
    std::sort( values.begin(), values.end() );
    return values[ values.size() / 2 ];
}

} // namespace

TEST_CASE( "Scrolling a Table View over 10 million Log Lines", "[tableview-scroll-benchmark]" )
{
    const auto lines = lineCount();

    QTemporaryFile file( "tableview_scroll_benchmark_XXXXXX" );
    REQUIRE( file.open() );
    {
        std::string block;
        for ( qint64 line = 0; line < lines; ++line ) {
            block += logLine( line );
            if ( block.size() > ( 1 << 20 ) ) {
                file.write( block.data(), static_cast<qint64>( block.size() ) );
                block.clear();
            }
        }
        file.write( block.data(), static_cast<qint64>( block.size() ) );
        file.flush();
    }

    auto policies = testSettingsPolicies();
    LogData logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding );
    {
        QEventLoop loop;
        QObject::connect( &logData, &LogData::loadingFinished, &loop, &QEventLoop::quit );
        QTimer::singleShot( 600'000, &loop, &QEventLoop::quit );
        logData.attachFile( file.fileName() );
        loop.exec();
    }
    REQUIRE( logData.getNbLine() == LinesCount( static_cast<uint64_t>( lines ) ) );

    const auto format = benchmarkFormat();
    LogTableView view;
    view.setFrameShape( QFrame::NoFrame );
    const auto viewPolicies = testSettingsPolicies();
    view.setDecorationPolicy( viewPolicies.decoration );
    view.setPresentationPolicy( viewPolicies.presentation );
    view.updateFont( QApplication::font() );
    view.setLogFormat( &format, &logData );
    view.updateData( nullptr, false );
    view.setActive( true );
    view.resize( 1400, 400 );
    view.show();
    QCoreApplication::processEvents();
    view.resize( 1400,
                 view.height() - view.viewport()->height() + VisibleRows * view.rowHeight( 0 ) );
    QCoreApplication::processEvents();
    REQUIRE( view.model()->rowCount() == lines );

    QElapsedTimer timer;
    const auto scrollTo = [ & ]( int value ) {
        view.verticalScrollBar()->setValue( value );
        view.viewport()->repaint();
    };

    // Down the Log File, a page at a time: every Row is new to the caches.
    std::vector<double> pageMilliseconds;
    for ( int round = 0; round < 5; ++round ) {
        const int start = static_cast<int>( ( lines / 7 ) * ( round + 1 ) );
        scrollTo( start );
        timer.restart();
        for ( int page = 1; page <= 200; ++page ) {
            scrollTo( start + page * VisibleRows );
        }
        pageMilliseconds.push_back( static_cast<double>( timer.nsecsElapsed() ) / 1e6 / 200 );
    }

    // Up the Log File, a page at a time: the previous Log Line is read after its Row.
    std::vector<double> pageUpMilliseconds;
    for ( int round = 0; round < 5; ++round ) {
        const int start = static_cast<int>( ( lines / 7 ) * ( round + 1 ) ) + 100'000;
        scrollTo( start );
        timer.restart();
        for ( int page = 1; page <= 200; ++page ) {
            scrollTo( start - page * VisibleRows );
        }
        pageUpMilliseconds.push_back( static_cast<double>( timer.nsecsElapsed() ) / 1e6 / 200 );
    }

    // Jumps to somewhere else in the Log File.
    std::vector<double> jumpMilliseconds;
    for ( int round = 0; round < 5; ++round ) {
        timer.restart();
        for ( int jump = 1; jump <= 200; ++jump ) {
            scrollTo(
                static_cast<int>( ( static_cast<qint64>( jump ) * 2654435761LL + round * 7919 )
                                  % ( lines - VisibleRows ) ) );
        }
        jumpMilliseconds.push_back( static_cast<double>( timer.nsecsElapsed() ) / 1e6 / 200 );
    }

    std::cout << "columns: " << view.model()->columnCount() << "\n"
              << "scroll down, one page: median " << median( pageMilliseconds ) << " ms\n"
              << "scroll up, one page: median " << median( pageUpMilliseconds ) << " ms\n"
              << "jump, one viewport: median " << median( jumpMilliseconds ) << " ms\n";
}

int main( int argc, char* argv[] )
{
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );
    Configuration::getSynced();
    HighlighterSetCollection::getSynced();
    return Catch::Session().run( argc, argv );
}
