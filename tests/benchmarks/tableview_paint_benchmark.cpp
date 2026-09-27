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

// Benchmarks for painting a shown Table View (#294): a viewport of 50 Rows
// and 6 columns with a Highlighter Set active, the mouse moving from one Row
// to the next, and the in-cell character hit test on a long cell.
//
// Only what the Table View and its delegate offered before #294 is used, and
// nothing from tests/helpers but fake_log_data.h and test_policies.h, so this
// file builds unchanged on both sides of an A/B comparison. See
// tests/benchmarks/README.md.

#include "configuration.h"
#include "fake_log_data.h"
#include "highlighterset.h"
#include "logformatdefinition.h"
#include "logtablehighlightdelegate.h"
#include "logtableview.h"
#include "persistentinfo.h"
#include "test_policies.h"

#include <QApplication>
#include <QFontMetrics>
#include <QMouseEvent>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "isolated_settings.h"

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

constexpr int VisibleRows = 50;
constexpr int LogLineCount = 10'000;

// Six fields: timestamp, host, level, thread, component and message.
LogFormatDefinition sixFieldFormat()
{
    LogFormatDefinition format;
    format.setName( "tableview_paint_benchmark" );
    format.setTitle( "Table View paint benchmark" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) )"
                       R"((?<host>\S+) (?<level>[A-Z]+) \[(?<thread>[^\]]+)\] )"
                       R"((?<component>\S+): (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );

    QHash<QString, LogFormatValueDef> values;
    values[ "host" ] = LogFormatValueDef{ "string", true, false };
    values[ "level" ] = LogFormatValueDef{ "string", true, false };
    values[ "thread" ] = LogFormatValueDef{ "string", true, false };
    values[ "component" ] = LogFormatValueDef{ "string", true, false };
    format.setValueDefinitions( values );

    return format;
}

QStringList generatedLogLines()
{
    const QStringList levels = { "INFO", "DEBUG", "WARN", "ERROR", "INFO", "TRACE" };
    const QStringList components = { "net.http", "db.pool", "cache", "auth.session", "scheduler" };
    QStringList lines;
    lines.reserve( LogLineCount );
    for ( int line = 0; line < LogLineCount; ++line ) {
        lines << QString( "2026-09-17 12:%1:%2.%3 host-%4 %5 [worker-%6] %7: request %8 took %9 ms "
                          "for user id=%10 with status code %11 and payload of %12 bytes" )
                     .arg( line / 3600 % 60, 2, 10, QChar( '0' ) )
                     .arg( line / 60 % 60, 2, 10, QChar( '0' ) )
                     .arg( line % 1000, 3, 10, QChar( '0' ) )
                     .arg( line % 7 )
                     .arg( levels[ line % levels.size() ] )
                     .arg( line % 12 )
                     .arg( components[ line % components.size() ] )
                     .arg( line )
                     .arg( line * 37 % 997 )
                     .arg( line * 13 % 5000 )
                     .arg( line % 9 == 0 ? 500 : 200 )
                     .arg( line * 101 % 65536 );
    }
    return lines;
}

// A Highlighter Set of whole-line and word-only Highlighters, as a user
// typically has one, made the active one.
void activateHighlighterSet()
{
    auto set = HighlighterSet::createNewSet( "tableview_paint_benchmark" );
    set.addHighlighter( Highlighter{ "ERROR", false, false, Qt::white, Qt::darkRed } );
    set.addHighlighter( Highlighter{ "WARN", false, false, Qt::black, Qt::yellow } );
    set.addHighlighter( Highlighter{ "status code 5\\d\\d", false, false, Qt::white, Qt::red } );
    set.addHighlighter( Highlighter{ "user id=\\d+", false, true, Qt::black, Qt::cyan } );
    set.addHighlighter( Highlighter{ "worker-\\d+", false, true, Qt::white, Qt::darkBlue } );
    set.addHighlighter( Highlighter{ "\\d+ ms", false, true, Qt::black, Qt::green } );

    auto& collection = HighlighterSetCollection::get();
    auto sets = collection.highlighterSets();
    sets.append( set );
    collection.setHighlighterSets( sets );
    collection.deactivateAll();
    collection.activateSet( set.id() );
}

void moveMouseTo( LogTableView& view, const QPoint& position )
{
    QMouseEvent move( QEvent::MouseMove, position, view.viewport()->mapToGlobal( position ),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &move );
}

} // namespace

TEST_CASE( "table view paint benchmarks", "[tableview-paint-benchmark]" )
{
    activateHighlighterSet();

    const auto format = sixFieldFormat();
    FakeLogData logData( generatedLogLines() );

    LogTableView view;
    view.setFrameShape( QFrame::NoFrame );
    const auto policies = testSettingsPolicies();
    view.setDecorationPolicy( policies.decoration );
    view.setPresentationPolicy( policies.presentation );
    view.updateFont( QApplication::font() );
    view.setLogFormat( &format, &logData );
    view.updateData( false );
    view.setActive( true );
    view.resize( 1400, 400 );
    view.show();
    QCoreApplication::processEvents();

    // Exactly VisibleRows Rows fill the viewport; the horizontal scrollbar
    // coming or going changes its height, so it is shrunk to fit.
    view.resize( 1400,
                 view.height() - view.viewport()->height() + VisibleRows * view.rowHeight( 0 ) );
    QCoreApplication::processEvents();
    for ( int shrink = 0;
          shrink < 200 && view.rowAt( view.viewport()->height() - 1 ) >= VisibleRows; ++shrink ) {
        view.resize( 1400, view.height() - 1 );
        QCoreApplication::processEvents();
    }
    REQUIRE( view.model()->columnCount() >= 6 );
    REQUIRE( view.rowAt( view.viewport()->height() - 1 ) == VisibleRows - 1 );

    view.viewport()->repaint();

    BENCHMARK( "paint: a viewport of 50 Rows and 6 columns" )
    {
        view.viewport()->repaint();
        return view.viewport()->height();
    };

    const auto rowCenter
        = [ & ]( int row ) { return view.visualRect( view.model()->index( row, 2 ) ).center(); };
    moveMouseTo( view, rowCenter( 20 ) );
    QCoreApplication::processEvents();

    BENCHMARK( "hover: the mouse moves to the next Row and back, each painted" )
    {
        moveMouseTo( view, rowCenter( 21 ) );
        QCoreApplication::processEvents();
        moveMouseTo( view, rowCenter( 20 ) );
        QCoreApplication::processEvents();
        return view.viewport()->height();
    };

    QString longCell;
    while ( longCell.size() < 4000 ) {
        longCell += "request took 37 ms for user id=4711 with status code 200; ";
    }
    const QFontMetrics fontMetrics( view.font() );
    const int cellLeft = 10;
    const int longCellWidth = fontMetrics.horizontalAdvance( longCell );

    BENCHMARK( "hit test: a character in the middle and at the end of a 4000 character cell" )
    {
        return LogTableHighlightDelegate::charIndexAtX( longCell, fontMetrics, cellLeft,
                                                        cellLeft + longCellWidth / 2 )
               + LogTableHighlightDelegate::charIndexAtX( longCell, fontMetrics, cellLeft,
                                                          cellLeft + longCellWidth - 2 );
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

    // What the Table View reads for its painting.
    Configuration::getSynced();
    HighlighterSetCollection::getSynced();

    return Catch::Session().run( argc, argv );
}
