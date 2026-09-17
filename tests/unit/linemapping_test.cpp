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

// A text view is handed a LineMapping (#243): what it asks of one is tested
// here over a mapping whose shown Log Lines and Marks are plain vectors.

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "linemapping.h"
#include "quickfindpattern.h"
#include "selection.h"

#include <QSignalSpy>

#include <algorithm>
#include <iterator>
#include <memory>
#include <vector>

#include <catch2/catch.hpp>

namespace {

constexpr int NbLogLines = 30;

QString logLineText( uint64_t line )
{
    return QStringLiteral( "log line %1" ).arg( line );
}

QStringList logLineTexts( const std::vector<uint64_t>& lines )
{
    QStringList texts;
    for ( const auto line : lines ) {
        texts.append( logLineText( line ) );
    }
    return texts;
}

std::vector<uint64_t> everyLogLine()
{
    std::vector<uint64_t> lines( NbLogLines );
    for ( uint64_t line = 0; line < NbLogLines; ++line ) {
        lines[ line ] = line;
    }
    return lines;
}

// Shows the Log Lines in shown, in order, of a Log File whose Log Lines read
// "log line N"; the Log Lines in marks are Marks, shown or not.
class VectorLines : public LineMapping {
public:
    VectorLines( const AbstractLogData* logFile, std::vector<uint64_t> shown,
                 std::vector<uint64_t> marks = {} )
        : logFile_( logFile )
        , shown_( std::move( shown ) )
        , marks_( std::move( marks ) )
    {
    }

    OptionalLineNumber logLineAt( LineNumber position ) const override
    {
        if ( position.get() < shown_.size() ) {
            return LineNumber( shown_[ position.get() ] );
        }
        return std::nullopt;
    }

    LineNumber nearestPositionOf( LineNumber logLine ) const override
    {
        const auto after = std::upper_bound( shown_.begin(), shown_.end(), logLine.get() );
        const auto shownUpTo = static_cast<uint64_t>( after - shown_.begin() );
        return LineNumber( shownUpTo > 0 ? shownUpTo - 1 : 0 );
    }

    LineType lineType( LineNumber logLine ) const override
    {
        return std::ranges::find( marks_, logLine.get() ) != marks_.end()
                   ? LineType{ AbstractLogData::LineTypeFlags::Mark }
                   : LineType{ AbstractLogData::LineTypeFlags::Match };
    }

    LinesCount logLineCount() const override
    {
        return logFile_->getNbLine();
    }

    OptionalLineNumber markAfter( LineNumber logLine ) const override
    {
        const auto mark = std::upper_bound( marks_.begin(), marks_.end(), logLine.get() );
        return mark != marks_.end() ? OptionalLineNumber( LineNumber( *mark ) ) : std::nullopt;
    }

    OptionalLineNumber markBefore( LineNumber logLine ) const override
    {
        const auto mark = std::lower_bound( marks_.begin(), marks_.end(), logLine.get() );
        return mark != marks_.begin() ? OptionalLineNumber( LineNumber( *std::prev( mark ) ) )
                                      : std::nullopt;
    }

    const AbstractLogData& logFile() const override
    {
        return *logFile_;
    }

    QuickFindLines quickFindLines() const override
    {
        SearchResultArray lines;
        for ( const auto line : shown_ ) {
            lines.add( line );
        }
        return QuickFindLines::someLogLines( *logFile_, std::move( lines ) );
    }

    DisplayedLinesReader linesToSave() const override
    {
        return [ this ]( LineNumber first, LinesCount count ) {
            logsquirl::vector<QString> text;
            for ( auto position = first; position < first + count; ++position ) {
                const auto logLine = logLineAt( position );
                text.push_back( logLine ? logFile_->getLineString( *logLine ) : QString{} );
            }
            return text;
        };
    }

private:
    const AbstractLogData* logFile_;
    std::vector<uint64_t> shown_;
    std::vector<uint64_t> marks_;
};

} // namespace

