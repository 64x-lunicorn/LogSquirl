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

#include <catch2/catch.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <QCursor>
#include <QFile>
#include <QFont>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "log_view_scrolling.h"
#include "logdata.h"
#include "qfnotifications.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QSignalSpy>

namespace {

// Minimal concrete subclass for testing AbstractLogView
class TestLogView : public AbstractLogView {
    Q_OBJECT
public:
    TestLogView( const AbstractLogData* logData, const QuickFindPattern* qfp,
                 QWidget* parent = nullptr, bool initialTextWrap = false )
        : AbstractLogView( logData, qfp, initialTextWrap, parent )
    {
    }

    using AbstractLogView::linesToSave;
};

} // namespace

SCENARIO( "AbstractLogView updateDisplaySize keeps charWidth_ safe", "[abstractlogview][viewport]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    QuickFindPattern qfp;

    GIVEN( "A log view widget created with default font" )
    {
        TestLogView view( &logData, &qfp );
        view.resize( 800, 600 );

        WHEN( "updateFont is called with a very small font" )
        {
            // A 1-pixel font may report zero width for "m" on some platforms
            QFont tinyFont( "Monospace", 1 );
            view.updateFont( tinyFont );

            THEN( "the view does not crash and remains in a valid state" )
            {
                // If charWidth_ were 0, this would trigger a division by zero
                // internally in getNbVisibleCols(). The show()/repaint() path
                // exercises that code.
                view.show();
                view.repaint();
                REQUIRE( true ); // Reaching here means no crash
            }
        }

        WHEN( "updateFont is called with a normal font" )
        {
            QFont normalFont( "Courier", 12 );
            view.updateFont( normalFont );

            THEN( "the view renders without crashing" )
            {
                view.show();
                view.repaint();
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "A text view scrolls under the Presentation Policy it was handed",
          "[abstractlogview][presentationpolicy]" )
{
    using namespace logviewscrolling;

    // No settings store takes part: the Policy is a literal, and what the view
    // does with the wheel follows from it alone.
    const FakeLogData logData{ tallLogLines() };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    auto policy = testSettingsPolicies().presentation;

    // The same notch, turned without the modifier: the yardstick the fast one
    // is compared against.
    const auto afterAPlainNotch = [ & ]() {
        moveTo( view, ScrollPosition{} );
        turnWheel( view, -QWheelEvent::DefaultDeltasPerStep );
        return view.scrollPosition();
    }();
    REQUIRE( afterAPlainNotch > ScrollPosition{} );

    GIVEN( "a text view handed a Policy that turns fast scrolling on" )
    {
        policy.fastScrollEnabled = true;
        policy.fastScrollMultiplier = 5;
        view.setPresentationPolicy( policy );

        WHEN( "the wheel is turned one notch with the fast scroll modifier held" )
        {
            moveTo( view, ScrollPosition{} );
            turnWheel( view, -QWheelEvent::DefaultDeltasPerStep, Qt::AltModifier );

            THEN( "the view moves further than the same notch without it" )
            {
                REQUIRE( view.scrollPosition() > afterAPlainNotch );
            }
        }
    }

    GIVEN( "a text view handed a Policy that turns fast scrolling off" )
    {
        policy.fastScrollEnabled = false;
        view.setPresentationPolicy( policy );

        WHEN( "the wheel is turned one notch with the fast scroll modifier held" )
        {
            moveTo( view, ScrollPosition{} );
            turnWheel( view, -QWheelEvent::DefaultDeltasPerStep, Qt::AltModifier );

            THEN( "the view moves exactly as far as it does without the modifier" )
            {
                REQUIRE( view.scrollPosition() == afterAPlainNotch );
            }
        }
    }
}

SCENARIO( "A text view says where a Log Line sits in its Viewport", "[abstractlogview][viewport]" )
{
    using namespace logviewscrolling;

    const FakeLogData logData{ tallLogLines() };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "a view standing partway through a Log Line taller than the Viewport" )
    {
        moveTo( view, ScrollPosition{ TallLine, 150 } );

        THEN( "its Viewport layout says which Visual Line the top row shows" )
        {
            const auto layout = view.viewportLayout();
            const auto onTopRow = layout.visualLineAtPoint( TopRowY );
            REQUIRE( onTopRow.has_value() );
            REQUIRE( layout.visualLines()[ *onTopRow ].lineNumber == TallLine );
            REQUIRE( layout.visualLines()[ *onTopRow ].wrappedLineIndex == 150 );

            const auto line = layout.lineAtPoint( TopRowY );
            REQUIRE( line.has_value() );
            REQUIRE( *line == TallLine );
        }

        THEN( "asking it where things sit moves the view nowhere" )
        {
            const auto before = view.scrollPosition();
            const auto layout = view.viewportLayout();
            layout.lineAtPoint( lastRowY( view ) );
            layout.filePositionAtPoint( 0, TopRowY );
            REQUIRE( view.scrollPosition() == before );
        }

        THEN( "asked again once the view has moved, its Viewport layout shows the move" )
        {
            const auto onTopRowBefore = view.viewportLayout().visualLineAtPoint( TopRowY );
            REQUIRE( onTopRowBefore.has_value() );
            REQUIRE( view.viewportLayout().visualLines()[ *onTopRowBefore ].wrappedLineIndex
                     == 150 );

            moveTo( view, ScrollPosition{ TallLine, 160 } );

            const auto& layout = view.viewportLayout();
            REQUIRE( layout.input().scrollPosition == ScrollPosition{ TallLine, 160 } );
            const auto onTopRow = layout.visualLineAtPoint( TopRowY );
            REQUIRE( onTopRow.has_value() );
            REQUIRE( layout.visualLines()[ *onTopRow ].wrappedLineIndex == 160 );
        }
    }
}

SCENARIO( "A text view scrolls by Visual Lines", "[abstractlogview][scrollposition]" )
{
    using namespace logviewscrolling;

    const FakeLogData logData{ tallLogLines() };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "a Log Line taller than the Viewport in the middle of the Log File" )
    {
        THEN( "a wheel step moves the same number of Visual Lines, inside it and across Log Lines, "
              "and reaches its last Visual Line" )
        {
            requireWheelStepsMoveTheSameVisualLinesEverywhere( view );
        }

        THEN( "the scrollbar counts whole Log Lines" )
        {
            requireScrollbarCountsWholeLogLines( view );
        }

        THEN( "turning wrapping off and on keeps the same Log Line at the top" )
        {
            requireWrapToggleKeepsTheLogLineAtTheTop( view );
        }
    }
}

SCENARIO( "Every Visual Line of a Log Line with more than 10,000 of them can be reached",
          "[abstractlogview][scrollposition]" )
{
    using namespace logviewscrolling;

    // 2,000 numbered words of six characters: 12,000 Visual Lines one column wide.
    QString numberedWords;
    for ( int word = 0; word < 2000; ++word ) {
        numberedWords += QStringLiteral( "%1 " ).arg( word, 5, 10, QLatin1Char( '0' ) );
    }
    QStringList lines;
    for ( int line = 0; line < 10; ++line ) {
        lines << QStringLiteral( "a" );
    }
    lines << numberedWords;
    for ( int line = 0; line < 100; ++line ) {
        lines << QStringLiteral( "b" );
    }

    const FakeLogData logData{ lines };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    GIVEN( "the view scrolled a page at a time, then a Visual Line at a time, to the last digit" )
    {
        // Word 1999 takes the characters 11994 to 11998.
        const ScrollPosition lastDigit{ 10_lnum, 11998 };
        for ( int page = 0; page < 20000 && view.scrollPosition() < lastDigit; ++page ) {
            pressKey( view, Qt::Key_PageDown );
        }
        for ( int step = 0; step < 20000 && view.scrollPosition() > lastDigit; ++step ) {
            pressKey( view, Qt::Key_Up );
        }
        REQUIRE( view.scrollPosition() == lastDigit );

        WHEN( "its top row is double-clicked" )
        {
            doubleClickTopRow( view );

            THEN( "the word under it is selected" )
            {
                REQUIRE( view.getSelectedText() == QStringLiteral( "01999" ) );
            }
        }

        WHEN( "the view moves up two words and its top row is double-clicked" )
        {
            for ( int step = 0; step < 12; ++step ) {
                pressKey( view, Qt::Key_Up );
            }
            doubleClickTopRow( view );

            THEN( "the word under it is selected" )
            {
                REQUIRE( view.getSelectedText() == QStringLiteral( "01997" ) );
            }
        }
    }
}

SCENARIO( "The bottom of a wrapped text view shows exactly the last Visual Line of the Log File",
          "[abstractlogview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log File whose last Log Lines are one Visual Line each" )
    {
        const FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view, logData.getNbLine() );
        }
    }

    GIVEN( "a Log File whose last Log Line is taller than the Viewport" )
    {
        const FakeLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            requireScrollbarMaximumShowsTheLastVisualLineOnTheLastRow( view, logData.getNbLine() );
        }

        THEN( "moving the scrollbar to its maximum from partway up that Log Line lands at the "
              "bottom" )
        {
            requireScrollbarMovedToItsMaximumLandsAtTheBottom( view, logData.getNbLine() );
        }
    }
}

