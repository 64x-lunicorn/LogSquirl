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
#include "configuration.h"
#include "fake_log_data.h"
#include "linemapping.h"
#include "quickfindpattern.h"
#include "selection.h"
#include "shortcuts.h"

#include <QShortcut>
#include <QSignalSpy>

#include <algorithm>
#include <iterator>
#include <memory>
#include <tuple>
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

namespace {

// Log Lines with tabs, characters outside the Basic Multilingual Plane and an
// empty one, whose expanded text has its tabs expanded as a Log File's has.
// Counts the Log Lines read (FakeLogData reads several at once one by one).
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable uint64_t linesRead = 0;

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        ++linesRead;
        return FakeLogData::doGetLineString( line );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        ++linesRead;
        return untabify( FakeLogData::doGetLineString( line ) );
    }
};

QStringList mixedLogLineTexts()
{
    QStringList texts;
    for ( int line = 0; line < NbLogLines; ++line ) {
        switch ( line % 5 ) {
        case 0:
            texts.append( QStringLiteral( "log\tline %1\twith tabs" ).arg( line ) );
            break;
        case 1:
            texts.append( QStringLiteral( "wide 日本 \U0001F600 line %1" ).arg( line ) );
            break;
        case 2:
            texts.append( QString{} );
            break;
        default:
            texts.append( logLineText( static_cast<uint64_t>( line ) ) );
        }
    }
    return texts;
}

LineLength textLength( const Selection& selection, const LineMapping& lines,
                       const AbstractLogData& shownText )
{
    return LineLength( static_cast<LineLength::UnderlyingType>(
        selection.getSelectedText( lines, shownText ).size() ) );
}

void triggerShortcut( QWidget& view, const char* action )
{
    const auto keys = ShortcutAction::shortcutKeys( action, Configuration::get().shortcuts() );
    REQUIRE_FALSE( keys.isEmpty() );

    for ( auto* shortcut : view.findChildren<QShortcut*>() ) {
        if ( shortcut->key() == keys.first() ) {
            Q_EMIT shortcut->activated();
            return;
        }
    }
    FAIL( "no shortcut for " << action );
}

} // namespace

