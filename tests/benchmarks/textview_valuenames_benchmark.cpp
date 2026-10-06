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

// What a Selection drag and a double-click cost a shown Text View with Value
// Names shown (#745): every Log Line on screen holds Named Values, named by 22
// Naming Rules, and the mouse goes through the view's own events.
//
// Only what the text view offered at #744 is used, so the same file measures
// both sides of an A/B comparison (see README.md).

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "datalocation.h"
#include "highlighterset.h"
#include "quickfindpattern.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "valuenames_fixture.h"

#include <QApplication>
#include <QMouseEvent>

#include <algorithm>
#include <cstdint>
#include <optional>

#include "instruction_count.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "isolated_settings.h"

// The settings library, which the UI library links, asks every executable.
const bool DataLocation::ForcePortable = true;

namespace {

using logsquirl::valuenames::GroupTable;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;

constexpr std::uint64_t LogLineCount = 100'000;
constexpr int KeyRules = 20;

// Log Line number index: the example's ECU and Id values, and four of the 20
// key=value pairs, all of the same width, so every Log Line is laid out alike.
QString namedLogLine( std::uint64_t index )
{
    const auto key = []( std::uint64_t number ) {
        return QStringLiteral( "key%1=%2" )
            .arg( number % KeyRules, 2, 10, QLatin1Char( '0' ) )
            .arg( 1 + number % 3 );
    };
    return QStringLiteral( "2026-10-05 12:34:56.%1 [worker-%2] BAP << ECU 0x15 0x14 id=7 %3 %4 "
                           "%5 %6 request handled by the dispatcher of the gateway" )
        .arg( index % 1000, 3, 10, QLatin1Char( '0' ) )
        .arg( index % 8 )
        .arg( key( index ), key( index + 5 ), key( index + 11 ), key( index + 17 ) );
}

// The example's group, and one of 20 rules "keyNN=(\d)", each with a Name
// Table of its own.
QList<NamingGroup> namingGroups()
{
    auto keys = NamingGroup::createNewGroup( QStringLiteral( "Keys" ) );
    QList<NamingRule> rules;
    QList<NameTable> tables;
    for ( int number = 0; number < KeyRules; ++number ) {
        const auto table = QStringLiteral( "Key %1" ).arg( number );
        NamingRule rule;
        rule.name = QStringLiteral( "key %1" ).arg( number );
        rule.pattern = QStringLiteral( "key%1=(\\d)" ).arg( number, 2, 10, QLatin1Char( '0' ) );
        rule.groupTables = { GroupTable{ QStringLiteral( "1" ), table } };
        rules.push_back( rule );
        tables.push_back(
            NameTable{ table,
                       { NameRow{ QStringLiteral( "1" ), QStringLiteral( "one" ) },
                         NameRow{ QStringLiteral( "2" ), QStringLiteral( "two" ) },
                         NameRow{ QStringLiteral( "3" ), QStringLiteral( "three" ) } } } );
    }
    keys.setRules( rules );
    keys.setTables( tables );
    return { valuenamesfixture::exampleGroup(), keys };
}

class NamedLogData final : public AbstractLogData {
protected:
    QString doGetLineString( LineNumber line ) const override
    {
        return namedLogLine( line.get() );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return untabify( namedLogLine( line.get() ) );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> lines;
        const auto end = std::min( first.get() + count.get(), LogLineCount );
        for ( auto line = first.get(); line < end; ++line ) {
            lines.push_back( namedLogLine( line ) );
        }
        return lines;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        auto lines = doGetLines( first, count );
        for ( auto& line : lines ) {
            line = untabify( std::move( line ) );
        }
        return lines;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( LogLineCount );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( 200 );
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength(
            static_cast<LineLength::UnderlyingType>( namedLogLine( line.get() ).size() ) );
    }
    const TextEncoding* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}
};

void sendMouse( AbstractLogView& view, QEvent::Type type, QPointF pos, Qt::MouseButton button,
                Qt::MouseButtons buttons )
{
    QMouseEvent event( type, pos, view.viewport()->mapToGlobal( pos ), button, buttons,
                       Qt::NoModifier );
    QCoreApplication::sendEvent( view.viewport(), &event );
}

void doubleClick( AbstractLogView& view, QPointF pos )
{
    sendMouse( view, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton );
    sendMouse( view, QEvent::MouseButtonDblClick, pos, Qt::LeftButton, Qt::LeftButton );
    sendMouse( view, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton );
}