SCENARIO( "A wrapped text view at the bottom as its Log File grows",
          "[abstractlogview][scrollposition][bottom][follow]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "Log Lines appended to the Log File" )
    {
        FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        const auto appendLines = [ & ]() {
            auto lines = tallLogLines();
            lines << QStringLiteral( "b" ) << tallLine();
            logData.setLines( lines );
            view.updateData();
            return logData.getNbLine();
        };

        THEN( "follow mode keeps the new last Visual Line on the last row" )
        {
            requireFollowKeepsTheLastVisualLineOnTheLastRow( view, appendLines );
        }
    }
}

SCENARIO( "A re-wrap keeps the text on the top row of a wrapped text view",
          "[abstractlogview][scrollposition][rewrap]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log Line taller than the Viewport" )
    {
        const FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "widening and narrowing the view keep the text at the top on the top row" )
        {
            requireResizingKeepsTheTopRowText( view );
        }

        THEN( "a larger font keeps the text at the top on the top row" )
        {
            showWide( view );
            requireRewrapKeepsTheTopRowText( view, [ &view ]() {
                auto font = view.font();
                font.setPointSize( font.pointSize() > 0 ? font.pointSize() * 2 : 24 );
                view.updateFont( font );
            } );
        }

        THEN( "showing and hiding line numbers keep the text at the top on the top row" )
        {
            showWide( view );
            requireRewrapKeepsTheTopRowText( view,
                                             [ &view ]() { view.setLineNumbersVisible( true ); } );
            requireRewrapKeepsTheTopRowText( view,
                                             [ &view ]() { view.setLineNumbersVisible( false ); } );
        }
    }

    GIVEN( "a view at the bottom of a Log File whose last Log Line is taller than the Viewport" )
    {
        const FakeLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "it stays at the bottom through a resize" )
        {
            requireResizingKeepsTheViewAtTheBottom( view, logData.getNbLine() );
        }
    }
}

