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

// Benchmarks for scrolling a shown, wrapped text view through Qt events: the
// wheel, the keys, the scrollbar, the Log File changing and the view resized,
// on a million generated Log Lines, with and without painting.
//
// Only the text view's public API that origin/master already had before #246
// is used, and nothing from tests/helpers but test_policies.h, so this file
// builds unchanged on both sides of an A/B comparison. See
// tests/benchmarks/README.md.

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <algorithm>

#include <QApplication>
#include <QKeyEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "generated_log_lines.h"
#include "highlighterset.h"
#include "persistentinfo.h"
#include "quickfindpattern.h"
#include "test_policies.h"

// The settings library, which the UI library links, asks every executable.
const bool PersistentInfo::ForcePortable = true;

namespace {

using namespace scrollingbenchmark;

class GeneratedLogData final : public AbstractLogData {
public:
    uint64_t appended = 0;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        return generatedLogLine( line.get() );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return untabify( generatedLogLine( line.get() ) );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        logsquirl::vector<QString> lines;
        const auto end = std::min( first.get() + count.get(), doGetNbLine().get() );
        for ( auto line = first.get(); line < end; ++line ) {
            lines.push_back( generatedLogLine( line ) );
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
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( LogLineCount + appended );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( LongestLogLine );
    }
    LineLength doGetLineLength( LineNumber line ) const override
    {
        return LineLength(
            static_cast<LineLength::UnderlyingType>( generatedLogLine( line.get() ).size() ) );
    }
    void doSetDisplayEncoding( const char* ) override {}
    QTextCodec* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}
};

class BenchmarkedView : public AbstractLogView {
public:
    BenchmarkedView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, quickFindPattern, /* initialTextWrap */ true )
    {
    }

    // Before #243 a text view asked its subclass what each Log Line is; since
    // then it asks its line mapping. Declared without override so that this
    // builds on both sides of an A/B comparison.
    AbstractLogData::LineType lineType( LineNumber ) const // NOLINT
    {
        return {};
    }
};

constexpr int Notch = 120;

void turnWheel( AbstractLogView& view, int angleDeltaY )
{
    const QPointF inside{ 100.0, 10.0 };
    QWheelEvent wheel( inside, view.viewport()->mapToGlobal( inside ), QPoint{},
                       QPoint{ 0, angleDeltaY }, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                       false );
    QCoreApplication::sendEvent( view.viewport(), &wheel );
}

void pressKey( AbstractLogView& view, Qt::Key key )
{
    QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
    QCoreApplication::sendEvent( &view, &press );
}

} // namespace

TEST_CASE( "text view scroll benchmarks", "[textview-scroll-benchmark]" )
{
    GeneratedLogData logData;
    const QuickFindPattern quickFindPattern;
    BenchmarkedView view( &logData, &quickFindPattern );
    view.setFrameShape( QFrame::NoFrame );
    view.resize( 800, 600 );
    view.show();
    QCoreApplication::processEvents();
    view.setPresentationPolicy( testSettingsPolicies().presentation );
    view.updateData();

    const auto* scrollBar = view.verticalScrollBar();
    const int middle = scrollBar->maximum() / 2;
    view.verticalScrollBar()->setValue( middle );
    view.viewport()->repaint();

    BENCHMARK( "wheel: 20 notches down and 20 up" )
    {
        for ( int notch = 0; notch < 20; ++notch ) {
            turnWheel( view, -Notch );
        }
        for ( int notch = 0; notch < 20; ++notch ) {
            turnWheel( view, Notch );
        }
        return view.getTopLine();
    };

    BENCHMARK( "wheel: 20 notches down and 20 up, each painted" )
    {
        for ( int notch = 0; notch < 20; ++notch ) {
            turnWheel( view, -Notch );
            view.viewport()->repaint();
        }
        for ( int notch = 0; notch < 20; ++notch ) {
            turnWheel( view, Notch );
            view.viewport()->repaint();
        }
        return view.getTopLine();
    };

    BENCHMARK( "keys: 5 pages down and 5 up" )
    {
        for ( int page = 0; page < 5; ++page ) {
            pressKey( view, Qt::Key_PageDown );
        }
        for ( int page = 0; page < 5; ++page ) {
            pressKey( view, Qt::Key_PageUp );
        }
        return view.getTopLine();
    };

    BENCHMARK( "scrollbar: dragged over 200 values" )
    {
        for ( int value = 0; value < 200; ++value ) {
            view.verticalScrollBar()->setSliderPosition( middle
                                                         + ( value % 2 == 0 ? value : -value ) );
        }
        return view.getTopLine();
    };

    BENCHMARK( "data changed: a Log Line appended" )
    {
        ++logData.appended;
        view.updateData();
        return view.getTopLine();
    };

    BENCHMARK( "width changed: narrower and wider again, each painted" )
    {
        view.resize( 640, 600 );
        view.viewport()->repaint();
        view.resize( 800, 600 );
        view.viewport()->repaint();
        return view.getTopLine();
    };
}

int main( int argc, char* argv[] )
{
    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );

    // What the text view reads for its shortcuts and its painting.
    Configuration::getSynced();
    HighlighterSetCollection::getSynced();

    return Catch::Session().run( argc, argv );
}