SCENARIO( "The length of a selection's text is known without building the text",
          "[linemapping][selection]" )
{
    const CountingLogData logFile{ mixedLogLineTexts() };

    GIVEN( "every Log Line shown, some with tabs, wide characters or none at all" )
    {
        const EveryLogLine lines{ &logFile };
        SelectedTextLength length;

        const auto requireSameLength = [ & ]( const Selection& selection ) {
            REQUIRE( length.of( selection, lines, logFile )
                     == textLength( selection, lines, logFile ) );
        };

        THEN( "a single Log Line's length is its text's" )
        {
            for ( uint64_t line = 0; line < 5; ++line ) {
                Selection selection;
                selection.selectLine( LineNumber( line ) );
                requireSameLength( selection );
            }
        }

        THEN( "a portion's length is its expanded text's, also where it passes the end" )
        {
            Selection selection;
            selection.selectPortion( 0_lnum, 2_lcol, 12_lcol );
            requireSameLength( selection );
            selection.selectPortion( 0_lnum, 20_lcol, 200_lcol );
            requireSameLength( selection );
            selection.selectPortion( 1_lnum, 3_lcol, 9_lcol );
            requireSameLength( selection );
            selection.selectPortion( 2_lnum, 0_lcol, 4_lcol );
            requireSameLength( selection );
            selection.selectPortion( 3_lnum, 90_lcol, 95_lcol );
            requireSameLength( selection );
        }

        THEN( "a range's length is its text's, however it grows, shrinks or moves" )
        {
            Selection selection;
            const std::vector<std::pair<uint64_t, uint64_t>> ranges{
                { 4, 4 },   { 4, 5 },   { 4, 9 },   { 3, 9 },   { 0, 9 },   { 2, 7 },
                { 7, 7 },   { 7, 29 },  { 20, 29 }, { 0, 3 },   { 12, 18 }, { 10, 20 },
                { 14, 16 }, { 16, 14 }, { 0, 29 },  { 29, 29 }, { 28, 40 },
            };
            for ( const auto& [ first, last ] : ranges ) {
                selection.selectRange( LineNumber( first ), LineNumber( last ) );
                INFO( "range " << first << " to " << last );
                requireSameLength( selection );
            }
        }

        WHEN( "a range grows one Log Line at a time" )
        {
            Selection selection;
            selection.selectRange( 0_lnum, 0_lnum );
            REQUIRE( length.of( selection, lines, logFile )
                     == textLength( selection, lines, logFile ) );
            logFile.linesRead = 0;

            for ( uint64_t last = 1; last < NbLogLines; ++last ) {
                selection.selectRange( 0_lnum, LineNumber( last ) );
                std::ignore = length.of( selection, lines, logFile );
            }

            THEN( "each step reads only the Log Line it added" )
            {
                REQUIRE( logFile.linesRead == NbLogLines - 1 );
                REQUIRE( length.of( selection, lines, logFile )
                         == textLength( selection, lines, logFile ) );
            }
        }

        WHEN( "the text shown changed" )
        {
            CountingLogData changing{ mixedLogLineTexts() };
            const EveryLogLine changingLines{ &changing };
            Selection selection;
            selection.selectRange( 0_lnum, 9_lnum );
            std::ignore = length.of( selection, changingLines, changing );

            changing.setLines( logLineTexts( everyLogLine() ) );
            length.forget();

            THEN( "the length is that of the new text" )
            {
                REQUIRE( length.of( selection, changingLines, changing )
                         == textLength( selection, changingLines, changing ) );
            }
        }
    }

    GIVEN( "only some Log Lines shown, their text read by position" )
    {
        const std::vector<uint64_t> shown{ 0, 1, 3, 5, 6, 10, 11, 12, 20, 25 };
        QStringList shownTexts;
        const auto everyText = mixedLogLineTexts();
        for ( const auto line : shown ) {
            shownTexts.append( everyText[ static_cast<qsizetype>( line ) ] );
        }
        const CountingLogData shownText{ shownTexts };
        const VectorLines lines{ &logFile, shown };
        SelectedTextLength length;

        THEN( "a range's length is that of the Log Lines shown in it" )
        {
            Selection selection;
            const std::vector<std::pair<uint64_t, uint64_t>> ranges{
                { 1, 6 }, { 1, 12 }, { 2, 12 }, { 0, 29 }, { 7, 9 }, { 11, 11 }, { 11, 25 },
            };
            for ( const auto& [ first, last ] : ranges ) {
                selection.selectRange( LineNumber( first ), LineNumber( last ) );
                INFO( "range " << first << " to " << last );
                REQUIRE( length.of( selection, lines, shownText )
                         == textLength( selection, lines, shownText ) );
            }
        }
    }
}

SCENARIO( "A text view extending its selection line by line reads only the Log Lines it adds",
          "[linemapping][abstractlogview][selection]" )
{
    constexpr uint64_t NbManyLogLines = 2000;
    QStringList texts;
    for ( uint64_t line = 0; line < NbManyLogLines; ++line ) {
        texts.append( line % 7 == 0 ? QStringLiteral( "tab\tline %1" ).arg( line )
                                    : logLineText( line ) );
    }
    CountingLogData logFile{ texts };
    QuickFindPattern quickFindPattern;
    AbstractLogView view( &logFile, std::make_unique<EveryLogLine>( &logFile ), &quickFindPattern,
                          false );
    view.resize( 400, 200 );
    view.registerShortcuts();

    view.selectAndDisplayLine( 10_lnum );
    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    logFile.linesRead = 0;

    WHEN( "Shift+Down extends it over 500 Log Lines" )
    {
        constexpr uint64_t Steps = 500;
        for ( uint64_t step = 0; step < Steps; ++step ) {
            triggerShortcut( view, ShortcutAction::LogViewSelectLinesDown );
        }
        const auto linesRead = logFile.linesRead;

        THEN( "it reports the length of the selected text" )
        {
            REQUIRE( selected.count() == static_cast<int>( Steps ) );
            REQUIRE( qvariant_cast<LinesCount>( selected.last().at( 1 ) )
                     == LinesCount( Steps + 1 ) );
            REQUIRE( qvariant_cast<LineLength>( selected.last().at( 3 ) ).get()
                     == view.getSelectedText().size() );
        }

        THEN( "it read about one Log Line per step, not the whole selection each time" )
        {
            REQUIRE( linesRead < 4 * Steps );
        }
    }
}