SCENARIO( "A jump moves a wrapped text view only when its target is off screen",
          "[abstractlogview][scrollposition][jump]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a Log Line taller than the Viewport in the middle of the Log File" )
    {
        const FakeLogData logData{ tallLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        const JumpToLogLine selectLine
            = [ &view ]( LineNumber line ) { view.selectAndDisplayLine( line ); };
        // What selecting a Match in the Filtered View does to the main view.
        const JumpToLogLine selectMatch = [ &view ]( LineNumber line ) {
            view.selectPortionAndDisplayLine( line, 1_lcount, 0_lcol, 1_length );
        };

        THEN( "going to a Match or Mark already wholly visible does not scroll" )
        {
            requireJumpToAWhollyVisibleLogLineDoesNotScroll( view, selectLine );
            requireJumpToAWhollyVisibleLogLineDoesNotScroll( view, selectMatch );
        }

        THEN( "going to a Match or Mark off screen puts its first Visual Line on the top row" )
        {
            requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( view, selectLine );
            requireJumpOffScreenPutsTheFirstVisualLineOnTheTopRow( view, selectMatch );
        }
    }

    GIVEN( "text QuickFind finds in the 40th Visual Line of a tall Log Line" )
    {
        const FakeLogData logData{ quickFindLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        THEN( "off screen, that Visual Line is put on the top row" )
        {
            requireQuickFindPutsTheVisualLineOfTheFoundTextOnTheTopRow( view, qfp );
        }

        THEN( "already wholly visible, the view does not scroll" )
        {
            requireQuickFindOnAWhollyVisibleVisualLineDoesNotScroll( view, qfp );
        }
    }
}

namespace {

// A FakeLogData that counts the Log Lines read from it.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable uint64_t linesRead = 0;

protected:
    // Every other read of FakeLogData goes through this one.
    QString doGetLineString( LineNumber line ) const override
    {
        ++linesRead;
        return FakeLogData::doGetLineString( line );
    }
};

} // namespace