// The middle of a Visual Line of the Viewport, from the height of one.
qreal visualLineY( int visualLine, int visualLineHeight )
{
    return visualLine * visualLineHeight + visualLineHeight / 2.0;
}

} // namespace

TEST_CASE( "text view Value Names interaction benchmarks", "[textview-valuenames-benchmark]" )
{
    const valuenamesfixture::ScopedValueNames valueNames{ namingGroups() };
    NamedLogData logData;
    const QuickFindPattern quickFindPattern;
    AbstractLogView view( &logData, &quickFindPattern, false );
    view.setFrameShape( QFrame::NoFrame );
    view.resize( 1200, 600 );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    view.setPresentationPolicy( testSettingsPolicies().presentation );
    view.updateData();
    view.valueNamesShownSet( true );
    REQUIRE( view.showsValueNames() );
    view.viewport()->repaint();

    const int visualLineHeight = std::max( view.fontMetrics().height(), 1 );
    // The first point, from the left, where a double-click on the first
    // Visual Line selects Beispiel(0x15): the Log Lines are laid out alike,
    // each one Visual Line, so it is on that Named Value on every Visual Line.
    std::optional<qreal> onNamedValue;
    for ( int x = 0; x < view.viewport()->width() && !onNamedValue.has_value(); ++x ) {
        doubleClick( view, QPointF( x, visualLineY( 0, visualLineHeight ) ) );
        if ( view.getSelectedTextAsShown() == QStringLiteral( "Beispiel(0x15)" ) ) {
            onNamedValue = x + 2;
        }
    }
    REQUIRE( onNamedValue.has_value() );
    const int visualLines = view.viewport()->height() / visualLineHeight;
    REQUIRE( visualLines >= 20 );
    doubleClick( view, QPointF( *onNamedValue, visualLineY( 19, visualLineHeight ) ) );
    REQUIRE( view.selectedLogLines() == logsquirl::vector<LineNumber>{ 19_lnum } );
    REQUIRE( view.getSelectedTextAsShown() == QStringLiteral( "Beispiel(0x15)" ) );

    // From the start of the text to past the last Named Value, a few pixels
    // per move, the way a mouse moves.
    const auto dragAlong = [ & ]( int visualLine, int moves, bool paint ) {
        const auto y = visualLineY( visualLine, visualLineHeight );
        sendMouse( view, QEvent::MouseButtonPress, QPointF( 40, y ), Qt::LeftButton,
                   Qt::LeftButton );
        for ( int move = 1; move <= moves; ++move ) {
            sendMouse( view, QEvent::MouseMove, QPointF( 40 + move * 1000.0 / moves, y ),
                       Qt::NoButton, Qt::LeftButton );
            if ( paint ) {
                view.viewport()->repaint();
            }
        }
        sendMouse( view, QEvent::MouseButtonRelease, QPointF( 1040, y ), Qt::LeftButton,
                   Qt::NoButton );
    };

    dragAlong( 3, 10, false );
    REQUIRE( view.getSelectedTextAsShown().contains( QStringLiteral( "Beispiel(0x15)" ) ) );

    BENCHMARK( "drag: a Selection along a Log Line with Named Values, 200 mouse moves" )
    {
        dragAlong( 5, 200, false );
        return view.getSelectedText().size();
    };

    BENCHMARK( "drag: a Selection along a Log Line with Named Values, 20 mouse moves, each "
               "painted" )
    {
        dragAlong( 7, 20, true );
        return view.getSelectedText().size();
    };

    BENCHMARK( "double-click: a Named Value on each of 20 Log Lines" )
    {
        for ( int visualLine = 0; visualLine < 20; ++visualLine ) {
            doubleClick( view,
                         QPointF( *onNamedValue, visualLineY( visualLine, visualLineHeight ) ) );
        }
        return view.getSelectedText().size();
    };
}

int main( int argc, char* argv[] )
{
    // The test cases run beside a settings file of this process's own (#370).
    if ( const auto launcherExitCode = isolated_settings::relaunchWithOwnSettings( argc, argv ) ) {
        return *launcherExitCode;
    }

    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    Configuration::getSynced();
    HighlighterSetCollection::getSynced();

    return Catch::Session().run( argc, argv );
}
