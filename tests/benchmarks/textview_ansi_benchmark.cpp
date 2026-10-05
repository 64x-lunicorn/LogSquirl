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

// What Show colors costs a shown Text View (#573): one screen's Viewport
// content computed and painted, and a scroll one Visual Line at a time, on a
// million generated Log Lines with ANSI color sequences in every one -- under
// Hide, the reference, and under Show colors, which reads the Log Lines
// entering the Viewport with their colors, and resolves and paints them.

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "ansicolorsequences.h"
#include "configuration.h"
#include "datalocation.h"
#include "highlighterset.h"
#include "quickfindpattern.h"
#include "settingspolicies.h"
#include "shown_widget.h"
#include "test_policies.h"

#include <QApplication>
#include <QKeyEvent>
#include <QScrollBar>

#include <algorithm>
#include <cstdint>

#include "instruction_count.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "isolated_settings.h"

// The settings library, which the UI library links, asks every executable.
const bool DataLocation::ForcePortable = true;

namespace {

constexpr std::uint64_t LogLineCount = 1'000'000;

// Log Line number index as a colored logger writes it: a colored level, a
// colored source, a 256-color and a truecolor word and a bold one.
QString coloredLogLine( std::uint64_t index )
{
    const auto scrambled = ( index * 2654435761u ) % 1000u;
    QString line
        = QStringLiteral( "2026-09-17 12:34:56.%1 \x1B[3%2mINFO \x1B[0m [\x1B[36mworker-%3\x1B[0m] "
                          "\x1B[38;5;%4mrequest\x1B[39m %5 \x1B[48;2;40;40;%6mhandled\x1B[49m by "
                          "the \x1B[1mfrontend\x1B[0m" )
              .arg( static_cast<int>( index % 1000u ), 3, 10, QLatin1Char( '0' ) )
              .arg( static_cast<int>( index % 7u ) + 1 )
              .arg( static_cast<int>( index % 8u ) )
              .arg( static_cast<int>( 16 + scrambled % 216u ) )
              .arg( static_cast<int>( scrambled ) )
              .arg( static_cast<int>( scrambled % 256u ) );
    for ( std::uint64_t word = 0; word < scrambled % 12u; ++word ) {
        line += ( word % 3 == 2 ) ? QStringLiteral( "\t\x1B[35mvalue\x1B[0m" )
                                  : QStringLiteral( " payload" );
    }
    return line;
}

// The Log Lines as a Log File hiding their sequences reads them; with their
// colors, as it reads them for Show colors.
class ColoredLogData final : public AbstractLogData {
protected:
    QString doGetLineString( LineNumber line ) const override
    {
        auto text = coloredLogLine( line.get() );
        removeAnsiColorSequences( text );
        return text;
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return untabify( doGetLineString( line ) );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> lines;
        const auto end = std::min( first.get() + count.get(), LogLineCount );
        for ( auto line = first.get(); line < end; ++line ) {
            lines.push_back( doGetLineString( LineNumber( line ) ) );
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
    logsquirl::vector<AnsiColoredText> doGetAnsiColoredLines( LineNumber first,
                                                              LinesCount count ) const override
    {
        logsquirl::vector<AnsiColoredText> lines;
        const auto end = std::min( first.get() + count.get(), LogLineCount );
        for ( auto line = first.get(); line < end; ++line ) {
            lines.push_back( parseAnsiColorSequences( coloredLogLine( line ) ) );
        }
        return lines;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( LogLineCount );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( 400 );
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength(
            static_cast<LineLength::UnderlyingType>( doGetExpandedLineString( line ).size() ) );
    }
    const TextEncoding* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}
};

void pressKey( AbstractLogView& view, Qt::Key key )
{
    QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
    QCoreApplication::sendEvent( &view, &press );
}

} // namespace

TEST_CASE( "text view ANSI color benchmarks", "[textview-ansi-benchmark]" )
{
    ColoredLogData logData;
    const QuickFindPattern quickFindPattern;
    const auto showAnsiColors = GENERATE( false, true );
    const auto textWrap = GENERATE( false, true );
    const std::string mode = std::string( showAnsiColors ? "Show colors" : "Hide" )
                             + ( textWrap ? ", wrapped" : ", unwrapped" );

    AbstractLogView view( &logData, &quickFindPattern, textWrap );
    view.setFrameShape( QFrame::NoFrame );
    view.resize( 800, 600 );
    showUntilExposed( view );
    QCoreApplication::processEvents();
    const auto policies = testSettingsPolicies();
    view.setPresentationPolicy( policies.presentation );
    auto decoration = policies.decoration;
    decoration.showAnsiColors = showAnsiColors;
    view.setDecorationPolicy( decoration );
    view.updateData();
    view.verticalScrollBar()->setValue( view.verticalScrollBar()->maximum() / 2 );
    view.viewport()->repaint();

    // Every Log Line of the screen read, parsed, decorated and painted again.
    BENCHMARK( "one screen: Viewport content computed and painted, " + mode )
    {
        view.updateData();
        view.viewport()->repaint();
        return view.getTopLine();
    };

    BENCHMARK( "keys: 20 Visual Lines down and 20 up one at a time, each painted, " + mode )
    {
        for ( int step = 0; step < 20; ++step ) {
            pressKey( view, Qt::Key_Down );
            view.viewport()->repaint();
        }
        for ( int step = 0; step < 20; ++step ) {
            pressKey( view, Qt::Key_Up );
            view.viewport()->repaint();
        }
        return view.getTopLine();
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