SCENARIO( "Updating the scroll bars reads no more than one Viewport height of Log Lines",
          "[abstractlogview][scrollposition][bottom]" )
{
    using namespace logviewscrolling;

    QuickFindPattern qfp;

    GIVEN( "a wrapped view of 10,000 Log Lines of one Visual Line each" )
    {
        QStringList lines;
        for ( int line = 0; line < 10000; ++line ) {
            lines << QStringLiteral( "b" );
        }
        const CountingLogData logData{ lines };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        WHEN( "the Log File changes" )
        {
            logData.linesRead = 0;
            view.updateData();

            THEN( "no more Log Lines are read than the Viewport has rows" )
            {
                // Every row plus a partly hidden one: at least as many as it has rows.
                REQUIRE( logData.linesRead > 0 );
                REQUIRE( logData.linesRead
                         <= static_cast<uint64_t>( view.verticalScrollBar()->pageStep() ) );
            }
        }
    }

    GIVEN( "a wrapped view whose last Log Line is taller than the Viewport" )
    {
        const CountingLogData logData{ tallLastLogLines() };
        TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
        showOneColumnWide( view );

        WHEN( "the Log File changes" )
        {
            logData.linesRead = 0;
            view.updateData();

            THEN( "only that last Log Line is read" )
            {
                REQUIRE( logData.linesRead == 1 );
            }
        }
    }
}