SCENARIO( "A line mapping answers in Log Lines over the positions it shows", "[linemapping]" )
{
    const FakeLogData logFile{ logLineTexts( everyLogLine() ) };

    GIVEN( "Log Lines 3, 7, 10 and 15 shown, and Marks on 5, 7, 12 and 15" )
    {
        const VectorLines lines{ &logFile, { 3, 7, 10, 15 }, { 5, 7, 12, 15 } };

        THEN( "only the Log Lines shown are shown" )
        {
            REQUIRE( lines.shows( 7_lnum ) );
            REQUIRE_FALSE( lines.shows( 8_lnum ) );
            REQUIRE_FALSE( lines.shows( 0_lnum ) );
            REQUIRE_FALSE( lines.shows( 20_lnum ) );
        }

        THEN( "a Log Line not shown is nearest the one shown before it, or the first one" )
        {
            REQUIRE( lines.nearestShownLogLine( 10_lnum ) == 10_lnum );
            REQUIRE( lines.nearestShownLogLine( 9_lnum ) == 7_lnum );
            REQUIRE( lines.nearestShownLogLine( 1_lnum ) == 3_lnum );
            REQUIRE( lines.nearestShownLogLine( 29_lnum ) == 15_lnum );
        }

        THEN( "the Log Lines shown from one Log Line through another are those in between" )
        {
            using Positions = std::pair<LineNumber, LineNumber>;
            REQUIRE( lines.positionsFromTo( 4_lnum, 12_lnum ) == Positions{ 1_lnum, 2_lnum } );
            REQUIRE( lines.positionsFromTo( 3_lnum, 15_lnum ) == Positions{ 0_lnum, 3_lnum } );
            REQUIRE( lines.positionsFromTo( 0_lnum, 3_lnum ) == Positions{ 0_lnum, 0_lnum } );
            REQUIRE( lines.shownLogLinesFromTo( 4_lnum, 12_lnum )
                     == logsquirl::vector<LineNumber>{ 7_lnum, 10_lnum } );

            REQUIRE_FALSE( lines.positionsFromTo( 8_lnum, 9_lnum ).has_value() );
            REQUIRE_FALSE( lines.positionsFromTo( 0_lnum, 2_lnum ).has_value() );
            REQUIRE_FALSE( lines.positionsFromTo( 16_lnum, 29_lnum ).has_value() );
            REQUIRE( lines.shownLogLinesFromTo( 8_lnum, 9_lnum ).empty() );
        }

        THEN( "the next and previous Mark are the nearest Marks shown" )
        {
            REQUIRE( lines.shownMarkAfter( 0_lnum ) == 7_lnum );
            REQUIRE( lines.shownMarkAfter( 7_lnum ) == 15_lnum );
            REQUIRE_FALSE( lines.shownMarkAfter( 15_lnum ).has_value() );

            REQUIRE( lines.shownMarkBefore( 29_lnum ) == 15_lnum );
            REQUIRE( lines.shownMarkBefore( 15_lnum ) == 7_lnum );
            REQUIRE_FALSE( lines.shownMarkBefore( 7_lnum ).has_value() );
        }

        THEN( "a selected range holds the Log Lines shown in it, and reads their text" )
        {
            const FakeLogData shownText{ logLineTexts( { 3, 7, 10, 15 } ) };
            Selection selection;
            selection.selectRange( 7_lnum, 15_lnum );

            REQUIRE( selection.getLines( lines )
                     == logsquirl::vector<LineNumber>{ 7_lnum, 10_lnum, 15_lnum } );
            REQUIRE( selection.getSelectedLinesCount( lines ) == 3_lcount );
            REQUIRE( selection.getSelectedText( lines, shownText, true )
                         .remove( QChar::CarriageReturn )
                         .split( QChar::LineFeed )
                     == QStringList{ "7: log line 7", "10: log line 10", "15: log line 15" } );
        }
    }

    GIVEN( "every Log Line shown by the main view's mapping" )
    {
        const EveryLogLine lines{ &logFile };

        THEN( "each is shown at its own position, and one not yet in the Log File too" )
        {
            REQUIRE( lines.logLineAt( 12_lnum ) == 12_lnum );
            REQUIRE_FALSE( lines.logLineAt( LineNumber( NbLogLines ) ).has_value() );
            REQUIRE( lines.nearestPositionOf( 40_lnum ) == 40_lnum );
            REQUIRE( lines.positionsFromTo( 4_lnum, 12_lnum ) == std::pair{ 4_lnum, 12_lnum } );
        }
    }
}

SCENARIO( "A text view keeps its selection on the same Log Line when the lines it shows change",
          "[linemapping][abstractlogview]" )
{
    FakeLogData logFile{ logLineTexts( everyLogLine() ) };
    FakeLogData shownText{ logLineTexts( { 3, 7, 10, 15 } ) };
    QuickFindPattern quickFindPattern;

    AbstractLogView view(
        &shownText,
        std::make_unique<VectorLines>( &logFile, std::vector<uint64_t>{ 3, 7, 10, 15 } ),
        &quickFindPattern, false );
    view.resize( 400, 200 );

    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    view.selectAndDisplayLine( 10_lnum );
    REQUIRE( selected.count() == 1 );
    REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 10_lnum );
    REQUIRE( view.getSelectedText() == logLineText( 10 ) );

    WHEN( "two Log Lines above it are shown as well" )
    {
        shownText.setLines( logLineTexts( { 1, 3, 5, 7, 10, 15 } ) );
        view.setLineMapping( std::make_unique<VectorLines>(
            &logFile, std::vector<uint64_t>{ 1, 3, 5, 7, 10, 15 } ) );
        view.updateData();

        THEN( "the same Log Line is still selected" )
        {
            REQUIRE( view.getSelectedText() == logLineText( 10 ) );
        }
    }

    WHEN( "a Log Line not shown is selected" )
    {
        view.selectAndDisplayLine( 12_lnum );

        THEN( "the one shown before it is selected" )
        {
            REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 10_lnum );
            REQUIRE( view.getSelectedText() == logLineText( 10 ) );
        }
    }
}
