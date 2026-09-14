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

#include <QFont>
#include <QWidget>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "log_view_scrolling.h"
#include "logdata.h"
#include "quickfindpattern.h"
#include "test_policies.h"

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

protected:
    AbstractLogData::LineType lineType( LineNumber ) const override
    {
        return {};
    }
};

} // namespace

SCENARIO( "AbstractLogView updateDisplaySize keeps charWidth_ safe", "[abstractlogview][viewport]" )
{
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess };
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

        THEN( "an arrow key moves one Visual Line, into the next Log Line and back" )
        {
            requireArrowKeysStepOneVisualLineAcrossLogLines( view );
        }

        THEN( "Page Down followed by Page Up returns to the same Scroll Position" )
        {
            requirePageDownThenPageUpReturns( view );
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

#include "abstractlogview_test.moc"