SCENARIO( "QuickFind in the main view searches every Log Line", "[abstractlogview][quickfind]" )
{
    const FakeLogData logData{ QStringList{
        QStringLiteral( "alpha" ), QStringLiteral( "found one" ), QStringLiteral( "beta" ),
        QStringLiteral( "found two" ), QStringLiteral( "gamma" ) } };
    QuickFindPattern qfp;
    qfp.changeSearchPattern( QStringLiteral( "found" ), /* useExtendedRegexp */ false );
    TestLogView view( &logData, &qfp );
    view.selectAndDisplayLine( 0_lnum );

    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    const auto waitForSelection
        = [ & ]( qsizetype count ) { return selected.count() >= count || selected.wait( 10000 ); };
    const auto selectedLine = [ & ]( qsizetype index ) {
        return qvariant_cast<LineNumber>( selected.at( index ).at( 0 ) );
    };

    SECTION( "searching forward selects each match, then reports the end of the file" )
    {
        QSignalSpy notifications( &view, &AbstractLogView::notifyQuickFind );

        view.searchForward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 1_lnum );

        view.searchForward();
        REQUIRE( waitForSelection( 2 ) );
        REQUIRE( selectedLine( 1 ) == 3_lnum );

        view.searchForward();
        const auto reachedEndOfFile = [ & ]() {
            for ( const auto& arguments : notifications ) {
                if ( qvariant_cast<QFNotification>( arguments.at( 0 ) ).message()
                     == QFNotificationReachedEndOfFile{}.message() ) {
                    return true;
                }
            }
            return false;
        };
        REQUIRE( ( reachedEndOfFile() || notifications.wait( 10000 ) ) );
        QCoreApplication::processEvents();
        REQUIRE( reachedEndOfFile() );
        REQUIRE( selected.count() == 2 );
    }

    SECTION( "searching backward from the last Log Line selects the last match" )
    {
        view.selectAndDisplayLine( 4_lnum );
        selected.clear();
        view.searchBackward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 3_lnum );
    }

    SECTION( "an aborted incremental QuickFind restores the initial selection" )
    {
        view.incrementallySearchForward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 1_lnum );

        view.incrementalSearchAbort();
        REQUIRE( view.getSelectedText() == QStringLiteral( "alpha" ) );
    }

    SECTION( "a stopped incremental QuickFind keeps its match selected" )
    {
        view.incrementallySearchForward();
        REQUIRE( waitForSelection( 1 ) );
        REQUIRE( selectedLine( 0 ) == 1_lnum );

        view.incrementalSearchStop();
        REQUIRE( view.getSelectedText() == QStringLiteral( "found" ) );
    }
}

SCENARIO( "a save from the main view reads the lines of its data", "[abstractlogview][linessaver]" )
{
    QStringList lines;
    for ( int line = 0; line < 7001; ++line ) {
        lines.append( QStringLiteral( "line %1" ).arg( line ) );
    }
    FakeLogData logData{ lines };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp );

    const auto readLines = view.linesToSave();

    THEN( "it reads the lines at the positions asked for" )
    {
        REQUIRE( readLines( 0_lnum, 7001_lcount ) == logData.getLines( 0_lnum, 7001_lcount ) );
        REQUIRE( readLines( 4998_lnum, 3_lcount ) == logData.getLines( 4998_lnum, 3_lcount ) );
    }
}

SCENARIO( "Selection autoscroll moves a wrapped text view in Visual Lines",
          "[abstractlogview][scrollposition]" )
{
    using namespace logviewscrolling;

    const FakeLogData logData{ tallLogLines() };
    QuickFindPattern qfp;
    TestLogView view( &logData, &qfp, nullptr, /* initialTextWrap */ true );
    showOneColumnWide( view );

    const ScrollPosition start{ TallLine, 150 };
    moveTo( view, start );

    GIVEN( "a selection started on the top row, and the mouse dragged just below the Viewport" )
    {
        const auto onText = topRowText();
        QMouseEvent press( QEvent::MouseButtonPress, onText, view.viewport()->mapToGlobal( onText ),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.viewport(), &press );

        const QPointF below{ onText.x(), static_cast<qreal>( view.viewport()->height() + 8 ) };
        QCursor::setPos( view.viewport()->mapToGlobal( below.toPoint() ) );
        QMouseEvent move( QEvent::MouseMove, below, view.viewport()->mapToGlobal( below ),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( view.viewport(), &move );

        const auto moved = waitUiState( [ & ]() { return view.scrollPosition() != start; }, 5000 );

        QMouseEvent release( QEvent::MouseButtonRelease, below,
                             view.viewport()->mapToGlobal( below ), Qt::LeftButton, Qt::NoButton,
                             Qt::NoModifier );
        QCoreApplication::sendEvent( view.viewport(), &release );

        THEN( "the view scrolls down Visual Lines of the tall Log Line, not to the next Log Line" )
        {
            REQUIRE( moved );
            REQUIRE( view.scrollPosition().lineNumber == TallLine );
            REQUIRE( view.scrollPosition().visualLineIndex > start.visualLineIndex );
        }
    }
}

namespace {

// Every Log Line at its own position, saved through what *readLines holds
// when a save starts.
class SavedThroughReader : public EveryLogLine {
public:
    SavedThroughReader( const AbstractLogData* logData, const DisplayedLinesReader* readLines )
        : EveryLogLine( logData )
        , readLines_( readLines )
    {
    }

    DisplayedLinesReader linesToSave() const override
    {
        return *readLines_;
    }

private:
    const DisplayedLinesReader* readLines_;
};

// A text view whose saves read through readLines.
class SavingLogView : public AbstractLogView {
public:
    using AbstractLogView::saveLinesTo;

    SavingLogView( const AbstractLogData* logData, const QuickFindPattern* qfp )
        : AbstractLogView( logData, std::make_unique<SavedThroughReader>( logData, &readLines ),
                           qfp, false )
    {
    }

    DisplayedLinesReader readLines;
};

QByteArray contentOf( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

} // namespace

SCENARIO( "a save from a text view replaces the file only when every line was written",
          "[abstractlogview][linessaver]" )
{
    QStringList lines;
    for ( int line = 0; line < 50001; ++line ) {
        lines.append( QStringLiteral( "line %1" ).arg( line ) );
    }
    const FakeLogData logData{ lines };
    QuickFindPattern qfp;
    SavingLogView view( &logData, &qfp );

    // The destination is a plain file that nothing holds open: a QTemporaryFile
    // keeps its handle open even after close(), and Windows can't replace an
    // open file, so the save's commit would fail there.
    const QByteArray previousContent = "previous content\n";
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    QFile file{ dir.filePath( "abstractlogview_save_test.log" ) };
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( previousContent ) == previousContent.size() );
    file.close();

    GIVEN( "a save that runs to its end" )
    {
        view.readLines = [ &logData ]( LineNumber first, LinesCount count ) {
            return logData.getLines( first, count );
        };

        view.saveLinesTo( file.fileName(), 0_lnum, 50001_lnum );

        THEN( "the file holds every line, encoded as UTF-8" )
        {
#if defined( Q_OS_WIN )
            const QByteArray lineEnding = "\n";
#else
            const QByteArray lineEnding = "\r\n";
#endif
            QByteArray expected = "\xEF\xBB\xBF";
            for ( const auto& line : lines ) {
                expected += line.toUtf8() + lineEnding;
            }
            REQUIRE( contentOf( file.fileName() ) == expected );
        }
    }

    GIVEN( "a save whose progress dialog is cancelled while it reads its second chunk" )
    {
        std::atomic<bool> dialogCancelled = false;
        std::atomic<int> chunksRead = 0;
        view.readLines = [ & ]( LineNumber first, LinesCount count ) {
            if ( ++chunksRead > 1 ) {
                // Holds the save until the dialog is cancelled, and then long
                // enough for the interrupt to follow.
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
                while ( !dialogCancelled && std::chrono::steady_clock::now() < deadline ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            }
            return logData.getLines( first, count );
        };

        bool dialogWasShown = false;
        QTimer poll;
        QObject::connect( &poll, &QTimer::timeout, &poll, [ & ]() {
            auto* dialog = view.findChild<QProgressDialog*>();
            if ( dialog != nullptr && dialog->isVisible() ) {
                poll.stop();
                dialogWasShown = true;
                dialog->cancel();
                dialogCancelled = true;
            }
        } );
        poll.start( 10 );

        view.saveLinesTo( file.fileName(), 0_lnum, 50001_lnum );

        THEN( "the save stops, and the file keeps its previous content" )
        {
            REQUIRE( dialogWasShown );
            REQUIRE( chunksRead < 11 );
            REQUIRE( contentOf( file.fileName() ) == previousContent );
        }
    }
}

#include "abstractlogview_test.moc"
